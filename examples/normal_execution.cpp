#include "robot_harness/sample_execution.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

const char* outcome_name(robot_harness::NativeOutcome outcome) {
  switch (outcome) {
  case robot_harness::NativeOutcome::kPending:
    return "pending";
  case robot_harness::NativeOutcome::kSucceeded:
    return "succeeded";
  case robot_harness::NativeOutcome::kFailed:
    return "failed";
  case robot_harness::NativeOutcome::kUnknown:
    return "unknown";
  }
  return "unknown";
}

bool run_sample(robot_harness::SampleExecutionHost& host,
                const robot_harness::SampleRequest& request,
                robot_harness::CompletionMode completion_mode, const char* mode_name) {
  const auto submission = host.submit(request);
  if (submission.status != robot_harness::SampleSubmissionStatus::kSubmitted ||
      !submission.authority.has_value()) {
    return false;
  }
  if (completion_mode == robot_harness::CompletionMode::kDeferred) {
    if (!host.complete_deferred()) {
      return false;
    }
    host.process_all_staged_events();
  }
  const auto receipt = host.receipt(*submission.authority);
  if (!receipt.has_value() || host.results().empty()) {
    return false;
  }
  const auto& result = host.results().back();
  std::cout << mode_name << ' ' << request.request_reference
            << ": operation=" << receipt->authority.operation_id << " accepted=yes"
            << " native=" << outcome_name(receipt->native_outcome) << " output="
            << (receipt->output == robot_harness::OutputDisposition::kAccepted ? "accepted"
                                                                               : "pending")
            << " settled="
            << (receipt->settlement == robot_harness::SettlementStatus::kSettled ? "yes" : "no")
            << " domain_verdict=unassessed"
            << " sum_of_squares=" << result.sum_of_squares << '\n';
  return receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded &&
         receipt->output == robot_harness::OutputDisposition::kAccepted &&
         receipt->settlement == robot_harness::SettlementStatus::kSettled;
}

bool run_mode(robot_harness::CompletionMode completion_mode, const char* mode_name,
              robot_harness::MonotonicTime start_time) {
  robot_harness::MonotonicTime time = start_time;
  robot_harness::SampleExecutionHost host(completion_mode, [&time] { return time++; });
  if (host.initialize().state != robot_harness::InitializationState::kReady) {
    return false;
  }
  if (!run_sample(host, {"sample-a", {2, -3, 4}}, completion_mode, mode_name)) {
    return false;
  }
  if (!run_sample(host, {"sample-b", {5, 6}}, completion_mode, mode_name)) {
    return false;
  }
  host.shutdown();
  return true;
}

}  // namespace

int main() {
  return run_mode(robot_harness::CompletionMode::kSynchronous, "synchronous", 100) &&
                 run_mode(robot_harness::CompletionMode::kDeferred, "deferred", 200)
             ? 0
             : 1;
}
