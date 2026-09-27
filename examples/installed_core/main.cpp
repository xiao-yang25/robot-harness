#include <robot_harness/authority_gate.hpp>

#include <iostream>

int main() {
  // This application has no backend observations. Core must not grant authority.
  robot_harness::AuthorityGateConfig config;
  config.binding = {"consumer", "demo-domain", 1, 1};
  config.capability_id = "demo.action";
  config.binding_observer = "binding-observer";
  config.worker_observer = "worker";
  config.result_sink_observer = "sink";
  config.native_evidence_source = "worker";
  config.settlement_evidence_source = "adapter";
  config.settlement_scope = "demo-scope";
  config.started_at = 10;
  robot_harness::AuthorityGate gate(config);
  const auto decision = gate.admit({"demo.action", "demo-domain", "request", "sink", 11});
  if (gate.startup_status().state != robot_harness::InitializationState::kRecoveryRequired ||
      decision.status != robot_harness::AdmissionStatus::kRecoveryRequired ||
      decision.authority.has_value()) {
    std::cerr << "Missing readiness must block admission\n";
    return 1;
  }
  gate.close_admission();
  std::cout << "Installed Core: missing readiness blocks admission; no work dispatched\n";
  return 0;
}
