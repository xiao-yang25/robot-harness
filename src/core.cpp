#include "robot_harness/authority_gate.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace robot_harness {
namespace {

bool records_match(const EvidenceRecord& left, const EvidenceRecord& right) {
  return left.source == right.source && left.observed_at == right.observed_at &&
         left.sequence == right.sequence;
}

bool authority_matches(const OperationAuthority& left, const OperationAuthority& right) {
  return left.operation_id == right.operation_id && left.binding == right.binding &&
         left.admitted_at == right.admitted_at;
}

bool is_record_well_formed(const EvidenceRecord& record, const std::string& expected_source,
                           MonotonicTime earliest_time) {
  return !record.source.empty() && record.source == expected_source && record.sequence != 0 &&
         record.observed_at >= earliest_time;
}

std::array<const std::optional<EvidenceRecord>*, 11>
receipt_records(const OperationReceipt& receipt) {
  return {&receipt.native_acceptance_evidence,   &receipt.native_rejection_evidence,
          &receipt.native_started_evidence,      &receipt.native_success_evidence,
          &receipt.native_failure_evidence,      &receipt.output_evidence,
          &receipt.non_submission_evidence,      &receipt.output_non_delivery_evidence,
          &receipt.settlement_evidence,          &receipt.native_cancellation_evidence,
          &receipt.stop_acknowledgement_evidence};
}

bool follows_same_source_records(const EvidenceRecord& evidence, const OperationReceipt& receipt) {
  for (const auto* record : receipt_records(receipt)) {
    if (record->has_value() && (*record)->source == evidence.source &&
        evidence.sequence <= (*record)->sequence) {
      return false;
    }
  }
  return true;
}

bool is_revoked(const OperationReceipt& receipt) {
  return receipt.cancellation_requested_at.has_value() || receipt.expiry_observed_at.has_value();
}

MonotonicTime latest_effect_time(const OperationReceipt& receipt) {
  MonotonicTime time = receipt.authority.admitted_at;
  if (receipt.dispatched_at.has_value() && *receipt.dispatched_at > time) {
    time = *receipt.dispatched_at;
  }
  for (const auto* record : receipt_records(receipt)) {
    // A stop response is not a causal prerequisite of already completed cleanup.
    if (record == &receipt.stop_acknowledgement_evidence) {
      continue;
    }
    if (record->has_value() && (*record)->observed_at > time) {
      time = (*record)->observed_at;
    }
  }
  return time;
}

MonotonicTime latest_time(const OperationReceipt& receipt) {
  auto time = latest_effect_time(receipt);
  if (receipt.stop_acknowledgement_evidence &&
      receipt.stop_acknowledgement_evidence->observed_at > time) {
    time = receipt.stop_acknowledgement_evidence->observed_at;
  }
  if (receipt.cancellation_requested_at && *receipt.cancellation_requested_at > time) {
    time = *receipt.cancellation_requested_at;
  }
  if (receipt.expiry_observed_at && *receipt.expiry_observed_at > time) {
    time = *receipt.expiry_observed_at;
  }
  return time;
}

bool has_reached_deadline(const OperationReceipt& receipt, MonotonicTime time) {
  return receipt.deadline && time >= *receipt.deadline;
}

bool revocation_precedes(const OperationReceipt& receipt, MonotonicTime time) {
  return (receipt.cancellation_requested_at && time >= *receipt.cancellation_requested_at) ||
         (receipt.expiry_observed_at && time >= *receipt.expiry_observed_at);
}

}  // namespace

bool operator==(const BindingIdentity& left, const BindingIdentity& right) noexcept {
  return left.provider_id == right.provider_id && left.effect_domain == right.effect_domain &&
         left.composition_revision == right.composition_revision &&
         left.provider_generation == right.provider_generation;
}

bool OperationAuthority::is_valid() const noexcept {
  return operation_id != 0 && !binding.provider_id.empty() && !binding.effect_domain.empty() &&
         binding.composition_revision != 0 && binding.provider_generation != 0;
}

struct AuthorityGate::Implementation {
  explicit Implementation(AuthorityGateConfig supplied_config)
      : config(std::move(supplied_config)) {
  }

  struct StartupFact {
    bool observed = false;
    bool value = false;
    std::optional<EvidenceRecord> record;
  };

