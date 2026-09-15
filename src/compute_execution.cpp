#include "robot_harness/compute_execution.hpp"

#include "compute_process.hpp"

#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

#include <sys/wait.h>

namespace robot_harness {
namespace {
BindingIdentity make_binding() {
  static std::atomic<std::uint64_t> next_generation{1};
  auto generation = next_generation.load(std::memory_order_relaxed);
  do {
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("compute instance identities exhausted");
    }
  } while (!next_generation.compare_exchange_weak(generation, generation + 1,
                                                  std::memory_order_relaxed));
  return {"local-compute-process", "local-compute-result-" + std::to_string(generation), 1,
          generation};
}
constexpr const char* kCapability = "compute.periodic-square-sum";
constexpr const char* kObserver = "compute-adapter";
constexpr const char* kSink = "compute-result-sink";
constexpr const char* kNative = "compute-process";
constexpr const char* kStop = "compute-control";
constexpr const char* kScope = "child-exit-output-and-channels";

AuthorityGateConfig make_gate_config(const BindingIdentity& binding, MonotonicTime time) {
  AuthorityGateConfig config;
  config.binding = binding;
  config.capability_id = kCapability;
  config.binding_observer = kObserver;
  config.worker_observer = kObserver;
  config.result_sink_observer = kSink;
  config.native_evidence_source = kNative;
  config.settlement_evidence_source = kObserver;
  config.settlement_scope = kScope;
  config.stop_evidence_source = kStop;
  config.started_at = time;
  config.readiness_kind = StartupEvidenceKind::kAdapterReady;
  config.execution_capability_observer = kObserver;
  return config;
}

bool matches(const OperationAuthority& left, const OperationAuthority& right) {
  return left.operation_id == right.operation_id && left.binding == right.binding &&
         left.admitted_at == right.admitted_at;
}

std::uint64_t expected_sum(std::uint64_t iterations) {
  const auto remainder = iterations % 1000;
  return (iterations / 1000) * 332833500 +
         (remainder == 0 ? 0 : (remainder - 1) * remainder * (2 * remainder - 1) / 6);
}
}  // namespace

struct ComputeExecutionHost::Implementation {
  explicit Implementation(ComputeHostConfig supplied)
      : config(std::move(supplied)), gate(make_gate_config(binding, ComputeExecutionHost::now())) {
    if (config.worker_executable.empty() || config.worker_executable.front() != '/' ||
        config.maximum_iterations == 0 || config.maximum_iterations > 100000000 ||
        config.stop_grace_ms > 60000) {
      throw std::invalid_argument("compute host requires absolute worker path and bounded policy");
    }
  }

  ComputeHostConfig config;
  const BindingIdentity binding = make_binding();
  AuthorityGate gate;
  std::unique_ptr<detail::ComputeProcess> process;
  std::optional<OperationAuthority> authority;
  std::optional<ComputeRequest> request;
  std::optional<ComputeResult> result;
  std::string native_identity;
  std::string result_reference;
  std::uint64_t sequence = 0;
  bool initialized = false;
  bool stop_reported = false;
  bool outcome_reported = false;
  bool evidence_error = false;
  ComputeShutdownStatus shutdown = ComputeShutdownStatus::kOpen;

  EvidenceRecord record(const char* source, MonotonicTime time) {
    return {source, time, ++sequence};
  }
  void accept(EvidenceDisposition disposition) {
    if (disposition == EvidenceDisposition::kRejected) {
      evidence_error = true;
    }
  }
  bool observe_capabilities(MonotonicTime time) {
    const bool available = detail::ComputeProcess::can_launch(config.worker_executable);
    const auto expiry = time > std::numeric_limits<MonotonicTime>::max() - 1000
                            ? std::numeric_limits<MonotonicTime>::max()
                            : time + 1000;
    accept(gate.observe_execution_capabilities({binding, kCapability, kLocalComputeProfile,
                                                config.maximum_iterations, available,
                                                record(kObserver, time), expiry}));
    return available;
  }
  void native(NativeEventKind kind, MonotonicTime time) {
    accept(gate.observe_native({*authority, native_identity, kind, record(kNative, time)}));
  }
  void apply_stop(const ControlDecision& decision, MonotonicTime time) {
    if (decision.should_request_native_stop && process && process->observation().launched) {
      process->request_stop(time);
    }
  }
  void expire(MonotonicTime time) {
    if (authority) {
      apply_stop(gate.observe_time(*authority, time), time);
    }
  }
  void settle_unsubmitted(bool cleanup_confirmed = true) {
    const auto time = ComputeExecutionHost::now();
    accept(gate.observe_non_submission({*authority, record(kObserver, time)}));
    accept(gate.observe_output_not_delivered(
        {*authority, OutputNonDeliveryReason::kNoOutputProduced, "", record(kObserver, time)}));
    if (cleanup_confirmed) {
      accept(gate.observe_settlement({*authority, kScope, true, record(kObserver, time)}));
    } else {
      evidence_error = true;
    }
  }

