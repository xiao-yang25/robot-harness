#include "robot_harness/sample_execution.hpp"

#include <deque>
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
    is_closed_ = true;
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
  };

  void emit(const WorkItem& work, CallbackKind kind, std::uint64_t sequence,
            std::optional<SampleResult> result = std::nullopt) {
    if (callback_) {
      callback_({kind, work.authority, work.native_identity, sequence, std::move(result)});
    }
  }

  void finish(const WorkItem& work) {
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
  std::optional<WorkItem> pending_work_;
  std::vector<SampleResult> results_;
  std::size_t submission_count_ = 0;
  std::size_t execution_count_ = 0;
};

}  // namespace

struct SampleExecutionHost::Implementation {
  Implementation(CompletionMode completion_mode, Clock supplied_clock)
      : clock(std::move(supplied_clock)), adapter(completion_mode) {
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
    adapter.set_callback([this](NativeCallback callback) {
      staged_events.push_back({std::move(callback), read_time()});
    });
  }

  ~Implementation() {
    try {
      shutdown();
    } catch (...) {
      adapter.clear_callback();
      adapter.close();
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
    return {kind, kSampleBinding, {source, read_time(), sequence}, observed};
  }

  bool process_next() {
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
      const auto acceptance =
          adapter.accept_result(callback.authority, std::move(*callback.result), [this, &callback] {
            return gate->can_deliver_result(callback.authority);
          });
      callback.result.reset();
      if (acceptance == ResultAcceptance::kAccepted) {
        gate->observe_output_accepted(
            {callback.authority, result_reference, {kResultSinkObserver, read_time(), 1}});
      } else if (acceptance == ResultAcceptance::kSinkUnavailable) {
        gate->observe_output_not_delivered({callback.authority,
                                            OutputNonDeliveryReason::kSinkRejected,
                                            result_reference,
                                            {kResultSinkObserver, read_time(), 1}});
      }
      break;
    }
    case CallbackKind::kSettled:
      gate->observe_settlement({callback.authority,
                                kSettlementScope,
                                true,
                                {kSettlementEvidenceSource, read_time(), callback.sequence}});
      break;
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
    gate->close_admission();
    adapter.drain_pending();
    process_all();
    adapter.close();
    adapter.clear_callback();
    is_shutdown = true;
  }

  Clock clock;
  MonotonicTime started_at = 0;
  MonotonicTime last_time = 0;
  bool has_read_time = false;
  std::unique_ptr<AuthorityGate> gate;
  DeterministicSampleAdapter adapter;
  std::deque<StagedEvent> staged_events;
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
  state.gate->observe_startup(state.startup_evidence(
      StartupEvidenceKind::kBindingIdleAndSettled, kBindingObserver,
      state.adapter.is_idle_and_settled(), ++state.binding_observation_sequence));
  state.gate->observe_startup(
      state.startup_evidence(StartupEvidenceKind::kWorkerReady, kWorkerObserver,
                             state.adapter.is_worker_ready(), ++state.worker_observation_sequence));
  state.gate->observe_startup(state.startup_evidence(
      StartupEvidenceKind::kResultSinkReady, kResultSinkObserver,
      state.adapter.is_result_sink_ready(), ++state.sink_observation_sequence));
  return state.gate->startup_status();
}

void SampleExecutionHost::set_worker_ready(bool is_ready) {
  implementation_->adapter.set_worker_ready(is_ready);
}

void SampleExecutionHost::set_result_sink_ready(bool is_ready) {
  implementation_->adapter.set_result_sink_ready(is_ready);
}

void SampleExecutionHost::reject_next_native_submission() noexcept {
  implementation_->adapter.reject_next_native_submission();
}

void SampleExecutionHost::fail_next_native_execution() noexcept {
  implementation_->adapter.fail_next_native_execution();
}

SampleSubmission SampleExecutionHost::submit(const SampleRequest& request) {
  auto& state = *implementation_;
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
  const auto admission = state.gate->admit(operation_request);
  submission.admission_status = admission.status;
  submission.authority = admission.authority;
  if (admission.status != AdmissionStatus::kAdmitted || !admission.authority.has_value()) {
    submission.status = SampleSubmissionStatus::kAdmissionBlocked;
    return submission;
  }

  const auto authority = *admission.authority;
  const bool dispatched = state.adapter.dispatch(authority, request, [&state, &authority] {
    return state.gate->claim_dispatch(authority, state.read_time());
  });
  if (state.adapter.is_deferred()) {
    state.process_next();
  } else {
    state.process_all();
  }
  if (!dispatched) {
    submission.status = SampleSubmissionStatus::kDispatchBlocked;
    return submission;
  }
  submission.status = SampleSubmissionStatus::kSubmitted;
  return submission;
}

bool SampleExecutionHost::complete_deferred() {
  return implementation_->adapter.complete_deferred();
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
  return implementation_->adapter.results();
}

std::size_t SampleExecutionHost::worker_submission_count() const noexcept {
  return implementation_->adapter.submission_count();
}

std::size_t SampleExecutionHost::worker_execution_count() const noexcept {
  return implementation_->adapter.execution_count();
}

bool SampleExecutionHost::has_pending_native_work() const noexcept {
  return implementation_->adapter.has_pending_work();
}

void SampleExecutionHost::shutdown() {
  implementation_->shutdown();
}

bool SampleExecutionHost::is_shutdown() const noexcept {
  return implementation_->is_shutdown;
}

}  // namespace robot_harness
