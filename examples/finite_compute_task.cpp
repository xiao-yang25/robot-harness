#include "finite_compute_task.hpp"

#include <stdexcept>
#include <utility>

namespace robot_harness::examples {
namespace {
class LocalTaskExecution final : public TaskExecution {
public:
  explicit LocalTaskExecution(ComputeExecutionHost& host) : host_(host) {
  }
  void poll() override {
    host_.poll();
  }
  BindingPhase phase() const override {
    return host_.binding_status().phase;
  }
  ComputeSubmission submit(std::uint64_t iterations, const std::string& reference) override {
    ComputeRequest request;
    request.iterations = iterations;
    request.request_reference = reference;
    return host_.submit(request);
  }
  bool request_cancel(const OperationAuthority& authority) override {
    return host_.request_cancel(authority).status != ControlStatus::kRejected;
  }
  std::optional<OperationReceipt> receipt(const OperationAuthority& authority) const override {
    return host_.receipt(authority);
  }
  const std::optional<ComputeResult>& result() const override {
    return host_.result();
  }
  bool ownership_lost() const override {
    return host_.observation().ownership_lost;
  }
  void begin_shutdown() override {
    host_.begin_shutdown();
  }

private:
  ComputeExecutionHost& host_;
};

// Independent, bounded application check: sum one period directly rather than
// reuse the host's polynomial formula or the worker's accumulated result.
std::uint64_t reference_sum(std::uint64_t iterations) {
  std::uint64_t period = 0;
  std::uint64_t remainder = 0;
  for (std::uint64_t i = 0; i < 1000; ++i) {
    period += i * i;
    if (i < iterations % 1000) {
      remainder += i * i;
    }
  }
  return (iterations / 1000) * period + remainder;
}
}  // namespace

FiniteComputeTask::FiniteComputeTask(ComputeExecutionHost& host,
                                     MonotonicTime observation_budget_ms)
    : owned_execution_(std::make_unique<LocalTaskExecution>(host)), host_(*owned_execution_),
      observation_budget_ms_(observation_budget_ms) {
  if (observation_budget_ms == 0) {
    throw std::invalid_argument("task observation budget must be positive");
  }
}

FiniteComputeTask::FiniteComputeTask(TaskExecution& execution, MonotonicTime budget)
    : host_(execution), observation_budget_ms_(budget) {
  if (budget == 0)
    throw std::invalid_argument("task observation budget must be positive");
}

bool FiniteComputeTask::note_interrupted_goal(ComputeGoal goal) {
  if (closing_ || goal_ || operation_ || goal.revision == 0 || goal.first_iterations == 0 ||
      goal.first_iterations > 100000000)
    return false;
  goal_ = goal;
  outcome_ = TaskOutcome::kNeedsAttention;
  detail_ = "previous Host lost; prior goal outcome unavailable";
  return true;
}

bool FiniteComputeTask::revise_goal(ComputeGoal goal) {
  if (closing_ || outcome_ == TaskOutcome::kNeedsAttention || goal.revision == 0 ||
      goal.first_iterations == 0 || goal.first_iterations > 100000000 ||
      (goal_ && goal.revision <= goal_->revision)) {
    return false;
  }
  goal_ = goal;
  step_ = 1;
  first_sum_.reset();
  final_sum_.reset();
  // Keep one latest intent, but never restart an outstanding cleanup's observation budget.
  if (!operation_) {
    waiting_since_.reset();
  }
  outcome_ = TaskOutcome::kPending;
  detail_ = operation_ ? "waiting for previous goal cleanup" : "ready for first step";
  if (operation_) {
    if (!host_.request_cancel(operation_->authority)) {
      needs_attention("cancellation could not be correlated");
    }
  }
  return true;
}

void FiniteComputeTask::needs_attention(const char* detail) {
  outcome_ = TaskOutcome::kNeedsAttention;
  detail_ = detail;
  if (operation_) {
    host_.request_cancel(operation_->authority);
  }
}

bool FiniteComputeTask::observation_expired(MonotonicTime time) const noexcept {
  return waiting_since_ && time >= *waiting_since_ &&
         time - *waiting_since_ >= observation_budget_ms_;
}

void FiniteComputeTask::submit_step(MonotonicTime time) {
  const auto phase = host_.phase();
  if (phase == BindingPhase::kClosed) {
    outcome_ = TaskOutcome::kFailed;
    detail_ = "host is permanently closed";
    return;
  }
  if (phase != BindingPhase::kActive) {
    detail_ = "waiting for an explicitly rebound provider";
    if (observation_expired(time)) {
      needs_attention("provider handoff has not been confirmed");
    }
    return;
  }
  const auto iterations = step_ == 1 ? goal_->first_iterations : *first_sum_ % 8 + 1;
  const auto reference =
      "goal-" + std::to_string(goal_->revision) + "-step-" + std::to_string(step_);
  const auto submission = host_.submit(iterations, reference);
  if (submission.authority) {
    operation_ =
        TaskOperation{goal_->revision, step_, *submission.authority, reference, iterations};
    ++admitted_steps_;
    waiting_since_ = time;
  }
  if (submission.status != ComputeSubmissionStatus::kSubmitted) {
    if (submission.admission_status == AdmissionStatus::kRecoveryRequired ||
        submission.admission_status == AdmissionStatus::kDomainOccupied) {
      needs_attention("admission requires unresolved execution facts");
    } else {
      outcome_ = TaskOutcome::kFailed;
      detail_ = "step could not be submitted with the required capability";
    }
    return;
  }
  detail_ = "observing current step";
}

void FiniteComputeTask::tick(MonotonicTime caller_time) {
  host_.poll();
  if (closing_) {
    return;
  }
  if (last_tick_ && caller_time < *last_tick_) {
    needs_attention("caller observation clock moved backwards");
    return;
  }
  last_tick_ = caller_time;
  if (!goal_ || outcome_ != TaskOutcome::kPending) {
    return;
  }
  if (!waiting_since_) {
    waiting_since_ = caller_time;
  }
  if (!operation_) {
    submit_step(caller_time);
    return;
  }

  const auto receipt = host_.receipt(operation_->authority);
  if (!receipt || receipt->native_outcome == NativeOutcome::kUnknown ||
      receipt->authority_disposition == AuthorityDisposition::kBlockedUnknown ||
      host_.ownership_lost()) {
    needs_attention("operation outcome or ownership cannot be confirmed");
    return;
  }
  if (operation_->goal_revision != goal_->revision) {
    if (receipt->authority_disposition == AuthorityDisposition::kReleased) {
      operation_.reset();
      waiting_since_ = caller_time;
      detail_ = "previous goal settled; new goal may start";
    } else if (observation_expired(caller_time)) {
      needs_attention("previous goal cleanup has not been confirmed");
    }
    return;
  }

  if (receipt->native_outcome == NativeOutcome::kFailed ||
      receipt->native_outcome == NativeOutcome::kCancelled ||
      receipt->native_outcome == NativeOutcome::kNotExecuted ||
      receipt->cancellation_requested_at || receipt->expiry_observed_at ||
      receipt->binding_withdrawn_at) {
    outcome_ = TaskOutcome::kFailed;
    detail_ = "current goal interrupted or step failed; cleanup still driven";
    return;
  }
  if (receipt->authority_disposition != AuthorityDisposition::kReleased) {
    if (observation_expired(caller_time)) {
      needs_attention("step result and cleanup not confirmed within observation window");
    }
    return;
  }
  const auto& result = host_.result();
  if (receipt->native_outcome != NativeOutcome::kSucceeded ||
      receipt->output != OutputDisposition::kAccepted || !result ||
      result->operation_id != operation_->authority.operation_id ||
      result->request_reference != operation_->request_reference ||
      result->iterations != operation_->iterations) {
    needs_attention("accepted result cannot be correlated with this goal and step");
    return;
  }
  if (result->sum != reference_sum(operation_->iterations)) {
    outcome_ = TaskOutcome::kFailed;
    detail_ = "application arithmetic check failed";
    return;
  }
  if (step_ == 1) {
    first_sum_ = result->sum;
    step_ = 2;
    operation_.reset();
    waiting_since_ = caller_time;
    detail_ = "first result accepted and settled; second input selected";
  } else {
    final_sum_ = result->sum;
    outcome_ = TaskOutcome::kSucceeded;
    detail_ = "both accepted results verified and settled";
  }
}

void FiniteComputeTask::begin_shutdown() {
  if (closing_) {
    return;
  }
  closing_ = true;
  if (goal_ && outcome_ == TaskOutcome::kPending) {
    outcome_ = TaskOutcome::kCancelled;
    detail_ = "task cancelled by shutdown; native cleanup remains observable";
  }
  host_.begin_shutdown();
}

const std::optional<ComputeGoal>& FiniteComputeTask::goal() const noexcept {
  return goal_;
}
const std::optional<TaskOperation>& FiniteComputeTask::operation() const noexcept {
  return operation_;
}
TaskOutcome FiniteComputeTask::outcome() const noexcept {
  return outcome_;
}
const char* FiniteComputeTask::detail() const noexcept {
  return detail_;
}
unsigned FiniteComputeTask::step() const noexcept {
  return step_;
}
std::uint64_t FiniteComputeTask::admitted_steps() const noexcept {
  return admitted_steps_;
}
std::optional<std::uint64_t> FiniteComputeTask::first_sum() const noexcept {
  return first_sum_;
}
std::optional<std::uint64_t> FiniteComputeTask::final_sum() const noexcept {
  return final_sum_;
}

}  // namespace robot_harness::examples
