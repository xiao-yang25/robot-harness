#include "robot_harness/authority_gate.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace robot_harness;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

AuthorityGateConfig config(bool require_capabilities = false) {
  AuthorityGateConfig result;
  result.binding = {"old-provider", "domain", 1, 1};
  result.capability_id = "sample.square";
  result.binding_observer = "binding";
  result.worker_observer = "worker";
  result.result_sink_observer = "sink";
  result.native_evidence_source = "worker";
  result.settlement_evidence_source = "adapter";
  result.settlement_scope = "fixture";
  if (require_capabilities) {
    result.execution_capability_observer = "capabilities";
  }
  return result;
}

void initialize(AuthorityGate& gate, const BindingIdentity& binding) {
  require(gate.observe_startup(
              {StartupEvidenceKind::kBindingIdleAndSettled, binding, {"binding", 10, 1}, true}) ==
              EvidenceDisposition::kAccepted,
          "initial old-scope idle accepted");
  require(
      gate.observe_startup({StartupEvidenceKind::kWorkerReady, binding, {"worker", 11, 1}, true}) ==
          EvidenceDisposition::kAccepted,
      "initial worker ready accepted");
  require(gate.observe_startup(
              {StartupEvidenceKind::kResultSinkReady, binding, {"sink", 12, 1}, true}) ==
              EvidenceDisposition::kAccepted,
          "initial sink ready accepted");
}

OperationRequest request(MonotonicTime at, const char* reference = "request") {
  OperationRequest result;
  result.capability_id = "sample.square";
  result.effect_domain = "domain";
  result.opaque_request_reference = reference;
  result.target_reference = "sink";
  result.requested_at = at;
  return result;
}

BindingReadinessEvidence old_scope(const BindingIdentity& old, MonotonicTime at,
                                   std::uint64_t sequence, MonotonicTime valid_until = 200) {
  return {StartupEvidenceKind::kBindingIdleAndSettled,
          old,
          {"binding", at, sequence},
          true,
          valid_until};
}

BindingReadinessEvidence readiness(const BindingIdentity& candidate, StartupEvidenceKind kind,
                                   const char* source, MonotonicTime at, std::uint64_t sequence,
                                   bool observed = true, MonotonicTime valid_until = 200) {
  return {kind, candidate, {source, at, sequence}, observed, valid_until};
}

BindingIdentity prepare(AuthorityGate& gate, const BindingIdentity& old, MonotonicTime evidence_at,
                        MonotonicTime begin_at, std::uint64_t sequence = 2) {
  const auto decision =
      gate.begin_rebind(old, "new-provider", old_scope(old, evidence_at, sequence), begin_at);
  require(decision.status == BindingControlStatus::kApplied && decision.candidate.has_value(),
          "fresh old-scope closure prepares a candidate");
  return *decision.candidate;
}

void ready(AuthorityGate& gate, const BindingIdentity& candidate, MonotonicTime first_at,
           MonotonicTime valid_until = 200) {
  require(gate.observe_rebind_readiness(
              readiness(candidate, StartupEvidenceKind::kBindingIdleAndSettled, "binding", first_at,
                        1, true, valid_until)) == EvidenceDisposition::kAccepted,
          "candidate idle accepted");
  require(gate.observe_rebind_readiness(readiness(candidate, StartupEvidenceKind::kWorkerReady,
                                                  "worker", first_at + 1, 1, true, valid_until)) ==
              EvidenceDisposition::kAccepted,
          "candidate worker accepted");
  require(gate.observe_rebind_readiness(readiness(candidate, StartupEvidenceKind::kResultSinkReady,
                                                  "sink", first_at + 2, 1, true, valid_until)) ==
              EvidenceDisposition::kAccepted,
          "candidate sink accepted");
}