  void observe_process_events() {
    const auto& observed = process->observation();
    const auto receipt = gate.receipt(*authority);
    if (observed.started && receipt && !receipt->native_started) {
      native(NativeEventKind::kStarted, ComputeExecutionHost::now());
    }
    if (!stop_reported && observed.stop_requested &&
        (!observed.stop_delivered || observed.stop_acknowledged || observed.stop_ignored)) {
      const auto acknowledgement = !observed.stop_delivered ? StopAcknowledgement::kUnavailable
                                   : observed.stop_ignored  ? StopAcknowledgement::kRefused
                                                            : StopAcknowledgement::kAcknowledged;
      accept(gate.observe_stop_acknowledgement(
          {*authority, acknowledgement, record(kStop, ComputeExecutionHost::now())}));
      stop_reported = true;
    }
  }

  void update_shutdown_status() {
    if (shutdown == ComputeShutdownStatus::kOpen) {
      return;
    }
    const auto receipt = authority ? gate.receipt(*authority) : std::nullopt;
    const bool settled =
        !authority || (receipt && receipt->settlement == SettlementStatus::kSettled);
    const bool owned_clear =
        !process || !process->observation().launched || process->observation().channels_closed;
    if (evidence_error ||
        (process && (process->observation().ownership_lost ||
                     (!owned_clear && (process->observation().io_error ||
                                       process->observation().termination_error))))) {
      shutdown = ComputeShutdownStatus::kUnresolved;
    } else if (settled && owned_clear) {
      shutdown = ComputeShutdownStatus::kClosed;
    }
  }

  void finish() {
    const auto& observed = process->observation();
    if (!observed.exit_status || !observed.event_eof || observed.ownership_lost) {
      return;
    }
    if (!outcome_reported) {
      const auto time = ComputeExecutionHost::now();
      expire(time);
      const int status = *observed.exit_status;
      const bool valid = !observed.malformed && observed.io_error == 0;
      const bool succeeded =
          valid && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
          observed.terminal == detail::ProcessObservation::Terminal::kCompleted &&
          observed.terminal_iterations == request->iterations &&
          observed.terminal_value == expected_sum(request->iterations);
      const bool cancelled =
          valid && WIFEXITED(status) && WEXITSTATUS(status) == 20 && observed.stop_requested &&
          observed.terminal == detail::ProcessObservation::Terminal::kCancelled &&
          observed.terminal_iterations < request->iterations;
      native(succeeded   ? NativeEventKind::kTerminalSucceeded
             : cancelled ? NativeEventKind::kTerminalCancelled
                         : NativeEventKind::kTerminalFailed,
             time);
      if (succeeded) {
        // Prepare result storage before the actual sink authority/time check.
        ComputeResult pending{authority->operation_id, request->request_reference,
                              request->iterations, observed.terminal_value};
        const auto delivery_time = ComputeExecutionHost::now();
        expire(delivery_time);
        if (!evidence_error && gate.can_deliver_result(*authority)) {
          result = std::move(pending);
          accept(gate.observe_output_accepted(
              {*authority, result_reference, record(kSink, delivery_time)}));
        } else {
          accept(gate.observe_output_not_delivered(
              {*authority, OutputNonDeliveryReason::kAuthorityRevoked, result_reference,
               record(kSink, delivery_time)}));
        }
      } else {
        accept(gate.observe_output_not_delivered(
            {*authority, OutputNonDeliveryReason::kNoOutputProduced, "", record(kObserver, time)}));
      }
      outcome_reported = true;
    }
    // Native ownership must be released even when receipt mapping is unresolved.
    // Only the authority-reopening evidence depends on successful Core mapping.
    if (process->close_channels()) {
      const auto receipt = gate.receipt(*authority);
      if (!evidence_error && receipt && receipt->settlement == SettlementStatus::kPending) {
        accept(gate.observe_settlement(
            {*authority, kScope, true, record(kObserver, ComputeExecutionHost::now())}));
      }
    }
  }
};

