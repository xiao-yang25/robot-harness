#include "robot_harness/sample_execution.hpp"

#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>

namespace robot_harness {
namespace {

constexpr std::size_t kMaximumSampleValues = 16;
constexpr std::int32_t kMaximumAbsoluteSampleValue = 10000;

const BindingIdentity kSampleBinding{"deterministic-sample-worker", "managed-sample-result", 1, 1};
const char* const kCapabilityId = "sample.square";
const char* const kBindingObserver = "deterministic-sample-adapter";
const char* const kWorkerObserver = "deterministic-sample-worker";
const char* const kResultSinkObserver = "managed-sample-result-sink";
const char* const kNativeEvidenceSource = "deterministic-sample-worker";
const char* const kSettlementEvidenceSource = "deterministic-sample-adapter";
const char* const kTargetReference = "managed-sample-result-sink";
const char* const kSettlementScope = "worker-callbacks-and-managed-result";

bool is_valid_request(const SampleRequest& request) {
  if (request.request_reference.empty() || request.values.empty() ||
      request.values.size() > kMaximumSampleValues) {
    return false;
  }
  for (const auto value : request.values) {
    if (value < -kMaximumAbsoluteSampleValue || value > kMaximumAbsoluteSampleValue) {
      return false;
    }
  }
  return true;
}

enum class CallbackKind {
  kNotSubmitted,
  kAccepted,
  kRejected,
  kStarted,
  kTerminalSucceeded,
  kTerminalFailed,
  kTerminalCancelled,
  kNoOutputProduced,
  kResult,
  kSettled
};

enum class ResultAcceptance { kAccepted, kSinkUnavailable, kInvalid, kPermissionDenied };

struct NativeCallback {
  CallbackKind kind = CallbackKind::kAccepted;
  OperationAuthority authority;
  std::string native_identity;
  std::uint64_t sequence = 0;
  std::optional<SampleResult> result;
};

struct StagedEvent {
  NativeCallback callback;
  MonotonicTime observed_at = 0;
};

class DeterministicSampleAdapter {
public:
  using Callback = std::function<void(NativeCallback)>;
  using DispatchPermission = std::function<bool()>;
  using ResultPermission = std::function<bool()>;

  explicit DeterministicSampleAdapter(CompletionMode completion_mode)
      : completion_mode_(completion_mode) {
  }

  void set_callback(Callback callback) {
    callback_ = std::move(callback);
  }

  bool is_idle_and_settled() const noexcept {
    return !pending_work_.has_value();
  }
  bool is_worker_ready() const noexcept {
    return worker_ready_ && !is_closed_;
  }
  bool is_result_sink_ready() const noexcept {
    return result_sink_ready_ && !is_closed_;
  }

  void set_worker_ready(bool is_ready) noexcept {
    worker_ready_ = is_ready;
  }
  void set_result_sink_ready(bool is_ready) noexcept {
    result_sink_ready_ = is_ready;
  }

  void reject_next_native_submission() noexcept {
    reject_next_native_submission_ = true;
  }
  void fail_next_native_execution() noexcept {
    fail_next_native_execution_ = true;
  }

  bool dispatch(const OperationAuthority& authority, const SampleRequest& request,
                DispatchPermission permission) {
    WorkItem work;
    work.authority = authority;
    work.request = request;
    work.native_identity = "native-sample-" + std::to_string(authority.operation_id);

    if (!is_worker_ready() || !is_result_sink_ready() || pending_work_.has_value() ||
        !permission()) {
      emit(work, CallbackKind::kNotSubmitted, 1);
      emit(work, CallbackKind::kNoOutputProduced, 2);
      emit(work, CallbackKind::kSettled, 3);
      return false;
    }

    ++submission_count_;
    if (reject_next_native_submission_) {
      reject_next_native_submission_ = false;
      emit(work, CallbackKind::kRejected, 1);
      emit(work, CallbackKind::kNoOutputProduced, 1);
      emit(work, CallbackKind::kSettled, 2);
      return true;
    }

    emit(work, CallbackKind::kAccepted, 1);
    if (completion_mode_ == CompletionMode::kSynchronous) {
      finish(work);
    } else {
      pending_work_ = std::move(work);
    }
    return true;
  }