void no_prior_operation_still_needs_fresh_old_scope() {
  AuthorityGate gate(config());
  const auto old = config().binding;
  require(gate.binding_status(12).phase == BindingPhase::kActive,
          "binding starts active before startup observations");
  require(gate.admit(request(12)).status == AdmissionStatus::kRecoveryRequired,
          "active binding without startup readiness grants no operation");
  initialize(gate, old);
  require(gate.withdraw_binding(old, 20).status == BindingControlStatus::kApplied,
          "idle binding withdrawn");
  require(gate.admit(request(21)).status == AdmissionStatus::kRecoveryRequired,
          "withdrawn binding remains unavailable");
  require(gate.begin_rebind(old, "new-provider", old_scope(old, 10, 1), 22).status ==
              BindingControlStatus::kInvalidEvidence,
          "initial idle fact cannot substitute for post-withdrawal proof");
  auto invalid = old_scope(old, 21, 2);
  invalid.binding.provider_generation++;
  require(gate.begin_rebind(old, "new-provider", invalid, 22).status ==
              BindingControlStatus::kInvalidEvidence,
          "wrong old binding cannot prove quiescence");
  invalid = old_scope(old, 21, 2);
  invalid.record.source = "worker";
  require(gate.begin_rebind(old, "new-provider", invalid, 22).status ==
              BindingControlStatus::kInvalidEvidence,
          "wrong observer cannot prove quiescence");
  require(gate.begin_rebind(old, "new-provider", old_scope(old, 21, 2, 22), 22).status ==
              BindingControlStatus::kInvalidEvidence,
          "old-scope proof expires at its exclusive bound");
  const auto candidate = prepare(gate, old, 21, 22);
  require(!gate.binding_status(22).can_commit &&
              gate.admit(request(23)).status == AdmissionStatus::kRecoveryRequired,
          "preparation alone does not grant admission");
  require(gate.observe_rebind_readiness(readiness(old, StartupEvidenceKind::kWorkerReady, "worker",
                                                  23, 1)) == EvidenceDisposition::kRejected,
          "old-binding readiness cannot fill candidate slot");
  require(gate.observe_rebind_readiness(
              readiness(candidate, StartupEvidenceKind::kWorkerReady, "worker", 21, 1)) ==
              EvidenceDisposition::kRejected,
          "candidate readiness must follow preparation");
  require(gate.commit_rebind(candidate, 23).status == BindingControlStatus::kInvalidEvidence,
          "missing candidate readiness blocks commit");
}

void revoked_undispatched_operation_closes_without_native_stop() {
  AuthorityGate gate(config());
  const auto old = config().binding;
  initialize(gate, old);
  const auto admission = gate.admit(request(13, "old-request"));
  require(admission.status == AdmissionStatus::kAdmitted && admission.authority.has_value(),
          "old operation admitted");
  const auto old_authority = *admission.authority;
  const auto withdrawn = gate.withdraw_binding(old, 14);
  require(withdrawn.status == BindingControlStatus::kApplied &&
              !withdrawn.native_stop_authority.has_value(),
          "undispatched work needs no native stop");
  require(!gate.claim_dispatch(old_authority, 15), "withdrawal prevents native submission");
  require(gate.begin_rebind(old, "new-provider", old_scope(old, 18, 2), 19).status ==
              BindingControlStatus::kUnresolvedWork,
          "a ready candidate cannot erase pending old cleanup");
  require(gate.observe_non_submission({old_authority, {"adapter", 15, 1}}) ==
              EvidenceDisposition::kAccepted,
          "adapter confirms no native submission");
  require(
      gate.observe_output_not_delivered(
          {old_authority, OutputNonDeliveryReason::kNoOutputProduced, {}, {"adapter", 16, 2}}) ==
          EvidenceDisposition::kAccepted,
      "adapter confirms no old output");
  require(gate.begin_rebind(old, "new-provider", old_scope(old, 18, 2), 19).status ==
              BindingControlStatus::kUnresolvedWork,
          "missing settlement still blocks candidate");
  require(gate.observe_settlement({old_authority, "fixture", true, {"adapter", 17, 3}}) ==
              EvidenceDisposition::kAccepted,
          "old cleanup settles in its original scope");
  require(gate.receipt(old_authority)->authority_disposition == AuthorityDisposition::kReleased,
          "old authority releases only after all closure facts");
  const auto candidate = prepare(gate, old, 18, 19);
  ready(gate, candidate, 20);
  require(gate.commit_rebind(candidate, 23).status == BindingControlStatus::kApplied,
          "settled old work and fresh candidate facts commit");
  const auto before = *gate.receipt(old_authority);
  require(gate.request_cancel(old_authority, 24).status == ControlStatus::kNoActiveOperation &&
              gate.observe_native(
                  {old_authority, "old", NativeEventKind::kAccepted, {"worker", 24, 1}}) ==
                  EvidenceDisposition::kRejected &&
              gate.observe_settlement({old_authority, "fixture", true, {"adapter", 24, 4}}) ==
                  EvidenceDisposition::kRejected,
          "old controls and evidence cannot mutate a committed handoff");
  const auto after = *gate.receipt(old_authority);
  require(before.authority_disposition == after.authority_disposition &&
              before.dispatch == after.dispatch && before.native_outcome == after.native_outcome &&
              before.output == after.output && before.settlement == after.settlement,
          "retained old receipt remains unchanged until next admission");
  const auto next = gate.admit(request(24, "new-request"));
  require(next.status == AdmissionStatus::kAdmitted && next.authority.has_value() &&
              next.authority->operation_id > old_authority.operation_id &&
              next.authority->binding == candidate,
          "new operation keeps the operation sequence and uses new binding identity");
  require(gate.claim_dispatch(*next.authority, 25), "new operation can dispatch");
  require(gate.observe_native(
              {*next.authority, "new", NativeEventKind::kAccepted, {"worker", 26, 1}}) ==
              EvidenceDisposition::kAccepted,
          "new provider accepts work");
  require(gate.observe_native(
              {*next.authority, "new", NativeEventKind::kTerminalSucceeded, {"worker", 27, 2}}) ==
              EvidenceDisposition::kAccepted,
          "new provider completes work");
  require(gate.can_deliver_result(*next.authority),
          "new result has permission at the actual delivery boundary");
  require(gate.observe_output_accepted({old_authority, "stale-result", {"sink", 28, 1}}) ==
              EvidenceDisposition::kRejected,
          "old result cannot cross new operation's eligible delivery boundary");
  require(gate.observe_output_accepted({*next.authority, "new-result", {"sink", 28, 1}}) ==
              EvidenceDisposition::kAccepted,
          "new result remains deliverable after stale result refusal");
}