  AuthorityGateConfig config;
  InitializationState initialization_state = InitializationState::kRecoveryRequired;
  StartupFact binding_idle;
  StartupFact worker_ready;
  StartupFact result_sink_ready;
  OperationId next_operation_id = 1;
  std::optional<OperationReceipt> current_receipt;
  std::optional<EvidenceRecord> last_native_record;
  std::optional<NativeEventKind> last_native_kind;
  std::optional<ExecutionCapabilities> execution_capabilities;

  bool current_authority_matches(const OperationAuthority& authority) const {
    return current_receipt.has_value() && authority_matches(current_receipt->authority, authority);
  }

  bool current_is_releasable() const {
    return current_receipt.has_value() && has_resolved_closure(*current_receipt) &&
           current_receipt->authority_disposition == AuthorityDisposition::kReleased;
  }

  bool has_resolved_effects(const OperationReceipt& receipt) const {
    if (receipt.dispatch == DispatchStatus::kNotSubmitted) {
      return receipt.native_outcome == NativeOutcome::kNotExecuted &&
             receipt.output == OutputDisposition::kNotDelivered &&
             receipt.output_non_delivery_reason == OutputNonDeliveryReason::kNoOutputProduced;
    }
    if (receipt.dispatch != DispatchStatus::kSubmitted) {
      return false;
    }
    if (receipt.native_acceptance == NativeAcceptance::kRejected) {
      return receipt.native_outcome == NativeOutcome::kNotExecuted &&
             receipt.output == OutputDisposition::kNotDelivered &&
             receipt.output_non_delivery_reason == OutputNonDeliveryReason::kNoOutputProduced;
    }
    if (receipt.native_acceptance != NativeAcceptance::kAccepted) {
      return false;
    }
    if (receipt.native_outcome == NativeOutcome::kFailed ||
        receipt.native_outcome == NativeOutcome::kCancelled) {
      return receipt.output == OutputDisposition::kNotDelivered &&
             receipt.output_non_delivery_reason == OutputNonDeliveryReason::kNoOutputProduced;
    }
    if (receipt.native_outcome != NativeOutcome::kSucceeded) {
      return false;
    }
    return receipt.output == OutputDisposition::kAccepted ||
           (receipt.output == OutputDisposition::kNotDelivered &&
            (receipt.output_non_delivery_reason == OutputNonDeliveryReason::kSinkRejected ||
             receipt.output_non_delivery_reason == OutputNonDeliveryReason::kAuthorityRevoked));
  }

  bool has_resolved_closure(const OperationReceipt& receipt) const {
    return receipt.settlement == SettlementStatus::kSettled && has_resolved_effects(receipt);
  }

  ControlDecision revoke(MonotonicTime observed_at) {
    auto& receipt = *current_receipt;
    const bool should_stop = receipt.dispatch == DispatchStatus::kSubmitted &&
                             receipt.native_outcome == NativeOutcome::kPending &&
                             !receipt.native_stop_requested_at;
    if (should_stop) {
      receipt.native_stop_requested_at = observed_at;
      receipt.stop_acknowledgement = StopAcknowledgement::kPending;
    }
    refresh_authority_disposition();
    return {ControlStatus::kApplied, should_stop};
  }

  void refresh_authority_disposition() {
    if (!current_receipt.has_value()) {
      return;
    }
    if (current_receipt->native_outcome == NativeOutcome::kUnknown) {
      current_receipt->authority_disposition = AuthorityDisposition::kBlockedUnknown;
      return;
    }
    if (has_resolved_closure(*current_receipt)) {
      current_receipt->authority_disposition = AuthorityDisposition::kReleased;
      return;
    }
    current_receipt->authority_disposition = is_revoked(*current_receipt)
                                                 ? AuthorityDisposition::kRevoked
                                                 : AuthorityDisposition::kCurrent;
  }
};

AuthorityGate::AuthorityGate(AuthorityGateConfig config)
    : implementation_(std::make_unique<Implementation>(std::move(config))) {
  const auto& value = implementation_->config;
  if (value.binding.provider_id.empty() || value.binding.effect_domain.empty() ||
      value.binding.composition_revision == 0 || value.binding.provider_generation == 0 ||
      value.capability_id.empty() || value.binding_observer.empty() ||
      value.worker_observer.empty() || value.result_sink_observer.empty() ||
      value.native_evidence_source.empty() || value.settlement_evidence_source.empty() ||
      value.settlement_scope.empty() || value.stop_evidence_source.empty() ||
      value.stop_evidence_source == value.native_evidence_source ||
      value.stop_evidence_source == value.settlement_evidence_source ||
      value.stop_evidence_source == value.result_sink_observer ||
      (value.readiness_kind != StartupEvidenceKind::kWorkerReady &&
       value.readiness_kind != StartupEvidenceKind::kAdapterReady)) {
    throw std::invalid_argument(
        "authority gate configuration requires static identities and sources");
  }
}

