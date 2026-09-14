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

enum class StartupEvidenceKind { kBindingIdleAndSettled, kWorkerReady, kResultSinkReady };

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
};

struct OperationRequest {
  std::string capability_id;
  std::string effect_domain;
  std::string opaque_request_reference;
  std::string target_reference;
  MonotonicTime requested_at = 0;
};

struct OperationAuthority {
  OperationId operation_id = 0;
  BindingIdentity binding;
  MonotonicTime admitted_at = 0;

  bool is_valid() const noexcept;
};

enum class AdmissionStatus {
  kAdmitted,
  kInvalidRequest,
  kRecoveryRequired,
  kDomainOccupied,
  kClosed,
};

struct AdmissionDecision {
  AdmissionStatus status = AdmissionStatus::kInvalidRequest;
  std::optional<OperationAuthority> authority;
};

enum class EvidenceDisposition { kAccepted, kDuplicate, kRejected };
enum class NativeEventKind { kAccepted, kRejected, kStarted, kTerminalSucceeded, kTerminalFailed };

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

enum class OutputNonDeliveryReason { kNoOutputProduced, kSinkRejected };

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
enum class NativeOutcome { kPending, kNotExecuted, kSucceeded, kFailed, kUnknown };
enum class OutputDisposition { kPending, kAccepted, kNotDelivered };
enum class SettlementStatus { kPending, kSettled };
enum class DomainVerdict { kUnassessed };
enum class AuthorityDisposition { kCurrent, kReleased, kBlockedUnknown };

struct OperationReceipt {
  OperationAuthority authority;
  DispatchStatus dispatch = DispatchStatus::kPending;
  NativeAcceptance native_acceptance = NativeAcceptance::kPending;
  bool native_started = false;
  NativeOutcome native_outcome = NativeOutcome::kPending;
  bool observed_native_success = false;
  bool observed_native_failure = false;
  OutputDisposition output = OutputDisposition::kPending;
  SettlementStatus settlement = SettlementStatus::kPending;
  DomainVerdict domain_verdict = DomainVerdict::kUnassessed;
  AuthorityDisposition authority_disposition = AuthorityDisposition::kCurrent;
  std::optional<MonotonicTime> dispatched_at;
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
  std::optional<EvidenceRecord> output_evidence;
  std::optional<EvidenceRecord> output_non_delivery_evidence;
  std::optional<EvidenceRecord> settlement_evidence;
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

  AdmissionDecision admit(const OperationRequest& request);
  bool claim_dispatch(const OperationAuthority& authority, MonotonicTime observed_at);
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
