#ifndef ROBOT_HARNESS_AUTHORITY_GATE_HPP_
#define ROBOT_HARNESS_AUTHORITY_GATE_HPP_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace robot_harness {

using MonotonicTime = std::uint64_t;
using OperationId = std::uint64_t;

struct BindingIdentity {
  std::string provider_id;
  std::string effect_domain;
  std::uint64_t composition_revision = 0;
  std::uint64_t provider_generation = 0;
};

bool operator==(const BindingIdentity& left, const BindingIdentity& right) noexcept;

struct EvidenceRecord {
  std::string source;
  MonotonicTime observed_at = 0;
  std::uint64_t sequence = 0;
};

enum class InitializationState { kRecoveryRequired, kReady, kClosed };

enum class StartupEvidenceKind {
  kBindingIdleAndSettled,
  kWorkerReady,
  kResultSinkReady,
  kAdapterReady
};

struct StartupEvidence {
  StartupEvidenceKind kind = StartupEvidenceKind::kBindingIdleAndSettled;
  BindingIdentity binding;
  EvidenceRecord record;
  bool observed = false;
};

struct StartupStatus {
  InitializationState state = InitializationState::kRecoveryRequired;
  BindingIdentity binding;
  bool is_binding_idle_and_settled = false;
  bool is_worker_ready = false;
  bool is_result_sink_ready = false;
  std::optional<EvidenceRecord> binding_idle_and_settled;
  std::optional<EvidenceRecord> worker_ready;
  std::optional<EvidenceRecord> result_sink_ready;
  bool is_adapter_ready = false;
  std::optional<EvidenceRecord> adapter_ready;
};

// Profile semantics belong to the adapter; Core matches the exact supported profile
// and checks the consumed workload against the observed limit.
struct ExecutionRequirements {
  std::string profile_id;
  std::uint64_t work_units = 0;
};

struct ExecutionCapabilities {
  BindingIdentity binding;
  std::string capability_id;
  std::string profile_id;
  std::uint64_t maximum_work_units = 0;
  bool available = false;
  EvidenceRecord record;
  MonotonicTime valid_until = 0;  // Exclusive; evaluated again at dispatch.
};

struct OperationRequest {
  std::string capability_id;
  std::string effect_domain;
  std::string opaque_request_reference;
  std::string target_reference;
  MonotonicTime requested_at = 0;
  std::optional<MonotonicTime> deadline;
  std::optional<ExecutionRequirements> execution_requirements;
};

struct OperationAuthority {
  OperationId operation_id = 0;
  BindingIdentity binding;
  MonotonicTime admitted_at = 0;

  bool is_valid() const noexcept;
};

enum class BindingPhase { kActive, kWithdrawn, kPreparing, kClosed };
enum class BindingControlStatus {
  kApplied,
  kAlreadyInPhase,
  kStaleIdentity,
  kWrongPhase,
  kUnresolvedWork,
  kInvalidEvidence,
  kInvalidTime,
  kInvalidProvider,
  kIdentityExhausted,
  kClosed
};

struct BindingReadinessEvidence {
  StartupEvidenceKind kind = StartupEvidenceKind::kBindingIdleAndSettled;
  BindingIdentity binding;
  EvidenceRecord record;
  bool observed = false;
  MonotonicTime valid_until = 0;  // Exclusive; rechecked at commit.
};

struct BindingDecision {
  BindingControlStatus status = BindingControlStatus::kWrongPhase;
  std::optional<BindingIdentity> candidate;
  std::optional<OperationAuthority> native_stop_authority;
};

struct BindingStatus {
  BindingPhase phase = BindingPhase::kActive;
  BindingIdentity active;
  std::optional<BindingIdentity> candidate;
  bool can_commit = false;  // At the queried time; grants no execution authority.
};