AuthorityGate::~AuthorityGate() = default;

EvidenceDisposition AuthorityGate::observe_startup(const StartupEvidence& evidence) {
  auto& state = *implementation_;
  if (state.initialization_state == InitializationState::kClosed ||
      state.current_receipt.has_value() || !(evidence.binding == state.config.binding)) {
    return EvidenceDisposition::kRejected;
  }

  Implementation::StartupFact* fact = nullptr;
  const std::string* expected_source = nullptr;
  switch (evidence.kind) {
  case StartupEvidenceKind::kBindingIdleAndSettled:
    fact = &state.binding_idle;
    expected_source = &state.config.binding_observer;
    break;
  case StartupEvidenceKind::kWorkerReady:
  case StartupEvidenceKind::kAdapterReady:
    if (evidence.kind != state.config.readiness_kind) {
      return EvidenceDisposition::kRejected;
    }
    fact = &state.worker_ready;
    expected_source = &state.config.worker_observer;
    break;
  case StartupEvidenceKind::kResultSinkReady:
    fact = &state.result_sink_ready;
    expected_source = &state.config.result_sink_observer;
    break;
  default:
    return EvidenceDisposition::kRejected;
  }

  if (!is_record_well_formed(evidence.record, *expected_source, state.config.started_at)) {
    return EvidenceDisposition::kRejected;
  }
  if (fact->record.has_value()) {
    if (evidence.record.sequence < fact->record->sequence ||
        evidence.record.observed_at < fact->record->observed_at) {
      return EvidenceDisposition::kRejected;
    }
    if (evidence.record.sequence == fact->record->sequence) {
      return records_match(evidence.record, *fact->record) && evidence.observed == fact->value
                 ? EvidenceDisposition::kDuplicate
                 : EvidenceDisposition::kRejected;
    }
  }

  fact->observed = true;
  fact->value = evidence.observed;
  fact->record = evidence.record;
  if (state.binding_idle.observed && state.binding_idle.value && state.worker_ready.observed &&
      state.worker_ready.value && state.result_sink_ready.observed &&
      state.result_sink_ready.value) {
    state.initialization_state = InitializationState::kReady;
  } else {
    state.initialization_state = InitializationState::kRecoveryRequired;
  }
  return EvidenceDisposition::kAccepted;
}

StartupStatus AuthorityGate::startup_status() const {
  const auto& state = *implementation_;
  StartupStatus status;
  status.state = state.initialization_state;
  status.binding = state.config.binding;
  status.is_binding_idle_and_settled = state.binding_idle.observed && state.binding_idle.value;
  const bool is_ready = state.worker_ready.observed && state.worker_ready.value;
  status.is_worker_ready =
      state.config.readiness_kind == StartupEvidenceKind::kWorkerReady && is_ready;
  status.is_adapter_ready =
      state.config.readiness_kind == StartupEvidenceKind::kAdapterReady && is_ready;
  status.is_result_sink_ready = state.result_sink_ready.observed && state.result_sink_ready.value;
  if (state.binding_idle.observed) {
    status.binding_idle_and_settled = state.binding_idle.record;
  }
  if (state.worker_ready.observed) {
    if (state.config.readiness_kind == StartupEvidenceKind::kWorkerReady) {
      status.worker_ready = state.worker_ready.record;
    } else {
      status.adapter_ready = state.worker_ready.record;
    }
  }
  if (state.result_sink_ready.observed) {
    status.result_sink_ready = state.result_sink_ready.record;
  }
  return status;
}

