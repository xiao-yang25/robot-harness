#include "robot_harness/authority_gate.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

robot_harness::AuthorityGateConfig config() {
  return {{"provider", "domain", 1, 1},
          "sample.square",
          "binding-observer",
          "worker",
          "sink",
          "worker",
          "adapter",
          "fixture",
          10};
}

void initialize(robot_harness::AuthorityGate& gate) {
  const robot_harness::BindingIdentity binding{"provider", "domain", 1, 1};
  require(gate.observe_startup({robot_harness::StartupEvidenceKind::kBindingIdleAndSettled,
                                binding,
                                {"binding-observer", 10, 1},
                                true}) == robot_harness::EvidenceDisposition::kAccepted,
          "binding observation accepted");
  require(
      gate.observe_startup(
          {robot_harness::StartupEvidenceKind::kWorkerReady, binding, {"worker", 11, 1}, true}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "worker observation accepted");
  require(
      gate.observe_startup(
          {robot_harness::StartupEvidenceKind::kResultSinkReady, binding, {"sink", 12, 1}, true}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "sink observation accepted");
}

robot_harness::OperationAuthority admit_and_dispatch(robot_harness::AuthorityGate& gate,
                                                     const char* request_reference,
                                                     robot_harness::MonotonicTime requested_at) {
  const auto admission =
      gate.admit({"sample.square", "domain", request_reference, "sink", requested_at});
  require(admission.status == robot_harness::AdmissionStatus::kAdmitted,
          "ready gate admits operation");
  require(admission.authority.has_value() && admission.authority->is_valid(),
          "Core allocates valid authority");
  require(gate.claim_dispatch(*admission.authority, requested_at + 1),
          "current authority claims one dispatch");
  return *admission.authority;
}

void startup_requires_fresh_matching_facts() {
  robot_harness::AuthorityGate gate(config());
  const robot_harness::BindingIdentity binding{"provider", "domain", 1, 1};
  require(gate.startup_status().state == robot_harness::InitializationState::kRecoveryRequired,
          "every gate starts recovery required");
  require(gate.observe_startup({robot_harness::StartupEvidenceKind::kBindingIdleAndSettled,
                                binding,
                                {"wrong-source", 10, 1},
                                true}) == robot_harness::EvidenceDisposition::kRejected,
          "wrong startup source rejected");
  require(gate.observe_startup({static_cast<robot_harness::StartupEvidenceKind>(99),
                                binding,
                                {"worker", 10, 1},
                                true}) == robot_harness::EvidenceDisposition::kRejected,
          "unknown startup evidence rejected");
  require(gate.observe_startup({robot_harness::StartupEvidenceKind::kBindingIdleAndSettled,
                                binding,
                                {"binding-observer", 10, 1},
                                true}) == robot_harness::EvidenceDisposition::kAccepted,
          "fresh binding evidence accepted");
  require(
      gate.observe_startup(
          {robot_harness::StartupEvidenceKind::kWorkerReady, binding, {"worker", 11, 1}, false}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "observed not-ready state accepted");
  require(
      gate.observe_startup(
          {robot_harness::StartupEvidenceKind::kResultSinkReady, binding, {"sink", 12, 1}, true}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "fresh sink evidence accepted");
  require(gate.startup_status().state == robot_harness::InitializationState::kRecoveryRequired,
          "one false prerequisite keeps admission closed");
  require(gate.startup_status().worker_ready.has_value() &&
              gate.startup_status().worker_ready->source == "worker" &&
              !gate.startup_status().is_worker_ready,
          "startup receipt retains the negative fact source and value");
  require(gate.admit({"sample.square", "domain", "request", "sink", 13}).status ==
              robot_harness::AdmissionStatus::kRecoveryRequired,
          "host activity cannot replace readiness");
  require(
      gate.observe_startup(
          {robot_harness::StartupEvidenceKind::kWorkerReady, binding, {"worker", 14, 2}, true}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "new worker-ready observation accepted");
  require(gate.startup_status().state == robot_harness::InitializationState::kReady,
          "all current prerequisites open admission");
  require(gate.admit({"wrong-capability", "domain", "request", "sink", 15}).status ==
              robot_harness::AdmissionStatus::kInvalidRequest,
          "invalid request does not consume authority");
  const auto authority = admit_and_dispatch(gate, "request", 15);
  require(authority.operation_id == 1, "rejected requests do not allocate operation identities");
}

void normal_receipt_is_layered() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto authority = admit_and_dispatch(gate, "request-a", 13);
  require(!gate.claim_dispatch(authority, 14), "one authority cannot claim a second dispatch");
  require(
      gate.observe_native(
          {authority, "native-a", robot_harness::NativeEventKind::kAccepted, {"worker", 15, 1}}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "native acceptance recorded");
  require(gate.observe_native({authority,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalSucceeded,
                               {"worker", 16, 2}}) == robot_harness::EvidenceDisposition::kAccepted,
          "native terminal recorded");
  require(gate.observe_output_accepted({authority, "result-a", {"sink", 17, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "managed output recorded separately");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 18, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "settlement recorded separately");
  const auto receipt = gate.receipt(authority);
  require(receipt.has_value(), "current receipt available");
  require(receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded,
          "native outcome succeeded");
  require(receipt->output == robot_harness::OutputDisposition::kAccepted, "output accepted");
  require(receipt->settlement == robot_harness::SettlementStatus::kSettled, "settlement complete");
  require(receipt->domain_verdict == robot_harness::DomainVerdict::kUnassessed,
          "domain verdict remains unassessed");
}

void correlation_ordering_and_conflicts_fail_closed() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto authority = admit_and_dispatch(gate, "request-a", 13);
  require(gate.observe_native({authority,
                               "attempt-a",
                               robot_harness::NativeEventKind::kRejected,
                               {"wrong-source", 1000, 1000}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "native rejection requires the configured native source");
  require(
      gate.observe_native(
          {authority, "native-a", robot_harness::NativeEventKind::kAccepted, {"worker", 15, 1}}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "native acceptance recorded");

  auto wrong_authority = authority;
  wrong_authority.operation_id += 100;
  require(gate.observe_native({wrong_authority,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalSucceeded,
                               {"worker", 1000, 1000}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "wrong operation evidence rejected");
  require(gate.observe_native({authority,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalSucceeded,
                               {"worker", 16, 2}}) == robot_harness::EvidenceDisposition::kAccepted,
          "invalid unrelated evidence does not advance the current evidence clock");
  require(gate.observe_native({authority,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalSucceeded,
                               {"worker", 16, 2}}) ==
              robot_harness::EvidenceDisposition::kDuplicate,
          "duplicate terminal evidence is idempotent");
  require(gate.observe_native({authority,
                               "native-a",
                               static_cast<robot_harness::NativeEventKind>(99),
                               {"worker", 1001, 1001}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "unknown native event rejected");
  require(gate.observe_output_accepted({authority, "result-a", {"sink", 15, 1}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "output observed before terminal is rejected");
  require(gate.observe_output_accepted({authority, "result-a", {"sink", 17, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "ordered managed output accepted");
  require(gate.admit({"sample.square", "domain", "request-b", "sink", 18}).status ==
              robot_harness::AdmissionStatus::kDomainOccupied,
          "pending settlement blocks conflicting admission");
  require(gate.observe_settlement({authority, "wrong-scope", true, {"adapter", 18, 1}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "settlement for another scope cannot release the domain");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 16, 1}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "settlement before result acceptance is rejected");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 18, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "ordered matching settlement accepted");

  require(gate.observe_native({authority,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalFailed,
                               {"worker", 19, 3}}) == robot_harness::EvidenceDisposition::kAccepted,
          "conflicting valid terminal evidence remains observable");
  const auto conflicted = gate.receipt(authority);
  require(conflicted->native_outcome == robot_harness::NativeOutcome::kUnknown,
          "conflicting terminal facts produce unknown native outcome");
  require(conflicted->observed_native_success && conflicted->observed_native_failure,
          "receipt preserves both terminal facts");
  require(conflicted->authority_disposition == robot_harness::AuthorityDisposition::kBlockedUnknown,
          "conflict closes new admission despite earlier settlement");
  require(gate.admit({"sample.square", "domain", "request-b", "sink", 20}).status ==
              robot_harness::AdmissionStatus::kDomainOccupied,
          "conflicted current receipt blocks the domain");
}

void stale_previous_evidence_cannot_corrupt_current_operation() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto first = admit_and_dispatch(gate, "request-a", 13);
  require(gate.observe_native(
              {first, "native-a", robot_harness::NativeEventKind::kAccepted, {"worker", 15, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "first native acceptance recorded");
  require(gate.observe_native({first,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalSucceeded,
                               {"worker", 16, 2}}) == robot_harness::EvidenceDisposition::kAccepted,
          "first native completion recorded");
  require(gate.observe_output_accepted({first, "result-a", {"sink", 17, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "first output accepted");
  require(gate.observe_settlement({first, "fixture", true, {"adapter", 18, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "first operation settled");

  const auto second = admit_and_dispatch(gate, "request-b", 19);
  require(second.operation_id == first.operation_id + 1,
          "sequential operation gets a new identity");
  require(gate.observe_native({first,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalFailed,
                               {"worker", 21, 3}}) == robot_harness::EvidenceDisposition::kRejected,
          "old operation terminal is stale after next admission");
  const auto receipt = gate.receipt(second);
  require(receipt.has_value() && receipt->native_outcome == robot_harness::NativeOutcome::kPending,
          "stale evidence leaves current receipt unchanged");
}

void non_submission_requires_correlated_cleanup() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto admission = gate.admit({"sample.square", "domain", "request-a", "sink", 13});
  require(admission.status == robot_harness::AdmissionStatus::kAdmitted &&
              admission.authority.has_value(),
          "ready gate admits operation before adapter refusal");
  const auto authority = *admission.authority;

  auto wrong_authority = authority;
  wrong_authority.operation_id += 100;
  require(gate.observe_non_submission({wrong_authority, {"adapter", 1000, 1000}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "non-submission for another operation is rejected");
  require(gate.observe_non_submission({authority, {"wrong-source", 1000, 1000}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "non-submission requires the configured adapter source");
  require(gate.observe_non_submission({authority, {"adapter", 14, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "correlated adapter non-submission is recorded");
  require(!gate.claim_dispatch(authority, 15),
          "closed non-submission authority can never claim dispatch");
  auto receipt = gate.receipt(authority);
  require(receipt->dispatch == robot_harness::DispatchStatus::kNotSubmitted &&
              receipt->native_outcome == robot_harness::NativeOutcome::kNotExecuted,
          "receipt distinguishes non-submission from native execution");
  require(gate.admit({"sample.square", "domain", "request-b", "sink", 15}).status ==
              robot_harness::AdmissionStatus::kDomainOccupied,
          "non-submission alone does not release the domain");

  require(gate.observe_output_not_delivered(
              {authority,
               robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
               {},
               {"wrong-source", 1000, 1000}}) == robot_harness::EvidenceDisposition::kRejected,
          "no-output evidence requires the configured adapter source");
  require(gate.observe_output_not_delivered(
              {authority,
               robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
               {},
               {"adapter", 13, 2}}) == robot_harness::EvidenceDisposition::kRejected,
          "causally premature no-output evidence is rejected");
  require(gate.observe_output_not_delivered(
              {authority,
               robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
               {},
               {"adapter", 15, 2}}) == robot_harness::EvidenceDisposition::kAccepted,
          "adapter resolves the absent output after non-submission");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 14, 3}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "settlement cannot precede output resolution");
  require(gate.observe_settlement({authority, "fixture", true, {"wrong-source", 1000, 1000}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "settlement requires the configured adapter source");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 16, 3}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "matching cleanup settles the non-submitted operation");
  receipt = gate.receipt(authority);
  require(receipt->output == robot_harness::OutputDisposition::kNotDelivered &&
              receipt->settlement == robot_harness::SettlementStatus::kSettled &&
              receipt->authority_disposition == robot_harness::AuthorityDisposition::kReleased,
          "complete non-submission closure releases authority");

  const auto second = gate.admit({"sample.square", "domain", "request-b", "sink", 17});
  require(second.status == robot_harness::AdmissionStatus::kAdmitted,
          "caller can submit a new operation after closure");
  require(!gate.claim_dispatch(authority, 18),
          "old authority remains unusable after new admission");
  require(gate.observe_output_accepted({authority, "old-result", {"sink", 18, 1}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "old result cannot be delivered into the new operation");
}

void closure_evidence_requires_increasing_same_source_sequences() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto admitted = gate.admit({"sample.square", "domain", "request-a", "sink", 13});
  require(admitted.authority.has_value(), "sequence fixture admits an operation");
  const auto authority = *admitted.authority;
  const robot_harness::NonSubmissionEvidence not_submitted{authority, {"adapter", 14, 20}};
  require(gate.observe_non_submission(not_submitted) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "non-submission establishes the adapter sequence");

  for (const auto sequence : {19U, 20U}) {
    require(gate.observe_output_not_delivered(
                {authority,
                 robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
                 {},
                 {"adapter", 14, sequence}}) == robot_harness::EvidenceDisposition::kRejected,
            "older or reused adapter sequence cannot establish no output at equal time");
  }
  require(gate.observe_output_not_delivered(
              {authority,
               robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
               {},
               {"adapter", 15, 19}}) == robot_harness::EvidenceDisposition::kRejected,
          "later time cannot repair a regressing sequence");
  require(gate.receipt(authority)->output == robot_harness::OutputDisposition::kPending,
          "invalid ordering does not mutate output state");
  require(gate.admit({"sample.square", "domain", "blocked-b", "sink", 16}).status ==
              robot_harness::AdmissionStatus::kDomainOccupied,
          "invalid output evidence leaves conflicting admission blocked");

  const robot_harness::OutputNonDeliveryEvidence no_output{
      authority,
      robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
      {},
      {"adapter", 14, 21}};
  require(gate.observe_output_not_delivered(no_output) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "increasing sequence allows equal observation times");
  require(gate.observe_output_not_delivered(no_output) ==
              robot_harness::EvidenceDisposition::kDuplicate,
          "exact output replay is idempotent");
  require(gate.observe_non_submission(not_submitted) ==
              robot_harness::EvidenceDisposition::kDuplicate,
          "exact earlier fact remains a duplicate after subsequent evidence");
  for (const auto sequence : {18U, 21U}) {
    require(gate.observe_settlement({authority, "fixture", true, {"adapter", 14, sequence}}) ==
                robot_harness::EvidenceDisposition::kRejected,
            "older or reused adapter sequence cannot settle output");
  }
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 15, 20}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "later settlement time cannot repair a regressing sequence");
  require(gate.receipt(authority)->settlement == robot_harness::SettlementStatus::kPending &&
              gate.admit({"sample.square", "domain", "blocked-b", "sink", 16}).status ==
                  robot_harness::AdmissionStatus::kDomainOccupied,
          "invalid settlement leaves the operation occupied");
  const robot_harness::SettlementEvidence settled{authority, "fixture", true, {"adapter", 14, 22}};
  require(gate.observe_settlement(settled) == robot_harness::EvidenceDisposition::kAccepted,
          "valid cleanup remains acceptable after rejected observations");
  require(gate.observe_settlement(settled) == robot_harness::EvidenceDisposition::kDuplicate &&
              gate.observe_output_not_delivered(no_output) ==
                  robot_harness::EvidenceDisposition::kDuplicate,
          "exact replays after settlement remain idempotent");
  require(gate.admit({"sample.square", "domain", "request-b", "sink", 16}).status ==
              robot_harness::AdmissionStatus::kAdmitted,
          "only ordered cleanup permits the next operation");
}

void native_rejection_is_not_acceptance_or_execution() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto authority = admit_and_dispatch(gate, "request-a", 13);
  require(
      gate.observe_native(
          {authority, "attempt-a", robot_harness::NativeEventKind::kRejected, {"worker", 15, 1}}) ==
          robot_harness::EvidenceDisposition::kAccepted,
      "native rejection records the attempted submission identity");
  auto receipt = gate.receipt(authority);
  require(receipt->dispatch == robot_harness::DispatchStatus::kSubmitted &&
              receipt->native_acceptance == robot_harness::NativeAcceptance::kRejected &&
              !receipt->native_started &&
              receipt->native_outcome == robot_harness::NativeOutcome::kNotExecuted,
          "attempt, acceptance and execution remain distinguishable");
  require(
      gate.observe_native(
          {authority, "attempt-a", robot_harness::NativeEventKind::kAccepted, {"worker", 16, 2}}) ==
          robot_harness::EvidenceDisposition::kRejected,
      "accepted evidence cannot overwrite definitive native rejection");
  require(gate.admit({"sample.square", "domain", "request-b", "sink", 16}).status ==
              robot_harness::AdmissionStatus::kDomainOccupied,
          "native rejection alone keeps the domain occupied");
  require(gate.observe_output_not_delivered(
              {authority,
               robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
               {},
               {"adapter", 16, 1}}) == robot_harness::EvidenceDisposition::kAccepted,
          "adapter resolves output after native rejection");
  require(gate.observe_settlement({authority, "fixture", true, {"adapter", 17, 2}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "cleanup settles the rejected attempt");
  receipt = gate.receipt(authority);
  require(receipt->native_outcome == robot_harness::NativeOutcome::kNotExecuted &&
              !receipt->observed_native_success && !receipt->observed_native_failure &&
              receipt->authority_disposition == robot_harness::AuthorityDisposition::kReleased,
          "rejection closure does not fabricate a native terminal outcome");
}

void failed_and_undelivered_successes_use_the_same_closure_gate() {
  robot_harness::AuthorityGate gate(config());
  initialize(gate);
  const auto failed = admit_and_dispatch(gate, "request-a", 13);
  require(gate.observe_native(
              {failed, "native-a", robot_harness::NativeEventKind::kAccepted, {"worker", 15, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "failed operation was first accepted");
  require(gate.observe_native({failed,
                               "native-a",
                               robot_harness::NativeEventKind::kRejected,
                               {"worker", 1000, 1000}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "native rejection cannot overwrite a trusted acceptance");
  require(gate.observe_native({failed,
                               "native-a",
                               robot_harness::NativeEventKind::kTerminalFailed,
                               {"worker", 16, 2}}) == robot_harness::EvidenceDisposition::kAccepted,
          "actual native failure is recorded");
  require(gate.observe_settlement({failed, "fixture", true, {"adapter", 17, 1}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "terminal failure cannot bypass unresolved output");
  require(gate.observe_output_not_delivered({failed,
                                             robot_harness::OutputNonDeliveryReason::kSinkRejected,
                                             "invented-result",
                                             {"sink", 17, 1}}) ==
              robot_harness::EvidenceDisposition::kRejected,
          "native failure cannot be relabeled as a sink rejection");
  require(gate.observe_output_not_delivered(
              {failed,
               robot_harness::OutputNonDeliveryReason::kNoOutputProduced,
               {},
               {"adapter", 17, 1}}) == robot_harness::EvidenceDisposition::kAccepted,
          "native failure resolves as no output produced");
  require(gate.observe_settlement({failed, "fixture", true, {"adapter", 18, 2}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "failed operation releases only after settlement");

  const auto undelivered = admit_and_dispatch(gate, "request-b", 19);
  require(gate.observe_native({undelivered,
                               "native-b",
                               robot_harness::NativeEventKind::kAccepted,
                               {"worker", 21, 1}}) == robot_harness::EvidenceDisposition::kAccepted,
          "second operation accepted");
  require(gate.observe_native({undelivered,
                               "native-b",
                               robot_harness::NativeEventKind::kTerminalSucceeded,
                               {"worker", 22, 2}}) == robot_harness::EvidenceDisposition::kAccepted,
          "second operation succeeds natively");
  require(gate.observe_output_not_delivered({undelivered,
                                             robot_harness::OutputNonDeliveryReason::kSinkRejected,
                                             "result-b",
                                             {"sink", 23, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "actual sink rejection is distinct from native success");
  require(gate.observe_settlement({undelivered, "fixture", true, {"adapter", 24, 1}}) ==
              robot_harness::EvidenceDisposition::kAccepted,
          "discarded output and cleanup release the domain");
  auto receipt = gate.receipt(undelivered);
  require(receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded &&
              receipt->output == robot_harness::OutputDisposition::kNotDelivered &&
              receipt->output_non_delivery_reason ==
                  robot_harness::OutputNonDeliveryReason::kSinkRejected,
          "receipt preserves native success and delivery failure");
  require(gate.observe_native({undelivered,
                               "native-b",
                               robot_harness::NativeEventKind::kTerminalFailed,
                               {"worker", 25, 3}}) == robot_harness::EvidenceDisposition::kAccepted,
          "later conflicting terminal remains observable");
  receipt = gate.receipt(undelivered);
  require(receipt->native_outcome == robot_harness::NativeOutcome::kUnknown &&
              receipt->authority_disposition ==
                  robot_harness::AuthorityDisposition::kBlockedUnknown,
          "terminal conflict blocks even after earlier settlement evidence");
}

}  // namespace

int main() {
  startup_requires_fresh_matching_facts();
  normal_receipt_is_layered();
  correlation_ordering_and_conflicts_fail_closed();
  stale_previous_evidence_cannot_corrupt_current_operation();
  non_submission_requires_correlated_cleanup();
  closure_evidence_requires_increasing_same_source_sequences();
  native_rejection_is_not_acceptance_or_execution();
  failed_and_undelivered_successes_use_the_same_closure_gate();
  return 0;
}
