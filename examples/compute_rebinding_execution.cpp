#include "robot_harness/compute_execution.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace {
template <typename Predicate>
bool drive_until(robot_harness::ComputeExecutionHost& host, Predicate predicate) {
  const auto end = robot_harness::ComputeExecutionHost::now() + 7000;
  while (!predicate()) {
    if (robot_harness::ComputeExecutionHost::now() >= end) {
      host.begin_shutdown();
      std::cerr << "observation timed out; shutdown still requires native cleanup\n";
      return false;
    }
    host.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  using namespace robot_harness;
  if (argc != 3) {
    std::cerr << "usage: robot_harness_compute_rebinding_execution OLD_WORKER NEW_WORKER\n";
    return 2;
  }
  ComputeExecutionHost host({argv[1], 100000000, 1000});
  if (host.initialize().state != InitializationState::kReady) {
    return 1;
  }
  const auto old = host.submit({"old-task", 100000000});
  if (!old.authority || old.status != ComputeSubmissionStatus::kSubmitted) {
    return 1;
  }
  const auto old_binding = old.authority->binding;
  if (host.withdraw_binding(old_binding).status != BindingControlStatus::kApplied ||
      host.rebind_provider(old_binding, "replacement-compute", argv[2]).status !=
          BindingControlStatus::kUnresolvedWork) {
    return 1;
  }
  std::cout << "Provider withdrawn; rebind waits for child exit, reaping and channel cleanup\n";
  if (!drive_until(host, [&] {
        return host.receipt(*old.authority)->settlement == SettlementStatus::kSettled;
      })) {
    return 1;
  }
  if (!host.observation().cleanup_confirmed || host.result() ||
      host.rebind_provider(old_binding, "replacement-compute", argv[2]).status !=
          BindingControlStatus::kApplied) {
    return 1;
  }
  const auto fresh = host.submit({"new-task", 1003});
  if (!fresh.authority || fresh.status != ComputeSubmissionStatus::kSubmitted ||
      !drive_until(host,
                   [&] {
                     return host.receipt(*fresh.authority)->settlement ==
                            SettlementStatus::kSettled;
                   }) ||
      !host.result() || host.result()->sum != 332833505) {
    return 1;
  }
  std::cout << "Same domain: " << fresh.authority->binding.effect_domain
            << "; new generation: " << fresh.authority->binding.provider_generation
            << "; sum: " << host.result()->sum << '\n';
  host.begin_shutdown();
  return drive_until(host, [&] { return host.shutdown_status() == ComputeShutdownStatus::kClosed; })
             ? 0
             : 1;
}
