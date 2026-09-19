#include "recovery_task_execution.hpp"

#include <utility>

namespace robot_harness::examples {
using detail::recovery::ClientState;

void RecoveryTaskExecution::poll() try {
  client_.poll(ComputeExecutionHost::now());
  if (client_.state() == ClientState::kClosed) {
    result_.reset();
    return;
  }
  const auto value = client_.result();
  if (!pending_result_ || !value)
    return;
  const auto observed = client_.receipt();
  if (!observed || observed->authority.operation_id != pending_result_->operation_id ||
      observed->authority_disposition != AuthorityDisposition::kReleased ||
      observed->output != OutputDisposition::kAccepted ||
      observed->native_outcome != NativeOutcome::kSucceeded) {
    client_.close();
    return;
  }
  pending_result_->sum = *value;
  result_ = std::move(pending_result_);
  pending_result_.reset();
} catch (...) {
  client_.close();
  throw;
}

BindingPhase RecoveryTaskExecution::phase() const {
  if (client_.state() == ClientState::kClosed)
    return BindingPhase::kClosed;
  return client_.state() == ClientState::kReady ? BindingPhase::kActive : BindingPhase::kWithdrawn;
}

ComputeSubmission RecoveryTaskExecution::submit(std::uint64_t iterations,
                                                const std::string& reference) try {
  // Copy caller metadata before queueing any native submission.
  ComputeResult pending{0, reference, iterations, 0};
  if (!client_.submit(iterations, ComputeExecutionHost::now()))
    return {ComputeSubmissionStatus::kNotSubmitted, AdmissionStatus::kRecoveryRequired, {}};
  const auto observed = client_.receipt();
  if (!observed) {
    client_.close();
    return {ComputeSubmissionStatus::kNotSubmitted, AdmissionStatus::kClosed, {}};
  }
  pending.operation_id = observed->authority.operation_id;
  pending_result_ = std::move(pending);
  result_.reset();
  return {ComputeSubmissionStatus::kSubmitted, AdmissionStatus::kAdmitted, observed->authority};
} catch (...) {
  client_.close();
  throw;
}

std::optional<OperationReceipt>
RecoveryTaskExecution::receipt(const OperationAuthority& authority) const {
  auto observed = client_.receipt();
  if (!observed || observed->authority.operation_id != authority.operation_id ||
      !(observed->authority.binding == authority.binding) ||
      observed->authority.admitted_at != authority.admitted_at)
    return std::nullopt;
  return observed;
}

bool RecoveryTaskExecution::request_cancel(const OperationAuthority& authority) {
  if (receipt(authority))
    begin_shutdown();
  return false;  // Session fencing is not an operation cancellation acknowledgement.
}
bool RecoveryTaskExecution::ownership_lost() const {
  return client_.state() == ClientState::kClosed;
}
void RecoveryTaskExecution::begin_shutdown() {
  client_.close();
  result_.reset();
}
}  // namespace robot_harness::examples