MonotonicTime ComputeExecutionHost::now() noexcept {
  return static_cast<MonotonicTime>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

ComputeExecutionHost::ComputeExecutionHost(ComputeHostConfig config)
    : implementation_(std::make_unique<Implementation>(std::move(config))) {
}
ComputeExecutionHost::~ComputeExecutionHost() = default;

StartupStatus ComputeExecutionHost::initialize() {
  auto& state = *implementation_;
  if (!state.initialized && state.shutdown == ComputeShutdownStatus::kOpen) {
    const auto time = now();
    const bool ready = state.observe_capabilities(time);
    state.accept(state.gate.observe_startup({StartupEvidenceKind::kBindingIdleAndSettled,
                                             state.binding, state.record(kObserver, time), true}));
    state.accept(state.gate.observe_startup(
        {StartupEvidenceKind::kAdapterReady, state.binding, state.record(kObserver, time), ready}));
    state.accept(state.gate.observe_startup(
        {StartupEvidenceKind::kResultSinkReady, state.binding, state.record(kSink, time), true}));
    state.initialized = ready && !state.evidence_error;
  }
  return state.gate.startup_status();
}

ComputeSubmission ComputeExecutionHost::prepare(const ComputeRequest& request) {
  auto& state = *implementation_;
  ComputeSubmission submission;
  if (request.request_reference.empty() || request.request_reference.size() > 128 ||
      request.iterations == 0 || request.iterations > 100000000 ||
      request.required_profile_id.empty() || request.required_profile_id.size() > 128) {
    return submission;
  }
  submission.status = ComputeSubmissionStatus::kAdmissionBlocked;
  if (state.evidence_error) {
    submission.admission_status = AdmissionStatus::kRecoveryRequired;
    return submission;
  }
  if (state.shutdown != ComputeShutdownStatus::kOpen) {
    submission.admission_status = AdmissionStatus::kClosed;
    return submission;
  }
  state.observe_capabilities(now());
  OperationRequest operation;
  operation.capability_id = kCapability;
  operation.effect_domain = state.binding.effect_domain;
  operation.opaque_request_reference = request.request_reference;
  operation.target_reference = kSink;
  operation.requested_at = now();
  operation.deadline = request.deadline;
  operation.execution_requirements =
      ExecutionRequirements{request.required_profile_id, request.iterations};
  const auto admission = state.gate.admit(operation);
  submission.admission_status = admission.status;
  submission.authority = admission.authority;
  if (admission.authority) {
    state.process.reset();
    state.result.reset();
    state.authority = admission.authority;
    state.request = request;
    state.native_identity = "compute-child-" + std::to_string(admission.authority->operation_id);
    state.result_reference = "compute-result-" + std::to_string(admission.authority->operation_id);
    state.stop_reported = false;
    state.outcome_reported = false;
    submission.status = ComputeSubmissionStatus::kPrepared;
  }
  return submission;
}

bool ComputeExecutionHost::dispatch_prepared(const OperationAuthority& authority) {
  auto& state = *implementation_;
  if (!state.authority || !matches(*state.authority, authority) || !state.request ||
      state.process) {
    return false;
  }
  const auto receipt = state.gate.receipt(authority);
  if (!receipt || receipt->dispatch != DispatchStatus::kPending) {
    return false;
  }
  state.observe_capabilities(now());
  auto process = std::make_unique<detail::ComputeProcess>(
      state.config.worker_executable, state.request->iterations, state.config.stop_grace_ms);
  const auto time = now();
  state.expire(time);
  if (state.evidence_error || !state.gate.claim_dispatch(authority, time)) {
    state.settle_unsubmitted();
    return false;
  }
  state.process = std::move(process);
  if (!state.process->launch()) {
    state.settle_unsubmitted(state.process->observation().io_error == 0);
    return false;
  }
  state.native(NativeEventKind::kAccepted, now());
  return true;
}

ComputeSubmission ComputeExecutionHost::submit(const ComputeRequest& request) {
  auto submission = prepare(request);
  if (submission.authority) {
    submission.status = dispatch_prepared(*submission.authority)
                            ? ComputeSubmissionStatus::kSubmitted
                            : ComputeSubmissionStatus::kNotSubmitted;
  }
  return submission;
}

ControlDecision ComputeExecutionHost::request_cancel(const OperationAuthority& authority) {
  auto& state = *implementation_;
  const auto time = now();
  const auto decision = state.gate.request_cancel(authority, time);
  state.apply_stop(decision, time);
  const auto receipt = state.gate.receipt(authority);
  if (decision.status == ControlStatus::kApplied && receipt &&
      receipt->dispatch == DispatchStatus::kPending) {
    state.settle_unsubmitted();
  }
  return decision;
}

void ComputeExecutionHost::poll() {
  auto& state = *implementation_;
  if (state.shutdown == ComputeShutdownStatus::kClosed) {
    return;
  }
  const auto time = now();
  state.expire(time);
  if (state.authority && !state.process) {
    const auto receipt = state.gate.receipt(*state.authority);
    if (receipt && receipt->dispatch == DispatchStatus::kPending &&
        receipt->authority_disposition == AuthorityDisposition::kRevoked) {
      state.settle_unsubmitted();
    }
  }
  if (state.process && state.process->observation().launched &&
      !state.process->observation().channels_closed) {
    if (state.shutdown == ComputeShutdownStatus::kOpen) {
      state.observe_capabilities(now());
      if (!state.gate.supports_execution(
              {state.request->required_profile_id, state.request->iterations}, now())) {
        request_cancel(*state.authority);
      }
    }
    state.process->poll(now());
    const auto& observed = state.process->observation();
    if (observed.malformed || observed.io_error || observed.ownership_lost) {
      request_cancel(*state.authority);
    }
    state.observe_process_events();
    state.finish();
  }
  state.update_shutdown_status();
}

void ComputeExecutionHost::begin_shutdown() {
  auto& state = *implementation_;
  if (state.shutdown != ComputeShutdownStatus::kOpen) {
    return;
  }
  state.shutdown = ComputeShutdownStatus::kClosing;
  state.gate.close_admission();
  if (state.authority) {
    request_cancel(*state.authority);
  }
  poll();
}
ComputeShutdownStatus ComputeExecutionHost::shutdown_status() const noexcept {
  return implementation_->shutdown;
}
ComputeObservation ComputeExecutionHost::observation() const {
  ComputeObservation result;
  if (!implementation_->process) {
    return result;
  }
  const auto& source = implementation_->process->observation();
  result.launched = source.launched;
  result.started = source.started;
  result.completed_iterations = source.progress;
  result.dropped_progress = source.dropped_progress;
  result.stop_sent = source.stop_delivered;
  result.stop_send_error = source.stop_error;
  result.stop_requested_at = source.stop_requested_at;
  result.termination_requested = source.termination_requested;
  result.protocol_error = source.malformed;
  result.ownership_lost = source.ownership_lost;
  result.launch_error = source.launch_error;
  result.system_error = source.io_error ? source.io_error : source.termination_error;
  result.child_process_id = source.child_pid;
  result.cleanup_confirmed = source.channels_closed;
  if (source.exit_status) {
    if (WIFEXITED(*source.exit_status))
      result.exit_code = WEXITSTATUS(*source.exit_status);
    if (WIFSIGNALED(*source.exit_status))
      result.terminating_signal = WTERMSIG(*source.exit_status);
  }
  return result;
}
std::optional<OperationReceipt>
ComputeExecutionHost::receipt(const OperationAuthority& authority) const {
  return implementation_->gate.receipt(authority);
}
const std::optional<ComputeResult>& ComputeExecutionHost::result() const noexcept {
  return implementation_->result;
}
}  // namespace robot_harness