  void set_cancellation_mode(CancellationMode mode) noexcept {
    cancellation_mode_ = mode;
  }
  void set_settlement_reporting_enabled(bool enabled) noexcept {
    settlement_reporting_enabled_ = enabled;
  }
  void set_result_replay_enabled(bool enabled) noexcept {
    result_replay_enabled_ = enabled;
    replay_result_.reset();
  }
  std::optional<NativeCallback> retained_result() const {
    return is_closed_ ? std::nullopt : replay_result_;
  }
  std::size_t result_permission_denial_count() const noexcept {
    return result_permission_denial_count_;
  }
  std::size_t stop_request_count() const noexcept {
    return stop_request_count_;
  }

  std::optional<StopAcknowledgement> request_stop(const OperationAuthority& authority) {
    ++stop_request_count_;
    if (!pending_work_ || pending_work_->authority.operation_id != authority.operation_id ||
        !(pending_work_->authority.binding == authority.binding) ||
        pending_work_->authority.admitted_at != authority.admitted_at) {
      return StopAcknowledgement::kUnavailable;
    }
    switch (cancellation_mode_) {
    case CancellationMode::kCooperative:
      pending_work_->is_cancelled = true;
      return StopAcknowledgement::kAcknowledged;
    case CancellationMode::kIgnore:
      return StopAcknowledgement::kAcknowledged;
    case CancellationMode::kRefuse:
      return StopAcknowledgement::kRefused;
    case CancellationMode::kUnavailable:
      return StopAcknowledgement::kUnavailable;
    case CancellationMode::kNoAcknowledgement:
      return std::nullopt;
    }
    return std::nullopt;
  }

  void confirm_non_submission(const OperationAuthority& authority) {
    WorkItem work;
    work.authority = authority;
    emit(work, CallbackKind::kNotSubmitted, 1);
    emit(work, CallbackKind::kNoOutputProduced, 2);
    emit(work, CallbackKind::kSettled, 3);
  }

  bool complete_deferred() {
    if (!pending_work_.has_value() || is_closed_) {
      return false;
    }
    WorkItem work = std::move(*pending_work_);
    pending_work_.reset();
    finish(work);
    return true;
  }

  ResultAcceptance accept_result(const OperationAuthority& authority, SampleResult result,
                                 ResultPermission permission) {
    if (result.operation_id != authority.operation_id) {
      return ResultAcceptance::kInvalid;
    }
    if (!permission()) {
      ++result_permission_denial_count_;
      return ResultAcceptance::kPermissionDenied;
    }
    if (!is_result_sink_ready()) {
      return ResultAcceptance::kSinkUnavailable;
    }
    results_.push_back(std::move(result));
    return ResultAcceptance::kAccepted;
  }

  void drain_pending() {
    if (pending_work_.has_value()) {
      WorkItem work = std::move(*pending_work_);
      pending_work_.reset();
      finish(work);
    }
  }

  void close() {
    if (is_closed_) {
      return;
    }
    worker_ready_ = false;
    result_sink_ready_ = false;
    replay_result_.reset();
    result_replay_enabled_ = false;
    is_closed_ = true;
  }

  void inherit_observations(DeterministicSampleAdapter& old) noexcept {
    results_ = std::move(old.results_);
    replay_result_ = std::move(old.replay_result_);
    result_replay_enabled_ = old.result_replay_enabled_;
    result_permission_denial_count_ = old.result_permission_denial_count_;
    stop_request_count_ = old.stop_request_count_;
    submission_count_ = old.submission_count_;
    execution_count_ = old.execution_count_;
  }

  void clear_callback() {
    callback_ = nullptr;
  }
  const std::vector<SampleResult>& results() const noexcept {
    return results_;
  }
  std::size_t submission_count() const noexcept {
    return submission_count_;
  }
  std::size_t execution_count() const noexcept {
    return execution_count_;
  }
  bool has_pending_work() const noexcept {
    return pending_work_.has_value();
  }
  bool is_deferred() const noexcept {
    return completion_mode_ == CompletionMode::kDeferred;
  }

private:
  struct WorkItem {
    OperationAuthority authority;
    SampleRequest request;
    std::string native_identity;
    bool is_cancelled = false;
  };