void accepted_old_work_gets_one_stop_and_still_needs_settlement() {
  AuthorityGate gate(config());
  const auto old = config().binding;
  initialize(gate, old);
  auto timed_request = request(13);
  timed_request.deadline = 25;
  const auto authority = *gate.admit(timed_request).authority;
  require(gate.claim_dispatch(authority, 14), "old operation claims dispatch");
  require(gate.observe_native(
              {authority, "old-child", NativeEventKind::kAccepted, {"worker", 15, 1}}) ==
              EvidenceDisposition::kAccepted,
          "native old work is accepted");
  const auto withdrawn = gate.withdraw_binding(old, 16);
  require(withdrawn.status == BindingControlStatus::kApplied &&
              withdrawn.native_stop_authority.has_value() &&
              withdrawn.native_stop_authority->operation_id == authority.operation_id,
          "withdrawal issues one stop for the original operation");
  require(!gate.request_cancel(authority, 17).should_request_native_stop &&
              !gate.observe_time(authority, 25).should_request_native_stop &&
              !gate.withdraw_binding(old, 26).native_stop_authority.has_value(),
          "cancel, expiry and repeated withdrawal never issue another stop");
  require(gate.begin_rebind(old, "new-provider", old_scope(old, 29, 2), 30).status ==
              BindingControlStatus::kUnresolvedWork,
          "new provider cannot replace still-owned old native work");
  require(gate.observe_native(
              {authority, "old-child", NativeEventKind::kTerminalCancelled, {"worker", 27, 2}}) ==
              EvidenceDisposition::kAccepted,
          "old native terminal remains observable after withdrawal");
  require(gate.observe_output_not_delivered(
              {authority, OutputNonDeliveryReason::kNoOutputProduced, {}, {"adapter", 28, 1}}) ==
              EvidenceDisposition::kAccepted,
          "old output is resolved without delivery");
  require(gate.begin_rebind(old, "new-provider", old_scope(old, 30, 2), 31).status ==
              BindingControlStatus::kUnresolvedWork,
          "native terminal and output still require settlement");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 29, 2}}) ==
              EvidenceDisposition::kAccepted,
          "old native owner settles after terminal and output resolution");
  const auto candidate = prepare(gate, old, 30, 31);
  require(candidate.provider_generation > old.provider_generation,
          "candidate receives a fresh generation");
}