EvidenceDisposition
AuthorityGate::observe_execution_capabilities(const ExecutionCapabilities& capabilities) {
  auto& state = *implementation_;
  if (state.initialization_state == InitializationState::kClosed ||
      state.config.execution_capability_observer.empty() ||
      !(capabilities.binding == state.config.binding) ||
      capabilities.capability_id != state.config.capability_id || capabilities.profile_id.empty() ||
      capabilities.maximum_work_units == 0 ||
      capabilities.valid_until <= capabilities.record.observed_at ||
      !is_record_well_formed(capabilities.record, state.config.execution_capability_observer,
                             state.config.started_at)) {
    return EvidenceDisposition::kRejected;
  }
  if (state.execution_capabilities) {
    const auto& previous = *state.execution_capabilities;
    if (capabilities.record.observed_at < previous.record.observed_at ||
        capabilities.record.sequence < previous.record.sequence) {
      return EvidenceDisposition::kRejected;
    }
    if (capabilities.record.sequence == previous.record.sequence) {
      return records_match(capabilities.record, previous.record) &&
                     capabilities.profile_id == previous.profile_id &&
                     capabilities.maximum_work_units == previous.maximum_work_units &&
                     capabilities.available == previous.available &&
                     capabilities.valid_until == previous.valid_until
                 ? EvidenceDisposition::kDuplicate
                 : EvidenceDisposition::kRejected;
    }
  }
  state.execution_capabilities = capabilities;
  return EvidenceDisposition::kAccepted;
}

bool AuthorityGate::supports_execution(const ExecutionRequirements& requirements,
                                       MonotonicTime at) const {
  const auto& state = *implementation_;
  if (!state.execution_capabilities || requirements.profile_id.empty() ||
      requirements.work_units == 0) {
    return false;
  }
  const auto& capabilities = *state.execution_capabilities;
  return capabilities.available && capabilities.profile_id == requirements.profile_id &&
         requirements.work_units <= capabilities.maximum_work_units &&
         at >= capabilities.record.observed_at && at < capabilities.valid_until;
}

AdmissionDecision AuthorityGate::admit(const OperationRequest& request) {
  auto& state = *implementation_;
  AdmissionDecision decision;
  if (state.initialization_state == InitializationState::kClosed) {
    decision.status = AdmissionStatus::kClosed;
    return decision;
  }
  if (request.capability_id != state.config.capability_id ||
      request.effect_domain != state.config.binding.effect_domain ||
      request.opaque_request_reference.empty() || request.target_reference.empty() ||
      request.requested_at < state.config.started_at) {
    decision.status = AdmissionStatus::kInvalidRequest;
    return decision;
  }
  if (request.deadline && request.requested_at >= *request.deadline) {
    decision.status = AdmissionStatus::kExpired;
    return decision;
  }
  if (state.initialization_state != InitializationState::kReady) {
    decision.status = AdmissionStatus::kRecoveryRequired;
    return decision;
  }
  if (state.current_receipt.has_value() && !state.current_is_releasable()) {
    decision.status = AdmissionStatus::kDomainOccupied;
    return decision;
  }
  if (state.current_receipt.has_value() &&
      request.requested_at < latest_time(*state.current_receipt)) {
    decision.status = AdmissionStatus::kInvalidRequest;
    return decision;
  }
  const std::optional<EvidenceRecord>* startup_records[] = {
      &state.binding_idle.record, &state.worker_ready.record, &state.result_sink_ready.record};
  for (const auto* record : startup_records) {
    if (record->has_value() && request.requested_at < (*record)->observed_at) {
      decision.status = AdmissionStatus::kInvalidRequest;
      return decision;
    }
  }

  if ((!state.config.execution_capability_observer.empty() && !request.execution_requirements) ||
      (request.execution_requirements &&
       !supports_execution(*request.execution_requirements, request.requested_at))) {
    decision.status = AdmissionStatus::kCapabilitiesUnavailable;
    return decision;
  }

  OperationAuthority authority;
  authority.operation_id = state.next_operation_id++;
  authority.binding = state.config.binding;
  authority.admitted_at = request.requested_at;

  OperationReceipt receipt;
  receipt.authority = authority;
  receipt.deadline = request.deadline;
  receipt.execution_requirements = request.execution_requirements;
  state.current_receipt = receipt;
  state.last_native_record.reset();
  state.last_native_kind.reset();

  decision.status = AdmissionStatus::kAdmitted;
  decision.authority = authority;
  return decision;
}

bool AuthorityGate::claim_dispatch(const OperationAuthority& authority, MonotonicTime observed_at) {
  auto& state = *implementation_;
  if (state.initialization_state != InitializationState::kReady ||
      !state.current_authority_matches(authority) || observed_at < authority.admitted_at ||
      state.current_receipt->dispatch != DispatchStatus::kPending ||
      is_revoked(*state.current_receipt) ||
      has_reached_deadline(*state.current_receipt, observed_at) ||
      (state.current_receipt->execution_requirements &&
       !supports_execution(*state.current_receipt->execution_requirements, observed_at))) {
    return false;
  }
  state.current_receipt->dispatch = DispatchStatus::kSubmitted;
  state.current_receipt->dispatched_at = observed_at;
  return true;
}

