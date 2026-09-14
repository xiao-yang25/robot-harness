#include "robot_harness/authority_gate.hpp"

#include <cstdlib>
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

}  // namespace

int main() {
  startup_requires_fresh_matching_facts();
  normal_receipt_is_layered();
  correlation_ordering_and_conflicts_fail_closed();
  stale_previous_evidence_cannot_corrupt_current_operation();
  return 0;
}