enum class AdmissionStatus {
  kAdmitted,
  kInvalidRequest,
  kExpired,
  kRecoveryRequired,
  kDomainOccupied,
  kClosed,
  kCapabilitiesUnavailable,
};

struct AdmissionDecision {
  AdmissionStatus status = AdmissionStatus::kInvalidRequest;
  std::optional<OperationAuthority> authority;
};

enum class EvidenceDisposition { kAccepted, kDuplicate, kRejected };
enum class NativeEventKind {
  kAccepted,
  kRejected,
  kStarted,
  kTerminalSucceeded,
  kTerminalFailed,
  kTerminalCancelled
};

struct NativeEvidence {
  OperationAuthority authority;
  std::string native_identity;
  NativeEventKind kind = NativeEventKind::kAccepted;
  EvidenceRecord record;
};

struct OutputEvidence {
  OperationAuthority authority;
  std::string result_reference;
  EvidenceRecord record;
};

struct NonSubmissionEvidence {
  OperationAuthority authority;
  EvidenceRecord record;
};

enum class OutputNonDeliveryReason { kNoOutputProduced, kSinkRejected, kAuthorityRevoked };

struct OutputNonDeliveryEvidence {
  OperationAuthority authority;
  OutputNonDeliveryReason reason = OutputNonDeliveryReason::kNoOutputProduced;
  std::string result_reference;
  EvidenceRecord record;
};

struct SettlementEvidence {
  OperationAuthority authority;
  std::string scope;
  bool permits_same_domain_admission = false;
  EvidenceRecord record;
};

enum class DispatchStatus { kPending, kSubmitted, kNotSubmitted };
enum class NativeAcceptance { kPending, kAccepted, kRejected };
enum class NativeOutcome { kPending, kNotExecuted, kSucceeded, kFailed, kCancelled, kUnknown };
enum class OutputDisposition { kPending, kAccepted, kNotDelivered };
enum class SettlementStatus { kPending, kSettled };
enum class DomainVerdict { kUnassessed };
enum class AuthorityDisposition { kCurrent, kRevoked, kReleased, kBlockedUnknown };

enum class ControlStatus { kApplied, kAlreadyRequested, kNoActiveOperation, kNotDue, kRejected };
struct ControlDecision {
  ControlStatus status = ControlStatus::kRejected;
  bool should_request_native_stop = false;
};

enum class StopAcknowledgement { kNotRequested, kPending, kAcknowledged, kRefused, kUnavailable };
struct StopAcknowledgementEvidence {
  OperationAuthority authority;
  StopAcknowledgement acknowledgement = StopAcknowledgement::kUnavailable;
  EvidenceRecord record;
};

struct OperationReceipt {
  OperationAuthority authority;
  DispatchStatus dispatch = DispatchStatus::kPending;
  NativeAcceptance native_acceptance = NativeAcceptance::kPending;
  bool native_started = false;
  NativeOutcome native_outcome = NativeOutcome::kPending;
  bool observed_native_success = false;
  bool observed_native_failure = false;
  bool observed_native_cancellation = false;
  OutputDisposition output = OutputDisposition::kPending;
  SettlementStatus settlement = SettlementStatus::kPending;
  DomainVerdict domain_verdict = DomainVerdict::kUnassessed;
  AuthorityDisposition authority_disposition = AuthorityDisposition::kCurrent;
  std::optional<MonotonicTime> dispatched_at;
  std::optional<MonotonicTime> cancellation_requested_at;
  std::optional<MonotonicTime> deadline;
  std::optional<MonotonicTime> expiry_observed_at;
  std::optional<MonotonicTime> binding_withdrawn_at;
  std::optional<MonotonicTime> native_stop_requested_at;
  StopAcknowledgement stop_acknowledgement = StopAcknowledgement::kNotRequested;
  std::string native_identity;
  std::string result_reference;
  std::string settlement_scope;
  std::optional<OutputNonDeliveryReason> output_non_delivery_reason;
  std::optional<EvidenceRecord> non_submission_evidence;
  std::optional<EvidenceRecord> native_acceptance_evidence;
  std::optional<EvidenceRecord> native_rejection_evidence;
  std::optional<EvidenceRecord> native_started_evidence;
  std::optional<EvidenceRecord> native_success_evidence;
  std::optional<EvidenceRecord> native_failure_evidence;
  std::optional<EvidenceRecord> native_cancellation_evidence;
  std::optional<EvidenceRecord> stop_acknowledgement_evidence;
  std::optional<EvidenceRecord> output_evidence;
  std::optional<EvidenceRecord> output_non_delivery_evidence;
  std::optional<EvidenceRecord> settlement_evidence;
  std::optional<ExecutionRequirements> execution_requirements;
};

