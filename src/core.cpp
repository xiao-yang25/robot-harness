#include "robot_harness/authority_gate.hpp"

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

MonotonicTime latest_time(const OperationReceipt& receipt) {
  MonotonicTime time = receipt.authority.admitted_at;
  if (receipt.dispatched_at.has_value() && *receipt.dispatched_at > time) {
    time = *receipt.dispatched_at;
  }
  const std::optional<EvidenceRecord>* records[] = {&receipt.native_acceptance_evidence,
                                                    &receipt.native_started_evidence,
                                                    &receipt.native_success_evidence,
                                                    &receipt.native_failure_evidence,
                                                    &receipt.output_evidence,
                                                    &receipt.settlement_evidence};
  for (const auto* record : records) {
    if (record->has_value() && (*record)->observed_at > time) {
      time = (*record)->observed_at;
    }
  }
  return time;
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

  bool current_authority_matches(const OperationAuthority& authority) const {
    return current_receipt.has_value() && authority_matches(current_receipt->authority, authority);
  }

  bool current_is_releasable() const {
    return current_receipt.has_value() &&
           current_receipt->native_outcome == NativeOutcome::kSucceeded &&
           current_receipt->output == OutputDisposition::kAccepted &&
           current_receipt->settlement == SettlementStatus::kSettled &&
           current_receipt->authority_disposition == AuthorityDisposition::kReleased;
  }

  void refresh_authority_disposition() {
    if (!current_receipt.has_value()) {
      return;
    }
    if (current_receipt->native_outcome == NativeOutcome::kUnknown) {
      current_receipt->authority_disposition = AuthorityDisposition::kBlockedUnknown;
      return;
    }
    if (current_receipt->native_outcome == NativeOutcome::kSucceeded &&
        current_receipt->output == OutputDisposition::kAccepted &&
        current_receipt->settlement == SettlementStatus::kSettled) {
      current_receipt->authority_disposition = AuthorityDisposition::kReleased;
      return;
    }
    current_receipt->authority_disposition = AuthorityDisposition::kCurrent;
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
      value.settlement_scope.empty()) {
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
  status.is_worker_ready = state.worker_ready.observed && state.worker_ready.value;
  status.is_result_sink_ready = state.result_sink_ready.observed && state.result_sink_ready.value;
  if (state.binding_idle.observed) {
    status.binding_idle_and_settled = state.binding_idle.record;
  }
  if (state.worker_ready.observed) {
    status.worker_ready = state.worker_ready.record;
  }
  if (state.result_sink_ready.observed) {
    status.result_sink_ready = state.result_sink_ready.record;
  }
  return status;
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

  OperationAuthority authority;
  authority.operation_id = state.next_operation_id++;
  authority.binding = state.config.binding;
  authority.admitted_at = request.requested_at;

  OperationReceipt receipt;
  receipt.authority = authority;
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
      state.current_receipt->dispatch != DispatchStatus::kPending) {
    return false;
  }
  state.current_receipt->dispatch = DispatchStatus::kSubmitted;
  state.current_receipt->dispatched_at = observed_at;
  return true;
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
  case NativeEventKind::kStarted:
  case NativeEventKind::kTerminalSucceeded:
  case NativeEventKind::kTerminalFailed:
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
  if (receipt.native_identity.empty() && evidence.kind != NativeEventKind::kAccepted) {
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
  }

  if (receipt.observed_native_success && receipt.observed_native_failure) {
    receipt.native_outcome = NativeOutcome::kUnknown;
  } else if (receipt.observed_native_success) {
    receipt.native_outcome = NativeOutcome::kSucceeded;
  } else if (receipt.observed_native_failure) {
    receipt.native_outcome = NativeOutcome::kFailed;
  }
  state.last_native_record = evidence.record;
  state.last_native_kind = evidence.kind;
  state.refresh_authority_disposition();
  return disposition;
}

bool AuthorityGate::can_deliver_result(const OperationAuthority& authority) const {
  const auto& state = *implementation_;
  return state.current_authority_matches(authority) &&
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
  if (!can_deliver_result(evidence.authority)) {
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
  if (!evidence.permits_same_domain_admission ||
      receipt.native_outcome == NativeOutcome::kPending ||
      (receipt.native_outcome == NativeOutcome::kSucceeded &&
       receipt.output != OutputDisposition::kAccepted)) {
    return EvidenceDisposition::kRejected;
  }
  if (receipt.native_outcome == NativeOutcome::kSucceeded &&
      (!receipt.output_evidence.has_value() ||
       evidence.record.observed_at < receipt.output_evidence->observed_at)) {
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