  void emit(const WorkItem& work, CallbackKind kind, std::uint64_t sequence,
            std::optional<SampleResult> result = std::nullopt) {
    if (kind == CallbackKind::kSettled && !settlement_reporting_enabled_) {
      return;
    }
    if (callback_) {
      NativeCallback callback{kind, work.authority, work.native_identity, sequence,
                              std::move(result)};
      if (kind == CallbackKind::kResult && result_replay_enabled_ && !replay_result_) {
        replay_result_ = callback;
      }
      callback_(std::move(callback));
    }
  }

  void finish(const WorkItem& work) {
    if (work.is_cancelled) {
      emit(work, CallbackKind::kTerminalCancelled, 2);
      emit(work, CallbackKind::kNoOutputProduced, 1);
      emit(work, CallbackKind::kSettled, 2);
      return;
    }
    ++execution_count_;
    emit(work, CallbackKind::kStarted, 2);
    if (fail_next_native_execution_) {
      fail_next_native_execution_ = false;
      emit(work, CallbackKind::kTerminalFailed, 3);
      emit(work, CallbackKind::kNoOutputProduced, 1);
      emit(work, CallbackKind::kSettled, 2);
      return;
    }

    SampleResult result;
    result.operation_id = work.authority.operation_id;
    result.request_reference = work.request.request_reference;
    result.squared_values.reserve(work.request.values.size());
    for (const auto value : work.request.values) {
      const auto wide_value = static_cast<std::int64_t>(value);
      const auto square = wide_value * wide_value;
      result.squared_values.push_back(square);
      result.sum_of_squares += square;
    }
    emit(work, CallbackKind::kTerminalSucceeded, 3);
    emit(work, CallbackKind::kResult, 4, std::move(result));
    emit(work, CallbackKind::kSettled, 5);
  }

  CompletionMode completion_mode_;
  Callback callback_;
  bool worker_ready_ = true;
  bool result_sink_ready_ = true;
  bool is_closed_ = false;
  bool reject_next_native_submission_ = false;
  bool fail_next_native_execution_ = false;
  CancellationMode cancellation_mode_ = CancellationMode::kCooperative;
  bool settlement_reporting_enabled_ = true;
  bool result_replay_enabled_ = false;
  std::optional<NativeCallback> replay_result_;
  std::size_t result_permission_denial_count_ = 0;
  std::size_t stop_request_count_ = 0;
  std::optional<WorkItem> pending_work_;
  std::vector<SampleResult> results_;
  std::size_t submission_count_ = 0;
  std::size_t execution_count_ = 0;
};

}  // namespace

struct SampleExecutionHost::Implementation {
  Implementation(CompletionMode completion_mode, Clock supplied_clock)
      : clock(std::move(supplied_clock)),
        adapter(std::make_unique<DeterministicSampleAdapter>(completion_mode)) {
    if (!clock) {
      throw std::invalid_argument("sample execution host requires a monotonic clock");
    }
    started_at = read_time();
    AuthorityGateConfig config;
    config.binding = kSampleBinding;
    config.capability_id = kCapabilityId;
    config.binding_observer = kBindingObserver;
    config.worker_observer = kWorkerObserver;
    config.result_sink_observer = kResultSinkObserver;
    config.native_evidence_source = kNativeEvidenceSource;
    config.settlement_evidence_source = kSettlementEvidenceSource;
    config.settlement_scope = kSettlementScope;
    config.started_at = started_at;
    gate = std::make_unique<AuthorityGate>(std::move(config));
    adapter->set_callback([this](NativeCallback callback) {
      staged_events.push_back({std::move(callback), read_time()});
    });
  }

  ~Implementation() {
    try {
      shutdown();
    } catch (...) {
      adapter->clear_callback();
      adapter->close();
    }
  }

  MonotonicTime read_time() {
    const auto current = clock();
    if (has_read_time && current < last_time) {
      throw std::logic_error("sample execution host clock moved backwards");
    }
    has_read_time = true;
    last_time = current;
    return current;
  }