ControlDecision AuthorityGate::request_cancel(const OperationAuthority& authority,
                                              MonotonicTime observed_at) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(authority)) {
    return {ControlStatus::kNoActiveOperation, false};
  }
  auto& receipt = *state.current_receipt;
  if (receipt.authority_disposition == AuthorityDisposition::kReleased) {
    return {ControlStatus::kNoActiveOperation, false};
  }
  if (observed_at < latest_time(receipt)) {
    return {ControlStatus::kRejected, false};
  }
  if (receipt.cancellation_requested_at) {
    return {ControlStatus::kAlreadyRequested, false};
  }
  receipt.cancellation_requested_at = observed_at;
  return state.revoke(observed_at);
}

ControlDecision AuthorityGate::observe_time(const OperationAuthority& authority,
                                            MonotonicTime observed_at) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(authority) ||
      state.current_receipt->authority_disposition == AuthorityDisposition::kReleased) {
    return {ControlStatus::kNoActiveOperation, false};
  }
  auto& receipt = *state.current_receipt;
  if (observed_at < latest_time(receipt)) {
    return {ControlStatus::kRejected, false};
  }
  if (receipt.expiry_observed_at) {
    return {ControlStatus::kAlreadyRequested, false};
  }
  if (!has_reached_deadline(receipt, observed_at)) {
    return {ControlStatus::kNotDue, false};
  }
  receipt.expiry_observed_at = observed_at;
  return state.revoke(observed_at);
}

EvidenceDisposition
AuthorityGate::observe_stop_acknowledgement(const StopAcknowledgementEvidence& evidence) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(evidence.authority)) {
    return EvidenceDisposition::kRejected;
  }
  auto& receipt = *state.current_receipt;
  if (!receipt.native_stop_requested_at ||
      !is_record_well_formed(evidence.record, state.config.stop_evidence_source,
                             *receipt.native_stop_requested_at)) {
    return EvidenceDisposition::kRejected;
  }
  switch (evidence.acknowledgement) {
  case StopAcknowledgement::kAcknowledged:
  case StopAcknowledgement::kRefused:
  case StopAcknowledgement::kUnavailable:
    break;
  default:
    return EvidenceDisposition::kRejected;
  }
  if (receipt.stop_acknowledgement_evidence) {
    return receipt.stop_acknowledgement == evidence.acknowledgement &&
                   records_match(*receipt.stop_acknowledgement_evidence, evidence.record)
               ? EvidenceDisposition::kDuplicate
               : EvidenceDisposition::kRejected;
  }
  if (receipt.authority_disposition == AuthorityDisposition::kReleased) {
    return EvidenceDisposition::kRejected;
  }
  receipt.stop_acknowledgement = evidence.acknowledgement;
  receipt.stop_acknowledgement_evidence = evidence.record;
  return EvidenceDisposition::kAccepted;
}

EvidenceDisposition AuthorityGate::observe_non_submission(const NonSubmissionEvidence& evidence) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(evidence.authority) ||
      !is_record_well_formed(evidence.record, state.config.settlement_evidence_source,
                             evidence.authority.admitted_at)) {
    return EvidenceDisposition::kRejected;
  }
  auto& receipt = *state.current_receipt;
  if (receipt.dispatch == DispatchStatus::kSubmitted &&
      (!receipt.dispatched_at || evidence.record.observed_at < *receipt.dispatched_at)) {
    return EvidenceDisposition::kRejected;
  }
  if (receipt.dispatch == DispatchStatus::kNotSubmitted) {
    return receipt.non_submission_evidence.has_value() &&
                   records_match(*receipt.non_submission_evidence, evidence.record)
               ? EvidenceDisposition::kDuplicate
               : EvidenceDisposition::kRejected;
  }
  // The adapter can prove that a claimed dispatch failed before any child/native
  // work was created. Once any native observation exists this path is forbidden.
  if ((receipt.dispatch != DispatchStatus::kPending &&
       receipt.dispatch != DispatchStatus::kSubmitted) ||
      receipt.native_acceptance_evidence.has_value() ||
      receipt.native_rejection_evidence.has_value() ||
      receipt.native_started_evidence.has_value() || receipt.native_success_evidence.has_value() ||
      receipt.native_failure_evidence.has_value()) {
    return EvidenceDisposition::kRejected;
  }
  receipt.dispatch = DispatchStatus::kNotSubmitted;
  receipt.native_outcome = NativeOutcome::kNotExecuted;
  receipt.non_submission_evidence = evidence.record;
  state.refresh_authority_disposition();
  return EvidenceDisposition::kAccepted;
}

