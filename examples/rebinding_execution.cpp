#include "robot_harness/sample_execution.hpp"

#include <iostream>

int main() {
  using namespace robot_harness;
  MonotonicTime now = 100;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now++; });
  if (host.initialize().state != InitializationState::kReady) {
    return 1;
  }
  const auto submitted = host.submit({"old-task", {3, 4}});
  if (!submitted.authority) {
    return 1;
  }
  const auto old = *submitted.authority;
  if (host.withdraw_binding(old.binding).status != BindingControlStatus::kApplied ||
      host.rebind_provider(old.binding, "replacement-worker", CompletionMode::kSynchronous)
              .status != BindingControlStatus::kUnresolvedWork) {
    return 1;
  }
  std::cout << "Old provider withdrawn; replacement blocked until cleanup\n";
  host.complete_deferred();
  host.process_all_staged_events();
  if (host.rebind_provider(old.binding, "replacement-worker", CompletionMode::kSynchronous)
          .status != BindingControlStatus::kApplied) {
    return 1;
  }
  const auto replacement = host.submit({"new-task", {5, 6}});
  if (!replacement.authority || host.results().size() != 1 ||
      host.results()[0].sum_of_squares != 61) {
    return 1;
  }
  std::cout << "New provider: " << replacement.authority->binding.provider_id
            << ", generation: " << replacement.authority->binding.provider_generation
            << ", result: " << host.results()[0].sum_of_squares << '\n';
  host.shutdown();
}