  StartupEvidence startup_evidence(StartupEvidenceKind kind, const char* source, bool observed,
                                   std::uint64_t sequence) {
    return {
        kind, gate->binding_status(last_time).active, {source, read_time(), sequence}, observed};
  }

  void discard_prepared() {
    if (!prepared_authority) {
      return;
    }
    const auto authority = *prepared_authority;
    prepared_request.reset();
    prepared_authority.reset();
    adapter->confirm_non_submission(authority);
  }

  void apply_control(const OperationAuthority& authority, const ControlDecision& decision) {
    if (decision.status == ControlStatus::kApplied) {
      if (prepared_authority && prepared_authority->operation_id == authority.operation_id) {
        discard_prepared();
      }
      if (decision.should_request_native_stop) {
        const auto acknowledgement = adapter->request_stop(authority);
        if (acknowledgement) {
          gate->observe_stop_acknowledgement(
              {authority, *acknowledgement, {"native-stop", read_time(), 1}});
        }
      }
    }
  }

  ControlDecision observe_time(MonotonicTime now) {
    if (!current_authority) {
      return {ControlStatus::kNoActiveOperation, false};
    }
    const auto decision = gate->observe_time(*current_authority, now);
    apply_control(*current_authority, decision);
    return decision;
  }

  BindingDecision withdraw_binding(const BindingIdentity& expected_binding) {
    const auto decision = gate->withdraw_binding(expected_binding, read_time());
    if (decision.status == BindingControlStatus::kApplied) {
      discard_prepared();
      if (decision.native_stop_authority) {
        apply_control(*decision.native_stop_authority, {ControlStatus::kApplied, true});
      }
    }
    return decision;
  }

  ControlDecision poll() {
    const auto now = read_time();
    if (gate->startup_status().state == InitializationState::kReady &&
        (!adapter->is_worker_ready() || !adapter->is_result_sink_ready())) {
      withdraw_binding(gate->binding_status(now).active);
    }
    return observe_time(last_time);
  }

  BindingDecision rebind_provider(const BindingIdentity& expected_old_binding,
                                  const std::string& provider_id, CompletionMode completion_mode) {
    poll();
    const auto now = read_time();
    if (is_shutdown) {
      return {BindingControlStatus::kClosed, {}, {}};
    }
    if (!adapter->is_idle_and_settled() || prepared_authority || !staged_events.empty()) {
      return {BindingControlStatus::kUnresolvedWork, {}, {}};
    }
    if (now == std::numeric_limits<MonotonicTime>::max()) {
      return {BindingControlStatus::kInvalidTime, {}, {}};
    }
    // All allocation and callback setup precede the Core commit. The fixture's
    // observations are valid for this one serialized handoff only.
    auto replacement = std::make_unique<DeterministicSampleAdapter>(completion_mode);
    replacement->set_callback([this](NativeCallback callback) {
      staged_events.push_back({std::move(callback), read_time()});
    });
    const auto begun = gate->begin_rebind(expected_old_binding, provider_id,
                                          {StartupEvidenceKind::kBindingIdleAndSettled,
                                           expected_old_binding,
                                           {kBindingObserver, now, ++binding_observation_sequence},
                                           true,
                                           now + 1},
                                          now);
    if (begun.status != BindingControlStatus::kApplied || !begun.candidate) {
      return begun;
    }
    try {
      const auto& candidate = *begun.candidate;
      gate->observe_rebind_readiness({StartupEvidenceKind::kBindingIdleAndSettled,
                                      candidate,
                                      {kBindingObserver, now, ++binding_observation_sequence},
                                      replacement->is_idle_and_settled(),
                                      now + 1});
      gate->observe_rebind_readiness({StartupEvidenceKind::kWorkerReady,
                                      candidate,
                                      {kWorkerObserver, now, ++worker_observation_sequence},
                                      replacement->is_worker_ready(),
                                      now + 1});
      gate->observe_rebind_readiness({StartupEvidenceKind::kResultSinkReady,
                                      candidate,
                                      {kResultSinkObserver, now, ++sink_observation_sequence},
                                      replacement->is_result_sink_ready(),
                                      now + 1});
      const auto committed = gate->commit_rebind(candidate, now);
      if (committed.status != BindingControlStatus::kApplied) {
        gate->withdraw_binding(candidate, now);
        return committed;
      }
      replacement->inherit_observations(*adapter);
      adapter->clear_callback();
      adapter->close();
      adapter.swap(replacement);
      return committed;
    } catch (...) {
      gate->withdraw_binding(*begun.candidate, now);
      throw;
    }
  }