EvidenceDisposition AuthorityGate::observe_native(const NativeEvidence& evidence) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(evidence.authority) || evidence.native_identity.empty() ||
      state.current_receipt->dispatch != DispatchStatus::kSubmitted ||
      !is_record_well_formed(evidence.record, state.config.native_evidence_source,
                             *state.current_receipt->dispatched_at)) {
    return EvidenceDisposition::kRejected;
  }

  switch (evidence.kind) {
  case NativeEventKind::kAccepted:
  case NativeEventKind::kRejected:
  case NativeEventKind::kStarted:
  case NativeEventKind::kTerminalSucceeded:
  case NativeEventKind::kTerminalFailed:
  case NativeEventKind::kTerminalCancelled:
    break;
  default:
    return EvidenceDisposition::kRejected;
  }

  if (state.last_native_record.has_value()) {
    if (evidence.record.sequence < state.last_native_record->sequence ||
        evidence.record.observed_at < state.last_native_record->observed_at) {
      return EvidenceDisposition::kRejected;
    }
    if (evidence.record.sequence == state.last_native_record->sequence) {
      return records_match(evidence.record, *state.last_native_record) &&
                     evidence.kind == *state.last_native_kind &&
                     evidence.native_identity == state.current_receipt->native_identity
                 ? EvidenceDisposition::kDuplicate
                 : EvidenceDisposition::kRejected;
    }
  }

  auto& receipt = *state.current_receipt;
  if (!receipt.native_identity.empty() && receipt.native_identity != evidence.native_identity) {
    return EvidenceDisposition::kRejected;
  }
  if (receipt.native_identity.empty() && evidence.kind != NativeEventKind::kAccepted &&
      evidence.kind != NativeEventKind::kRejected) {
    return EvidenceDisposition::kRejected;
  }

  if ((receipt.native_acceptance == NativeAcceptance::kRejected &&
       evidence.kind != NativeEventKind::kRejected) ||
      (receipt.native_acceptance == NativeAcceptance::kAccepted &&
       evidence.kind == NativeEventKind::kRejected)) {
    return EvidenceDisposition::kRejected;
  }

  EvidenceDisposition disposition = EvidenceDisposition::kAccepted;
  switch (evidence.kind) {
  case NativeEventKind::kAccepted:
    if (receipt.native_acceptance == NativeAcceptance::kAccepted) {
      disposition = EvidenceDisposition::kDuplicate;
    } else {
      receipt.native_identity = evidence.native_identity;
      receipt.native_acceptance = NativeAcceptance::kAccepted;
      receipt.native_acceptance_evidence = evidence.record;
    }
    break;
  case NativeEventKind::kRejected:
    if (receipt.native_acceptance == NativeAcceptance::kRejected) {
      disposition = EvidenceDisposition::kDuplicate;
    } else {
      receipt.native_identity = evidence.native_identity;
      receipt.native_acceptance = NativeAcceptance::kRejected;
      receipt.native_outcome = NativeOutcome::kNotExecuted;
      receipt.native_rejection_evidence = evidence.record;
    }
    break;
  case NativeEventKind::kStarted:
    if (receipt.native_acceptance != NativeAcceptance::kAccepted) {
      return EvidenceDisposition::kRejected;
    }
    if (receipt.native_started) {
      disposition = EvidenceDisposition::kDuplicate;
    } else {
      receipt.native_started = true;
      receipt.native_started_evidence = evidence.record;
    }
    break;
  case NativeEventKind::kTerminalSucceeded:
    if (receipt.native_acceptance != NativeAcceptance::kAccepted) {
      return EvidenceDisposition::kRejected;
    }
    if (receipt.observed_native_success) {
      disposition = EvidenceDisposition::kDuplicate;
    } else {
      receipt.observed_native_success = true;
      receipt.native_success_evidence = evidence.record;
    }
    break;
  case NativeEventKind::kTerminalFailed:
    if (receipt.native_acceptance != NativeAcceptance::kAccepted) {
      return EvidenceDisposition::kRejected;
    }
    if (receipt.observed_native_failure) {
      disposition = EvidenceDisposition::kDuplicate;
    } else {
      receipt.observed_native_failure = true;
      receipt.native_failure_evidence = evidence.record;
    }
    break;
  case NativeEventKind::kTerminalCancelled:
    if (receipt.native_acceptance != NativeAcceptance::kAccepted) {
      return EvidenceDisposition::kRejected;
    }
    if (receipt.observed_native_cancellation) {
      disposition = EvidenceDisposition::kDuplicate;
    } else {
      receipt.observed_native_cancellation = true;
      receipt.native_cancellation_evidence = evidence.record;
    }
    break;
  }

  const int terminal_count = static_cast<int>(receipt.observed_native_success) +
                             static_cast<int>(receipt.observed_native_failure) +
                             static_cast<int>(receipt.observed_native_cancellation);
  if (terminal_count > 1) {
    receipt.native_outcome = NativeOutcome::kUnknown;
  } else if (receipt.observed_native_success) {
    receipt.native_outcome = NativeOutcome::kSucceeded;
  } else if (receipt.observed_native_failure) {
    receipt.native_outcome = NativeOutcome::kFailed;
  } else if (receipt.observed_native_cancellation) {
    receipt.native_outcome = NativeOutcome::kCancelled;
  }
  state.last_native_record = evidence.record;
  state.last_native_kind = evidence.kind;
  state.refresh_authority_disposition();
  return disposition;
}

