#include "robot_harness/authority_gate.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace robot_harness;
void require(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
AuthorityGateConfig config() {
  AuthorityGateConfig result;
  result.binding = {"provider", "domain", 1, 1};
  result.capability_id = "compute";
  result.binding_observer = "adapter";
  result.worker_observer = "adapter";
  result.result_sink_observer = "sink";
  result.native_evidence_source = "native";
  result.settlement_evidence_source = "adapter";
  result.settlement_scope = "child-output-channels";
  result.execution_capability_observer = "capabilities";
  result.readiness_kind = StartupEvidenceKind::kAdapterReady;
  return result;
}
void initialize(AuthorityGate& gate) {
  const auto binding = config().binding;
  require(
      gate.observe_startup({StartupEvidenceKind::kWorkerReady, binding, {"adapter", 1, 1}, true}) ==
          EvidenceDisposition::kRejected,
      "worker ready must not substitute for adapter readiness");
  gate.observe_startup(
      {StartupEvidenceKind::kBindingIdleAndSettled, binding, {"adapter", 1, 1}, true});
  gate.observe_startup({StartupEvidenceKind::kAdapterReady, binding, {"adapter", 1, 2}, true});
  gate.observe_startup({StartupEvidenceKind::kResultSinkReady, binding, {"sink", 1, 1}, true});
  const auto status = gate.startup_status();
  require(status.state == InitializationState::kReady && status.is_adapter_ready &&
              !status.is_worker_ready,
          "prelaunch readiness does not claim an existing child");
}
OperationRequest request() {
  OperationRequest result;
  result.capability_id = "compute";
  result.effect_domain = "domain";
  result.opaque_request_reference = "request";
  result.target_reference = "sink";
  result.requested_at = 5;
  result.execution_requirements = ExecutionRequirements{"local-profile", 100};
  return result;
}
ExecutionCapabilities capabilities() {
  return {config().binding, "compute", "local-profile", 100, true, {"capabilities", 2, 1}, 10};
}
void match_and_expiry() {
  AuthorityGate gate(config());
  initialize(gate);
  auto operation = request();
  require(gate.admit(operation).status == AdmissionStatus::kCapabilitiesUnavailable,
          "missing capability");
  auto observed = capabilities();
  observed.binding.provider_generation = 2;
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kRejected,
          "wrong generation");
  observed = capabilities();
  observed.record.source = "untrusted";
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kRejected,
          "wrong observer");
  observed = capabilities();
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kAccepted,
          "observe");
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kDuplicate,
          "duplicate");
  observed.maximum_work_units = 101;
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kRejected,
          "mutated duplicate");
  operation.execution_requirements.reset();
  require(gate.admit(operation).status == AdmissionStatus::kCapabilitiesUnavailable,
          "cannot omit required policy");
  operation = request();
  operation.execution_requirements->profile_id = "hard-stop-100ms";
  require(gate.admit(operation).status == AdmissionStatus::kCapabilitiesUnavailable,
          "unknown stronger guarantee");
  operation = request();
  operation.execution_requirements->work_units = 101;
  require(gate.admit(operation).status == AdmissionStatus::kCapabilitiesUnavailable, "over budget");
  operation = request();
  operation.requested_at = 10;
  require(gate.admit(operation).status == AdmissionStatus::kCapabilitiesUnavailable,
          "exclusive validity bound");
  operation = request();
  const auto authority = *gate.admit(operation).authority;
  require(!gate.claim_dispatch(authority, 10), "expiry between admission and dispatch");
  observed = capabilities();
  observed.record = {"capabilities", 10, 2};
  observed.valid_until = 20;
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kAccepted,
          "fresh observation");
  require(gate.claim_dispatch(authority, 10), "dispatch with revalidated capabilities");
  require(gate.receipt(authority)->execution_requirements->work_units == 100,
          "retained consumed work");
}
void lost_capability_and_no_child() {
  AuthorityGate gate(config());
  initialize(gate);
  auto observed = capabilities();
  gate.observe_execution_capabilities(observed);
  const auto authority = *gate.admit(request()).authority;
  observed.available = false;
  observed.record = {"capabilities", 6, 2};
  require(gate.observe_execution_capabilities(observed) == EvidenceDisposition::kAccepted,
          "observe loss");
  require(!gate.claim_dispatch(authority, 6), "lost launch condition");
  require(gate.observe_non_submission({authority, {"adapter", 6, 3}}) ==
              EvidenceDisposition::kAccepted,
          "no child closure");
  require(gate.admit(request()).status == AdmissionStatus::kDomainOccupied,
          "no submission alone not settled");

  AuthorityGate failed_spawn(config());
  initialize(failed_spawn);
  failed_spawn.observe_execution_capabilities(capabilities());
  const auto second = *failed_spawn.admit(request()).authority;
  require(failed_spawn.claim_dispatch(second, 6), "claim before spawn");
  require(failed_spawn.observe_non_submission({second, {"adapter", 5, 3}}) ==
              EvidenceDisposition::kRejected,
          "no-child proof cannot predate claimed dispatch");
  require(failed_spawn.observe_non_submission({second, {"adapter", 6, 3}}) ==
              EvidenceDisposition::kAccepted,
          "proven no child after spawn error");
  require(failed_spawn.receipt(second)->dispatch == DispatchStatus::kNotSubmitted,
          "no fabricated submission");

  AuthorityGate running(config());
  initialize(running);
  running.observe_execution_capabilities(capabilities());
  const auto third = *running.admit(request()).authority;
  running.claim_dispatch(third, 5);
  running.observe_native({third, "child", NativeEventKind::kAccepted, {"native", 5, 1}});
  require(running.observe_non_submission({third, {"adapter", 6, 3}}) ==
              EvidenceDisposition::kRejected,
          "owned native work cannot become non-submission");
}
}  // namespace
int main() {
  try {
    match_and_expiry();
    lost_capability_and_no_child();
    std::cout << "compute capability admission checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