  ControlDecision request_cancel(const OperationAuthority& authority) {
    poll();
    const auto decision = gate->request_cancel(authority, read_time());
    apply_control(authority, decision);
    return decision;
  }

  bool process_next() {
    poll();
    if (staged_events.empty()) {
      return false;
    }
    StagedEvent event = std::move(staged_events.front());
    staged_events.pop_front();
    auto& callback = event.callback;
    switch (callback.kind) {
    case CallbackKind::kNotSubmitted:
      gate->observe_non_submission(
          {callback.authority, {kSettlementEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kAccepted:
      gate->observe_native({callback.authority,
                            callback.native_identity,
                            NativeEventKind::kAccepted,
                            {kNativeEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kRejected:
      gate->observe_native({callback.authority,
                            callback.native_identity,
                            NativeEventKind::kRejected,
                            {kNativeEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kStarted:
      gate->observe_native({callback.authority,
                            callback.native_identity,
                            NativeEventKind::kStarted,
                            {kNativeEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kTerminalSucceeded:
      gate->observe_native({callback.authority,
                            callback.native_identity,
                            NativeEventKind::kTerminalSucceeded,
                            {kNativeEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kTerminalFailed:
      gate->observe_native({callback.authority,
                            callback.native_identity,
                            NativeEventKind::kTerminalFailed,
                            {kNativeEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kTerminalCancelled:
      gate->observe_native({callback.authority,
                            callback.native_identity,
                            NativeEventKind::kTerminalCancelled,
                            {kNativeEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kNoOutputProduced:
      gate->observe_output_not_delivered(
          {callback.authority,
           OutputNonDeliveryReason::kNoOutputProduced,
           {},
           {kSettlementEvidenceSource, event.observed_at, callback.sequence}});
      break;
    case CallbackKind::kResult: {
      if (!callback.result.has_value()) {
        break;
      }
      const std::string result_reference =
          "managed-result-" + std::to_string(callback.authority.operation_id);
      const auto acceptance_time = read_time();
      observe_time(acceptance_time);
      const auto acceptance = adapter->accept_result(
          callback.authority, std::move(*callback.result),
          [this, &callback] { return gate->can_deliver_result(callback.authority); });
      callback.result.reset();
      if (acceptance == ResultAcceptance::kAccepted) {
        gate->observe_output_accepted(
            {callback.authority, result_reference, {kResultSinkObserver, acceptance_time, 1}});
      } else if (acceptance == ResultAcceptance::kSinkUnavailable ||
                 acceptance == ResultAcceptance::kPermissionDenied) {
        gate->observe_output_not_delivered({callback.authority,
                                            acceptance == ResultAcceptance::kPermissionDenied
                                                ? OutputNonDeliveryReason::kAuthorityRevoked
                                                : OutputNonDeliveryReason::kSinkRejected,
                                            result_reference,
                                            {kResultSinkObserver, acceptance_time, 1}});
      }
      break;
    }
    case CallbackKind::kSettled: {
      const auto settlement_time = read_time();
      observe_time(settlement_time);
      gate->observe_settlement({callback.authority,
                                kSettlementScope,
                                true,
                                {kSettlementEvidenceSource, settlement_time, callback.sequence}});
      break;
    }
    }
    return true;
  }

  void process_all() {
    while (process_next()) {
    }
  }

  void shutdown() {
    if (is_shutdown) {
      return;
    }
    poll();
    gate->close_admission();
    discard_prepared();
    adapter->drain_pending();
    process_all();
    adapter->close();
    adapter->clear_callback();
    is_shutdown = true;
  }

  Clock clock;
  MonotonicTime started_at = 0;
  MonotonicTime last_time = 0;
  bool has_read_time = false;
  std::unique_ptr<AuthorityGate> gate;
  std::unique_ptr<DeterministicSampleAdapter> adapter;
  std::deque<StagedEvent> staged_events;
  std::optional<OperationAuthority> current_authority;
  std::optional<OperationAuthority> prepared_authority;
  std::optional<SampleRequest> prepared_request;
  std::uint64_t binding_observation_sequence = 0;
  std::uint64_t worker_observation_sequence = 0;
  std::uint64_t sink_observation_sequence = 0;
  bool is_shutdown = false;
};

SampleExecutionHost::SampleExecutionHost(CompletionMode completion_mode, Clock clock)
    : implementation_(std::make_unique<Implementation>(completion_mode, std::move(clock))) {
}

SampleExecutionHost::~SampleExecutionHost() = default;

StartupStatus SampleExecutionHost::initialize() {
  auto& state = *implementation_;
  if (state.gate->startup_status().state == InitializationState::kReady &&
      (!state.adapter->is_worker_ready() || !state.adapter->is_result_sink_ready())) {
    state.poll();
  }
  state.gate->observe_startup(state.startup_evidence(
      StartupEvidenceKind::kBindingIdleAndSettled, kBindingObserver,
      state.adapter->is_idle_and_settled(), ++state.binding_observation_sequence));
  state.gate->observe_startup(state.startup_evidence(
      StartupEvidenceKind::kWorkerReady, kWorkerObserver, state.adapter->is_worker_ready(),
      ++state.worker_observation_sequence));
  state.gate->observe_startup(state.startup_evidence(
      StartupEvidenceKind::kResultSinkReady, kResultSinkObserver,
      state.adapter->is_result_sink_ready(), ++state.sink_observation_sequence));
  return state.gate->startup_status();
}

BindingStatus SampleExecutionHost::binding_status() const {
  auto& state = *implementation_;
  return state.gate->binding_status(state.read_time());
}

BindingDecision SampleExecutionHost::withdraw_binding(const BindingIdentity& expected_binding) {
  return implementation_->withdraw_binding(expected_binding);
}

BindingDecision SampleExecutionHost::rebind_provider(const BindingIdentity& expected_old_binding,
                                                     const std::string& provider_id,
                                                     CompletionMode completion_mode) {
  return implementation_->rebind_provider(expected_old_binding, provider_id, completion_mode);
}

void SampleExecutionHost::set_worker_ready(bool is_ready) {
  implementation_->adapter->set_worker_ready(is_ready);
}

void SampleExecutionHost::set_result_sink_ready(bool is_ready) {
  implementation_->adapter->set_result_sink_ready(is_ready);
}

void SampleExecutionHost::reject_next_native_submission() noexcept {
  implementation_->adapter->reject_next_native_submission();
}

void SampleExecutionHost::fail_next_native_execution() noexcept {
  implementation_->adapter->fail_next_native_execution();
}

SampleSubmission SampleExecutionHost::prepare(const SampleRequest& request) {
  auto& state = *implementation_;
  state.poll();
  SampleSubmission submission;
  if (!is_valid_request(request)) {
    return submission;
  }

  OperationRequest operation_request;
  operation_request.capability_id = kCapabilityId;
  operation_request.effect_domain = kSampleBinding.effect_domain;
  operation_request.opaque_request_reference = request.request_reference;
  operation_request.target_reference = kTargetReference;
  operation_request.requested_at = state.read_time();
  state.observe_time(operation_request.requested_at);
  operation_request.deadline = request.deadline;
  const auto admission = state.gate->admit(operation_request);
  submission.admission_status = admission.status;
  submission.authority = admission.authority;
  if (admission.status != AdmissionStatus::kAdmitted || !admission.authority.has_value()) {
    submission.status = SampleSubmissionStatus::kAdmissionBlocked;
    return submission;
  }

  state.current_authority = admission.authority;
  state.prepared_authority = admission.authority;
  state.prepared_request = request;
  submission.status = SampleSubmissionStatus::kPrepared;
  return submission;
}

bool SampleExecutionHost::dispatch_prepared(const OperationAuthority& authority) {
  auto& state = *implementation_;
  const bool was_prepared = state.prepared_authority &&
                            state.prepared_authority->operation_id == authority.operation_id &&
                            state.prepared_authority->binding == authority.binding &&
                            state.prepared_authority->admitted_at == authority.admitted_at;
  state.poll();
  if (was_prepared && !state.prepared_authority) {
    if (state.adapter->is_deferred()) {
      state.process_next();
    } else {
      state.process_all();
    }
    return false;
  }
  if (!state.prepared_authority || !state.prepared_request || state.is_shutdown ||
      state.prepared_authority->operation_id != authority.operation_id ||
      !(state.prepared_authority->binding == authority.binding) ||
      state.prepared_authority->admitted_at != authority.admitted_at) {
    return false;
  }
  auto request = std::move(*state.prepared_request);
  state.prepared_request.reset();
  state.prepared_authority.reset();
  const bool dispatched = state.adapter->dispatch(authority, request, [&state, &authority] {
    const auto dispatch_time = state.read_time();
    state.observe_time(dispatch_time);
    return state.gate->claim_dispatch(authority, dispatch_time);
  });
  if (state.adapter->is_deferred()) {
    state.process_next();
  } else {
    state.process_all();
  }
  return dispatched;
}

SampleSubmission SampleExecutionHost::submit(const SampleRequest& request) {
  auto submission = prepare(request);
  if (submission.status == SampleSubmissionStatus::kPrepared) {
    submission.status = dispatch_prepared(*submission.authority)
                            ? SampleSubmissionStatus::kSubmitted
                            : SampleSubmissionStatus::kDispatchBlocked;
  }
  return submission;
}

ControlDecision SampleExecutionHost::request_cancel(const OperationAuthority& authority) {
  return implementation_->request_cancel(authority);
}

void SampleExecutionHost::set_cancellation_mode(CancellationMode mode) noexcept {
  implementation_->adapter->set_cancellation_mode(mode);
}

void SampleExecutionHost::set_settlement_reporting_enabled(bool enabled) noexcept {
  implementation_->adapter->set_settlement_reporting_enabled(enabled);
}

void SampleExecutionHost::set_result_replay_enabled(bool enabled) noexcept {
  implementation_->adapter->set_result_replay_enabled(enabled);
}

bool SampleExecutionHost::replay_retained_result() {
  auto& state = *implementation_;
  auto callback = state.adapter->retained_result();
  if (!callback) {
    return false;
  }
  state.staged_events.push_front({std::move(*callback), state.read_time()});
  return true;
}

std::size_t SampleExecutionHost::result_permission_denial_count() const noexcept {
  return implementation_->adapter->result_permission_denial_count();
}

std::size_t SampleExecutionHost::native_stop_request_count() const noexcept {
  return implementation_->adapter->stop_request_count();
}

ControlDecision SampleExecutionHost::poll() {
  return implementation_->poll();
}

bool SampleExecutionHost::complete_deferred() {
  implementation_->poll();
  return implementation_->adapter->complete_deferred();
}

bool SampleExecutionHost::process_next_staged_event() {
  return implementation_->process_next();
}

void SampleExecutionHost::process_all_staged_events() {
  implementation_->process_all();
}

std::size_t SampleExecutionHost::staged_event_count() const noexcept {
  return implementation_->staged_events.size();
}

std::optional<OperationReceipt>
SampleExecutionHost::receipt(const OperationAuthority& authority) const {
  return implementation_->gate->receipt(authority);
}

const std::vector<SampleResult>& SampleExecutionHost::results() const noexcept {
  return implementation_->adapter->results();
}

std::size_t SampleExecutionHost::worker_submission_count() const noexcept {
  return implementation_->adapter->submission_count();
}

std::size_t SampleExecutionHost::worker_execution_count() const noexcept {
  return implementation_->adapter->execution_count();
}

bool SampleExecutionHost::has_pending_native_work() const noexcept {
  return implementation_->adapter->has_pending_work();
}

void SampleExecutionHost::shutdown() {
  implementation_->shutdown();
}

bool SampleExecutionHost::is_shutdown() const noexcept {
  return implementation_->is_shutdown;
}

}  // namespace robot_harness
