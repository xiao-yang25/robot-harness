#include "robot_harness/sample_execution.hpp"

#include <cstdint>
#include <iostream>

int main() {
  robot_harness::MonotonicTime time = 100;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  if (host.initialize().state != robot_harness::InitializationState::kReady) {
    return 1;
  }

  host.fail_next_native_execution();
  const auto failed = host.submit({"controlled-failure", {2, -3}});
  if (failed.status != robot_harness::SampleSubmissionStatus::kSubmitted ||
      !failed.authority.has_value() || !host.complete_deferred() ||
      !host.process_next_staged_event() || !host.process_next_staged_event()) {
    return 1;
  }
  auto receipt = host.receipt(*failed.authority);
  if (!receipt.has_value() || receipt->native_outcome != robot_harness::NativeOutcome::kFailed ||
      receipt->settlement != robot_harness::SettlementStatus::kPending) {
    return 1;
  }
  if (host.submit({"blocked-before-settlement", {4}}).status !=
      robot_harness::SampleSubmissionStatus::kAdmissionBlocked) {
    return 1;
  }
  std::cout << "operation " << failed.authority->operation_id
            << " failed natively; next request remains blocked until cleanup\n";

  host.process_all_staged_events();
  const auto recovered = host.submit({"caller-selected-next-operation", {4}});
  if (recovered.status != robot_harness::SampleSubmissionStatus::kSubmitted ||
      !recovered.authority.has_value() || !host.complete_deferred()) {
    return 1;
  }
  host.process_all_staged_events();
  if (host.results().size() != 1 || host.results()[0].sum_of_squares != 16) {
    return 1;
  }
  std::cout << "fresh operation " << recovered.authority->operation_id
            << " succeeded after settlement; result=16\n";
  return 0;
}