struct AuthorityGateConfig {
  BindingIdentity binding;
  std::string capability_id;
  std::string binding_observer;
  std::string worker_observer;
  std::string result_sink_observer;
  std::string native_evidence_source;
  std::string settlement_evidence_source;
  std::string settlement_scope;
  MonotonicTime started_at = 0;
  // Stop responses have a separate sequence stream from native execution events.
  std::string stop_evidence_source = "native-stop";
  StartupEvidenceKind readiness_kind = StartupEvidenceKind::kWorkerReady;
  // A configured observer makes execution requirements mandatory for admission.
  std::string execution_capability_observer;
};

class AuthorityGate {
public:
  explicit AuthorityGate(AuthorityGateConfig config);
  ~AuthorityGate();

  AuthorityGate(const AuthorityGate&) = delete;
  AuthorityGate& operator=(const AuthorityGate&) = delete;
  AuthorityGate(AuthorityGate&&) = delete;
  AuthorityGate& operator=(AuthorityGate&&) = delete;

  EvidenceDisposition observe_startup(const StartupEvidence& evidence);
  StartupStatus startup_status() const;
  EvidenceDisposition observe_execution_capabilities(const ExecutionCapabilities& capabilities);
  bool supports_execution(const ExecutionRequirements& requirements, MonotonicTime at) const;

  BindingStatus binding_status(MonotonicTime at) const;
  BindingDecision withdraw_binding(const BindingIdentity& expected_binding, MonotonicTime now);
  BindingDecision begin_rebind(const BindingIdentity& expected_old_binding,
                               const std::string& new_provider_id,
                               const BindingReadinessEvidence& old_scope_evidence,
                               MonotonicTime now);
  EvidenceDisposition observe_rebind_readiness(const BindingReadinessEvidence& evidence);
  BindingDecision commit_rebind(const BindingIdentity& expected_candidate, MonotonicTime now);

  AdmissionDecision admit(const OperationRequest& request);
  bool claim_dispatch(const OperationAuthority& authority, MonotonicTime observed_at);
  ControlDecision request_cancel(const OperationAuthority& authority, MonotonicTime observed_at);
  // Host calls this at execution boundaries and while idle; receipt reads are passive.
  ControlDecision observe_time(const OperationAuthority& authority, MonotonicTime observed_at);
  EvidenceDisposition observe_stop_acknowledgement(const StopAcknowledgementEvidence& evidence);
  EvidenceDisposition observe_non_submission(const NonSubmissionEvidence& evidence);
  EvidenceDisposition observe_native(const NativeEvidence& evidence);
  bool can_deliver_result(const OperationAuthority& authority) const;
  EvidenceDisposition observe_output_accepted(const OutputEvidence& evidence);
  EvidenceDisposition observe_output_not_delivered(const OutputNonDeliveryEvidence& evidence);
  EvidenceDisposition observe_settlement(const SettlementEvidence& evidence);
  std::optional<OperationReceipt> receipt(const OperationAuthority& authority) const;

  void close_admission() noexcept;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace robot_harness

#endif  // ROBOT_HARNESS_AUTHORITY_GATE_HPP_
