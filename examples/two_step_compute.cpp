#include "finite_compute_task.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  using namespace robot_harness;
  using namespace robot_harness::examples;
  if (argc != 3) {
    std::cerr << "usage: robot_harness_two_step_compute MODE WORKER\n"
              << "MODE: normal, revise, rebind, failure, unknown, shutdown\n";
    return 2;
  }
  const std::string mode = argv[1];
  if (mode != "normal" && mode != "revise" && mode != "rebind" && mode != "failure" &&
      mode != "unknown" && mode != "shutdown") {
    return 2;
  }
  ComputeExecutionHost host({argv[2], 100000000, 2000});
  if (host.initialize().state != InitializationState::kReady) {
    std::cerr << "worker launch prerequisites unavailable\n";
    return 1;
  }
  FiniteComputeTask task(host);
  const bool changes_goal = mode == "revise" || mode == "rebind";
  const auto first_iterations = changes_goal || mode == "shutdown" ? 100000000U
                                : mode == "unknown"                ? 1003U
                                                                   : 3U;
  task.revise_goal({1, first_iterations});
  task.tick(ComputeExecutionHost::now());
  if (!task.operation()) {
    return 1;
  }
  const auto old = task.operation()->authority;
  if (changes_goal) {
    if (!task.revise_goal({2, 4})) {
      return 1;
    }
    if (mode == "rebind" &&
        host.withdraw_binding(old.binding).status != BindingControlStatus::kApplied) {
      return 1;
    }
  }
  if (mode == "shutdown") {
    task.begin_shutdown();
  }

  MonotonicTime observation_offset = 0;
  const auto wall_limit = ComputeExecutionHost::now() + 10000;
  const char* last_detail = nullptr;
  bool rebound = false;
  while ((task.outcome() == TaskOutcome::kPending) ||
         (mode == "shutdown" && host.shutdown_status() == ComputeShutdownStatus::kClosing)) {
    if (ComputeExecutionHost::now() >= wall_limit) {
      task.begin_shutdown();
      std::cerr << "driver observation limit reached; cleanup is not confirmed\n";
      return 1;
    }
    if (mode == "unknown" && observation_offset == 0 &&
        host.observation().completed_iterations == 1003 && !host.observation().cleanup_confirmed) {
      observation_offset = 7001;
      std::cout << "Advance the caller observation clock; native time is unchanged\n";
    }
    task.tick(ComputeExecutionHost::now() + observation_offset);
    if (mode == "rebind" && !rebound && host.receipt(old) &&
        host.receipt(old)->authority_disposition == AuthorityDisposition::kReleased) {
      rebound = host.rebind_provider(old.binding, "task-replacement", argv[2]).status ==
                BindingControlStatus::kApplied;
      if (!rebound) {
        task.begin_shutdown();
        return 1;
      }
    }
    if (last_detail != task.detail()) {
      std::cout << "goal=" << task.goal()->revision << " step=" << task.step() << " "
                << task.detail() << '\n';
      last_detail = task.detail();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto expected = mode == "failure"    ? TaskOutcome::kFailed
                        : mode == "unknown"  ? TaskOutcome::kNeedsAttention
                        : mode == "shutdown" ? TaskOutcome::kCancelled
                                             : TaskOutcome::kSucceeded;
  bool verified = task.outcome() == expected;
  if (expected == TaskOutcome::kSucceeded) {
    verified = verified && task.first_sum() == (changes_goal ? 14U : 5U) &&
               task.final_sum() == (changes_goal ? 91U : 55U) && (mode != "rebind" || rebound);
    if (verified) {
      std::cout << "first=" << *task.first_sum() << " final=" << *task.final_sum() << '\n';
    }
  }
  const auto admitted = task.admitted_steps();
  task.begin_shutdown();
  while (host.shutdown_status() == ComputeShutdownStatus::kClosing &&
         ComputeExecutionHost::now() < wall_limit) {
    task.tick(ComputeExecutionHost::now() + observation_offset);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  verified = verified && host.shutdown_status() == ComputeShutdownStatus::kClosed &&
             task.admitted_steps() == admitted;
  std::cout << "cleanup=" << (host.shutdown_status() == ComputeShutdownStatus::kClosed)
            << " admitted_steps=" << task.admitted_steps() << '\n';
  return verified ? 0 : 1;
}