bool AuthorityGate::can_deliver_result(const OperationAuthority& authority) const {
  const auto& state = *implementation_;
  return state.current_authority_matches(authority) && !is_revoked(*state.current_receipt) &&
         state.current_receipt->native_outcome == NativeOutcome::kSucceeded &&
         state.current_receipt->output == OutputDisposition::kPending;
}

EvidenceDisposition AuthorityGate::observe_output_accepted(const OutputEvidence& evidence) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(evidence.authority) || evidence.result_reference.empty() ||
      !is_record_well_formed(evidence.record, state.config.result_sink_observer,
                             evidence.authority.admitted_at)) {
    return EvidenceDisposition::kRejected;
  }
  auto& receipt = *state.current_receipt;
  if (receipt.output == OutputDisposition::kAccepted) {
    return receipt.result_reference == evidence.result_reference &&
                   receipt.output_evidence.has_value() &&
                   records_match(*receipt.output_evidence, evidence.record)
               ? EvidenceDisposition::kDuplicate
               : EvidenceDisposition::kRejected;
  }
  if (!can_deliver_result(evidence.authority) ||
      has_reached_deadline(receipt, evidence.record.observed_at)) {
    return EvidenceDisposition::kRejected;
  }
  if (!receipt.native_success_evidence.has_value() ||
      evidence.record.observed_at < receipt.native_success_evidence->observed_at) {
    return EvidenceDisposition::kRejected;
  }
  receipt.output = OutputDisposition::kAccepted;
  receipt.result_reference = evidence.result_reference;
  receipt.output_evidence = evidence.record;
  state.refresh_authority_disposition();
  return EvidenceDisposition::kAccepted;
}

