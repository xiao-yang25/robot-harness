#include "robot_harness/authority_gate.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

namespace {
// This executable is single-threaded and isolates allocator injection from the
// other tests. Fail only during the withdrawal call, not setup or assertions.
std::ptrdiff_t allocations_before_failure = -1;

class FailAllocation {
public:
  explicit FailAllocation(std::ptrdiff_t after) {
    allocations_before_failure = after;
  }
  ~FailAllocation() {
    allocations_before_failure = -1;
  }
};

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}  // namespace

void* operator new(std::size_t size) {
  if (allocations_before_failure == 0) {
    throw std::bad_alloc();
  }
  if (allocations_before_failure > 0) {
    --allocations_before_failure;
  }
  if (void* result = std::malloc(size == 0 ? 1 : size)) {
    return result;
  }
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}

namespace {
using namespace robot_harness;

void withdrawal_can_retry_after_allocation_failure(std::ptrdiff_t after) {
  AuthorityGateConfig config;
  // Both identity strings exceed small-string storage. Exercise a failure in
  // the first allocation and after a partial authority copy has succeeded.
  config.binding = {std::string(100, 'p'), std::string(100, 'd'), 1, 1};
  config.capability_id = "compute";
  config.binding_observer = "binding";
  config.worker_observer = "worker";
  config.result_sink_observer = "sink";
  config.native_evidence_source = "worker";
  config.settlement_evidence_source = "adapter";
  config.settlement_scope = "scope";
  AuthorityGate gate(config);
  gate.observe_startup(
      {StartupEvidenceKind::kBindingIdleAndSettled, config.binding, {"binding", 1, 1}, true});
  gate.observe_startup({StartupEvidenceKind::kWorkerReady, config.binding, {"worker", 2, 1}, true});
  gate.observe_startup(
      {StartupEvidenceKind::kResultSinkReady, config.binding, {"sink", 3, 1}, true});
  require(gate.startup_status().state == InitializationState::kReady, "initial binding ready");
  OperationRequest request;
  request.capability_id = config.capability_id;
  request.effect_domain = config.binding.effect_domain;
  request.opaque_request_reference = "request";
  request.target_reference = "sink";
  request.requested_at = 4;
  const auto admission = gate.admit(request);
  require(admission.authority.has_value(), "operation admitted");
  const auto authority = *admission.authority;
  require(gate.claim_dispatch(authority, 5), "submission boundary claimed");
  require(
      gate.observe_native({authority, "native", NativeEventKind::kAccepted, {"worker", 6, 1}}) ==
              EvidenceDisposition::kAccepted &&
          gate.observe_native({authority, "native", NativeEventKind::kStarted, {"worker", 7, 2}}) ==
              EvidenceDisposition::kAccepted,
      "running operation reported before withdrawal");

  bool allocation_failed = false;
  {
    FailAllocation fail(after);
    try {
      gate.withdraw_binding(config.binding, 10);
    } catch (const std::bad_alloc&) {
      allocation_failed = true;
    }
  }
  require(allocation_failed, "authority-copy allocation failure injected");
  const auto receipt = gate.receipt(authority);
  require(gate.binding_status(10).phase == BindingPhase::kActive &&
              gate.startup_status().state == InitializationState::kReady && receipt &&
              !receipt->binding_withdrawn_at && !receipt->native_stop_requested_at,
          "failed withdrawal must not consume the pending stop or mutate binding state");

  const auto retry = gate.withdraw_binding(config.binding, 11);
  require(retry.status == BindingControlStatus::kApplied && retry.native_stop_authority &&
              retry.native_stop_authority->operation_id == authority.operation_id &&
              retry.native_stop_authority->binding == authority.binding &&
              retry.native_stop_authority->admitted_at == authority.admitted_at &&
              gate.receipt(authority)->native_stop_requested_at == 11,
          "retry must deliver the original operation's stop action");
  const auto repeated = gate.withdraw_binding(config.binding, 12);
  require(repeated.status == BindingControlStatus::kAlreadyInPhase &&
              !repeated.native_stop_authority &&
              !gate.request_cancel(authority, 13).should_request_native_stop,
          "successful stop delivery remains one-time across withdrawal and cancellation");
}
}  // namespace

int main() {
  try {
    withdrawal_can_retry_after_allocation_failure(0);
    withdrawal_can_retry_after_allocation_failure(1);
    std::cout << "withdrawal allocation failure and retry passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