void candidate_ordering_and_capabilities_fail_closed() {
  AuthorityGate gate(config(true));
  const auto old = config(true).binding;
  initialize(gate, old);
  require(gate.withdraw_binding(old, 20).status == BindingControlStatus::kApplied,
          "capability fixture withdraws old binding");
  const auto candidate = prepare(gate, old, 21, 22);
  auto idle = readiness(candidate, StartupEvidenceKind::kBindingIdleAndSettled, "binding", 23, 1);
  auto worker = readiness(candidate, StartupEvidenceKind::kWorkerReady, "worker", 24, 1, false);
  auto sink = readiness(candidate, StartupEvidenceKind::kResultSinkReady, "sink", 25, 1);
  require(gate.observe_rebind_readiness(idle) == EvidenceDisposition::kAccepted &&
              gate.observe_rebind_readiness(worker) == EvidenceDisposition::kAccepted &&
              gate.observe_rebind_readiness(sink) == EvidenceDisposition::kAccepted,
          "candidate tracks positive and negative readiness separately");
  require(gate.commit_rebind(candidate, 26).status == BindingControlStatus::kInvalidEvidence,
          "false readiness and missing capabilities block commit");
  worker.observed = true;
  worker.record = {"wrong-source", 27, 2};
  require(gate.observe_rebind_readiness(worker) == EvidenceDisposition::kRejected,
          "wrong source cannot repair negative readiness");
  worker.record = {"worker", 27, 2};
  worker.valid_until = 31;
  require(gate.observe_rebind_readiness(worker) == EvidenceDisposition::kAccepted,
          "newer worker fact repairs negative readiness");
  ExecutionCapabilities caps{candidate, "sample.square",         "profile", 100,
                             true,      {"capabilities", 28, 1}, 200};
  auto wrong_caps = caps;
  wrong_caps.binding = old;
  require(gate.observe_execution_capabilities(wrong_caps) == EvidenceDisposition::kAccepted &&
              gate.commit_rebind(candidate, 28).status == BindingControlStatus::kInvalidEvidence,
          "old capabilities stay separate and cannot fill candidate slot");
  require(gate.observe_execution_capabilities(caps) == EvidenceDisposition::kAccepted,
          "candidate capabilities accepted");
  idle.observed = false;
  require(gate.observe_rebind_readiness(idle) == EvidenceDisposition::kRejected &&
              gate.commit_rebind(candidate, 29).status == BindingControlStatus::kInvalidEvidence,
          "equal-sequence contradictory readiness poisons positive fact");
  idle.observed = true;
  idle.record = {"binding", 30, 2};
  require(gate.observe_rebind_readiness(idle) == EvidenceDisposition::kAccepted,
          "newer idle observation resolves contradiction");
  caps.available = false;
  require(gate.observe_execution_capabilities(caps) == EvidenceDisposition::kRejected &&
              gate.commit_rebind(candidate, 30).status == BindingControlStatus::kInvalidEvidence,
          "equal-sequence contradictory capabilities poison positive fact");
  require(gate.commit_rebind(candidate, 31).status == BindingControlStatus::kInvalidEvidence,
          "expired worker readiness blocks commit at exclusive validity bound");
  worker.record = {"worker", 32, 3};
  worker.valid_until = 200;
  require(gate.observe_rebind_readiness(worker) == EvidenceDisposition::kAccepted,
          "fresh worker evidence repairs expiry");
  caps.available = true;
  caps.record = {"capabilities", 33, 2};
  require(gate.observe_execution_capabilities(caps) == EvidenceDisposition::kAccepted,
          "newer capabilities resolve contradiction");
  sink.observed = false;
  sink.record = {"sink", 34, 2};
  require(gate.observe_rebind_readiness(sink) == EvidenceDisposition::kAccepted &&
              gate.commit_rebind(candidate, 35).status == BindingControlStatus::kInvalidEvidence,
          "newer negative sink fact supersedes earlier positive");
  sink.observed = true;
  sink.record = {"sink", 36, 3};
  require(gate.observe_rebind_readiness(sink) == EvidenceDisposition::kAccepted &&
              gate.binding_status(37).can_commit &&
              gate.commit_rebind(candidate, 37).status == BindingControlStatus::kApplied,
          "newer valid prerequisites permit exactly one commit");
  require(gate.commit_rebind(candidate, 38).status == BindingControlStatus::kAlreadyInPhase &&
              gate.binding_status(38).active == candidate,
          "repeat commit retains active generation");
  auto capability_request = request(38);
  capability_request.execution_requirements = ExecutionRequirements{"profile", 90};
  require(gate.admit(capability_request).status == AdmissionStatus::kAdmitted,
          "candidate capabilities become active at commit");
}

