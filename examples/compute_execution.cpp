#include "robot_harness/compute_execution.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
  using namespace robot_harness;
  if (argc != 2) {
    std::cerr
        << "usage: robot_harness_compute_execution /absolute/path/robot_harness_compute_worker\n";
    return 2;
  }
  ComputeExecutionHost host({argv[1], 100000000, 1000});
  if (host.initialize().state != InitializationState::kReady) {
    std::cerr << "worker launch conditions unavailable\n";
    return 1;
  }
  auto submission = host.submit({"sum-1003", 1003, std::nullopt, kLocalComputeProfile});
  if (!submission.authority || submission.status != ComputeSubmissionStatus::kSubmitted)
    return 1;
  const auto observation_deadline = ComputeExecutionHost::now() + 7000;
  while (host.receipt(*submission.authority)->settlement != SettlementStatus::kSettled) {
    if (ComputeExecutionHost::now() >= observation_deadline) {
      host.begin_shutdown();
      std::cerr << "observation timed out; destructor fallback has no stopping-time guarantee\n";
      return 1;
    }
    host.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!host.result() || host.result()->sum != 332833505)
    return 1;
  std::cout << "sum=" << host.result()->sum << " exit=" << *host.observation().exit_code
            << " cleanup=" << host.observation().cleanup_confirmed << '\n';
  host.begin_shutdown();
  return host.shutdown_status() == ComputeShutdownStatus::kClosed ? 0 : 1;
}