EvidenceDisposition
AuthorityGate::observe_output_not_delivered(const OutputNonDeliveryEvidence& evidence) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(evidence.authority)) {
    return EvidenceDisposition::kRejected;
  }

  const std::string* expected_source = nullptr;
  const EvidenceRecord* prerequisite = nullptr;
  switch (evidence.reason) {
  case OutputNonDeliveryReason::kNoOutputProduced:
    expected_source = &state.config.settlement_evidence_source;
    if (!evidence.result_reference.empty()) {
      return EvidenceDisposition::kRejected;
    }
    if (state.current_receipt->dispatch == DispatchStatus::kNotSubmitted) {
      prerequisite = state.current_receipt->non_submission_evidence
                         ? &*state.current_receipt->non_submission_evidence
                         : nullptr;
    } else if (state.current_receipt->native_acceptance == NativeAcceptance::kRejected) {
      prerequisite = state.current_receipt->native_rejection_evidence
                         ? &*state.current_receipt->native_rejection_evidence
                         : nullptr;
    } else if (state.current_receipt->native_acceptance == NativeAcceptance::kAccepted &&
               state.current_receipt->native_outcome == NativeOutcome::kFailed) {
      prerequisite = state.current_receipt->native_failure_evidence
                         ? &*state.current_receipt->native_failure_evidence
                         : nullptr;
    } else if (state.current_receipt->native_outcome == NativeOutcome::kCancelled) {
      prerequisite = state.current_receipt->native_cancellation_evidence
                         ? &*state.current_receipt->native_cancellation_evidence
                         : nullptr;
    }
    break;
  case OutputNonDeliveryReason::kAuthorityRevoked:
    if (!revocation_precedes(*state.current_receipt, evidence.record.observed_at)) {
      return EvidenceDisposition::kRejected;
    }
    [[fallthrough]];
  case OutputNonDeliveryReason::kSinkRejected:
    expected_source = &state.config.result_sink_observer;
    if (evidence.result_reference.empty() ||
        state.current_receipt->native_acceptance != NativeAcceptance::kAccepted ||
        state.current_receipt->native_outcome != NativeOutcome::kSucceeded) {
      return EvidenceDisposition::kRejected;
    }
    prerequisite = state.current_receipt->native_success_evidence
                       ? &*state.current_receipt->native_success_evidence
                       : nullptr;
    break;
  default:
    return EvidenceDisposition::kRejected;
  }
  if (prerequisite == nullptr ||
      !is_record_well_formed(evidence.record, *expected_source, prerequisite->observed_at)) {
    return EvidenceDisposition::kRejected;
  }

  auto& receipt = *state.current_receipt;
  if (receipt.output == OutputDisposition::kNotDelivered) {
    return receipt.output_non_delivery_reason == evidence.reason &&
                   receipt.result_reference == evidence.result_reference &&
                   receipt.output_non_delivery_evidence.has_value() &&
                   records_match(*receipt.output_non_delivery_evidence, evidence.record)
               ? EvidenceDisposition::kDuplicate
               : EvidenceDisposition::kRejected;
  }
  if (receipt.output != OutputDisposition::kPending ||
      !follows_same_source_records(evidence.record, receipt)) {
    return EvidenceDisposition::kRejected;
  }
  receipt.output = OutputDisposition::kNotDelivered;
  receipt.output_non_delivery_reason = evidence.reason;
  receipt.result_reference = evidence.result_reference;
  receipt.output_non_delivery_evidence = evidence.record;
  state.refresh_authority_disposition();
  return EvidenceDisposition::kAccepted;
}

EvidenceDisposition AuthorityGate::observe_settlement(const SettlementEvidence& evidence) {
  auto& state = *implementation_;
  if (!state.current_authority_matches(evidence.authority) ||
      evidence.scope != state.config.settlement_scope ||
      !is_record_well_formed(evidence.record, state.config.settlement_evidence_source,
                             evidence.authority.admitted_at)) {
    return EvidenceDisposition::kRejected;
  }
  auto& receipt = *state.current_receipt;
  if (receipt.settlement == SettlementStatus::kSettled) {
    return receipt.settlement_scope == evidence.scope && receipt.settlement_evidence.has_value() &&
                   records_match(*receipt.settlement_evidence, evidence.record) &&
                   evidence.permits_same_domain_admission
               ? EvidenceDisposition::kDuplicate
               : EvidenceDisposition::kRejected;
  }
  if (!evidence.permits_same_domain_admission || receipt.output == OutputDisposition::kPending) {
    return EvidenceDisposition::kRejected;
  }
  const auto previous_time = latest_effect_time(receipt);
  if (evidence.record.observed_at < previous_time ||
      !follows_same_source_records(evidence.record, receipt)) {
    return EvidenceDisposition::kRejected;
  }
  if (!state.has_resolved_effects(receipt)) {
    return EvidenceDisposition::kRejected;
  }
  receipt.settlement = SettlementStatus::kSettled;
  receipt.settlement_scope = evidence.scope;
  receipt.settlement_evidence = evidence.record;
  state.refresh_authority_disposition();
  return EvidenceDisposition::kAccepted;
}

std::optional<OperationReceipt> AuthorityGate::receipt(const OperationAuthority& authority) const {
  const auto& state = *implementation_;
  if (!state.current_authority_matches(authority)) {
    return std::nullopt;
  }
  return state.current_receipt;
}

void AuthorityGate::close_admission() noexcept {
  implementation_->initialization_state = InitializationState::kClosed;
}

}  // namespace robot_harness