void discarded_candidates_and_closed_gate_never_reopen() {
  AuthorityGate gate(config());
  const auto old = config().binding;
  initialize(gate, old);
  gate.withdraw_binding(old, 20);
  const auto first = prepare(gate, old, 21, 22);
  require(gate.withdraw_binding(old, 23).status == BindingControlStatus::kStaleIdentity &&
              gate.binding_status(23).candidate == first,
          "old identity cannot abort a candidate");
  require(gate.begin_rebind(old, "other-provider", old_scope(old, 23, 3), 23).status ==
              BindingControlStatus::kAlreadyInPhase,
          "repeated begin preserves the existing candidate");
  require(gate.withdraw_binding(first, 24).status == BindingControlStatus::kApplied,
          "candidate identity explicitly aborts preparation");
  const auto second = prepare(gate, old, 25, 26, 3);
  require(second.provider_generation > first.provider_generation &&
              gate.observe_rebind_readiness(
                  readiness(first, StartupEvidenceKind::kWorkerReady, "worker", 27, 1)) ==
                  EvidenceDisposition::kRejected,
          "discarded candidate identity and evidence cannot be reused");
  require(gate.withdraw_binding(first, 27).status == BindingControlStatus::kStaleIdentity &&
              gate.commit_rebind(first, 27).status == BindingControlStatus::kStaleIdentity,
          "stale candidate controls cannot affect current preparation");
  require(gate.withdraw_binding(second, 25).status == BindingControlStatus::kInvalidTime,
          "backward control time is refused");
  gate.close_admission();
  require(gate.binding_status(28).phase == BindingPhase::kClosed &&
              gate.withdraw_binding(second, 28).status == BindingControlStatus::kClosed &&
              gate.begin_rebind(old, "third", old_scope(old, 28, 4), 29).status ==
                  BindingControlStatus::kClosed &&
              gate.commit_rebind(second, 29).status == BindingControlStatus::kClosed &&
              gate.admit(request(30)).status == AdmissionStatus::kClosed,
          "permanent shutdown cannot be reopened by binding controls");

  auto exhausted_config = config();
  exhausted_config.binding.provider_generation = std::numeric_limits<std::uint64_t>::max();
  AuthorityGate exhausted(exhausted_config);
  initialize(exhausted, exhausted_config.binding);
  exhausted.withdraw_binding(exhausted_config.binding, 20);
  require(exhausted.begin_rebind(exhausted_config.binding, "new-provider",
                                 old_scope(exhausted_config.binding, 21, 2), 22)
                      .status == BindingControlStatus::kIdentityExhausted &&
              exhausted.binding_status(22).phase == BindingPhase::kWithdrawn,
          "generation exhaustion refuses wraparound and leaves admission closed");
}

void old_scope_validity_is_rechecked_at_commit() {
  AuthorityGate gate(config());
  const auto old = config().binding;
  initialize(gate, old);
  gate.withdraw_binding(old, 20);
  const auto begun = gate.begin_rebind(old, "new", old_scope(old, 21, 2, 30), 22);
  require(begun.candidate.has_value(), "prepare with unexpired old-scope evidence");
  ready(gate, *begun.candidate, 23);
  require(gate.binding_status(29).can_commit && !gate.binding_status(30).can_commit &&
              gate.commit_rebind(*begun.candidate, 30).status ==
                  BindingControlStatus::kInvalidEvidence &&
              gate.admit(request(30)).status == AdmissionStatus::kRecoveryRequired,
          "fresh candidate facts cannot outlive old-scope quiescence evidence");
  gate.withdraw_binding(*begun.candidate, 30);
  const auto next = prepare(gate, old, 31, 32, 3);
  ready(gate, next, 33);
  require(next.provider_generation > begun.candidate->provider_generation &&
              gate.commit_rebind(next, 36).status == BindingControlStatus::kApplied,
          "refresh requires a new candidate and new old-scope evidence");
}

}  // namespace

int main() {
  try {
    no_prior_operation_still_needs_fresh_old_scope();
    revoked_undispatched_operation_closes_without_native_stop();
    accepted_old_work_gets_one_stop_and_still_needs_settlement();
    candidate_ordering_and_capabilities_fail_closed();
    discarded_candidates_and_closed_gate_never_reopen();
    old_scope_validity_is_rechecked_at_commit();
    std::cout << "M3b Core rebind checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "FAILED: " << error.what() << '\n';
    return 1;
  }
}
