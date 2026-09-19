#include "finite_compute_task.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace robot_harness;
using namespace robot_harness::examples;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Predicate>
void pump_until(FiniteComputeTask& task, Predicate predicate, MonotonicTime offset = 0) {
  const auto limit = ComputeExecutionHost::now() + 7000;
  while (!predicate()) {
    require(ComputeExecutionHost::now() < limit, "bounded task observation timed out");
    task.tick(ComputeExecutionHost::now() + offset);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void close(FiniteComputeTask& task, ComputeExecutionHost& host, MonotonicTime offset = 0) {
  const auto admitted = task.admitted_steps();
  task.begin_shutdown();
  pump_until(
      task, [&] { return host.shutdown_status() == ComputeShutdownStatus::kClosed; }, offset);
  require(task.admitted_steps() == admitted, "shutdown cannot admit another step");
}

void dependent_result(const std::string& delayed) {
  ComputeExecutionHost host({delayed, 100000000, 2000});
  host.initialize();
  FiniteComputeTask task(host);
  require(task.revise_goal({1, 3}), "first goal accepted");
  task.tick(ComputeExecutionHost::now());
  const auto first = task.operation()->authority;
  pump_until(task, [&] { return host.observation().completed_iterations == 3; });
  require(!host.observation().cleanup_confirmed &&
              host.receipt(first)->authority_disposition != AuthorityDisposition::kReleased &&
              task.admitted_steps() == 1 && task.step() == 1 && !task.first_sum(),
          "computed data before actual native closure cannot start a dependent step");
  pump_until(task, [&] { return task.step() == 2; });
  require(task.first_sum() == 5 && task.admitted_steps() == 1 &&
              host.receipt(first)->output == OutputDisposition::kAccepted &&
              host.receipt(first)->authority_disposition == AuthorityDisposition::kReleased,
          "second input is selected only after accepted and settled first result");
  pump_until(task, [&] { return task.outcome() != TaskOutcome::kPending; });
  require(task.outcome() == TaskOutcome::kSucceeded && task.final_sum() == 55 &&
              task.operation()->iterations == 6 && task.admitted_steps() == 2 &&
              host.receipt(task.operation()->authority)->domain_verdict ==
                  DomainVerdict::kUnassessed,
          "independently known two-step result is an application verdict only");
  close(task, host);
}

void revised_running_goal(const std::string& normal) {
  ComputeExecutionHost host({normal, 100000000, 2000});
  host.initialize();
  FiniteComputeTask task(host);
  require(task.revise_goal({1, 100000000}), "long first goal accepted");
  task.tick(ComputeExecutionHost::now());
  const auto old = task.operation()->authority;
  pump_until(task, [&] { return host.observation().completed_iterations > 0; });
  require(!host.observation().cleanup_confirmed, "revision exercises actually running work");
  require(task.revise_goal({2, 3}) && task.revise_goal({3, 4}) && !task.revise_goal({3, 7}),
          "latest increasing revision wins; duplicate revision rejected");
  require(task.operation()->goal_revision == 1 && task.admitted_steps() == 1,
          "new intent does not relabel or replace outstanding authority");
  pump_until(task, [&] { return !task.operation(); });
  require(host.receipt(old)->authority_disposition == AuthorityDisposition::kReleased &&
              host.receipt(old)->output != OutputDisposition::kAccepted && !task.first_sum() &&
              task.admitted_steps() == 1,
          "new goal waits for actual release and cannot inherit discarded output");
  pump_until(task, [&] { return task.outcome() != TaskOutcome::kPending; });
  require(task.outcome() == TaskOutcome::kSucceeded && task.first_sum() == 14 &&
              task.final_sum() == 91 && task.admitted_steps() == 3 &&
              task.operation()->goal_revision == 3 && task.operation()->iterations == 7,
          "only latest goal executes two steps with independently expected results");
  close(task, host);
}

void unresolved_observation(const std::string& delayed) {
  ComputeExecutionHost host({delayed, 100000000, 2000});
  host.initialize();
  FiniteComputeTask task(host);
  task.revise_goal({1, 3});
  task.tick(ComputeExecutionHost::now());
  const auto old = task.operation()->authority;
  pump_until(task, [&] { return host.observation().completed_iterations == 3; });
  require(!host.observation().cleanup_confirmed, "fixture still owns a live child");
  task.tick(ComputeExecutionHost::now() + 7001);
  require(task.outcome() == TaskOutcome::kNeedsAttention && !task.revise_goal({2, 4}) &&
              !host.observation().cleanup_confirmed && !task.final_sum() &&
              host.receipt(old)->cancellation_requested_at,
          "uncertain caller window stops progression without asserting native closure");
  pump_until(task, [&] { return host.observation().cleanup_confirmed; }, 7001);
  require(task.outcome() == TaskOutcome::kNeedsAttention && task.admitted_steps() == 1 &&
              host.receipt(old)->authority_disposition == AuthorityDisposition::kReleased &&
              !task.revise_goal({2, 4}),
          "continued ticks drive cleanup but do not silently retry or clear uncertainty");
  close(task, host, 7001);
}

void failure_and_shutdown(const std::string& normal, const std::string& delayed,
                          const std::string& failing) {
  {
    ComputeExecutionHost host({failing, 100000000, 2000});
    host.initialize();
    FiniteComputeTask task(host);
    task.revise_goal({1, 3});
    pump_until(task, [&] { return task.outcome() != TaskOutcome::kPending; });
    require(task.outcome() == TaskOutcome::kFailed && !task.first_sum(),
            "native failure stops the dependent goal");
    pump_until(task, [&] { return host.observation().cleanup_confirmed; });
    const auto old = task.operation()->authority;
    host.withdraw_binding(old.binding);
    require(host.rebind_provider(old.binding, "replacement", normal).status ==
                BindingControlStatus::kApplied,
            "external recovery installs a usable provider");
    task.tick(ComputeExecutionHost::now());
    require(task.outcome() == TaskOutcome::kFailed && task.admitted_steps() == 1,
            "provider readiness does not implicitly retry a failed goal");
    close(task, host);
  }
  for (const bool between_steps : {false, true}) {
    ComputeExecutionHost host({delayed, 100000000, 2000});
    host.initialize();
    FiniteComputeTask task(host);
    task.revise_goal({1, 3});
    task.tick(ComputeExecutionHost::now());
    pump_until(task, [&] {
      return between_steps ? task.step() == 2 : host.observation().completed_iterations == 3;
    });
    if (!between_steps) {
      require(!host.observation().cleanup_confirmed, "shutdown starts with live native ownership");
    }
    task.begin_shutdown();
    require(task.outcome() == TaskOutcome::kCancelled && !task.revise_goal({2, 4}),
            "shutdown immediately cancels task intent and rejects new goals");
    close(task, host);
    require(task.admitted_steps() == 1 && !task.final_sum(),
            "shutdown prevents dependent submission");
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 5, "usage: task_tests CASE NORMAL DELAYED FAILING");
    const std::string mode = argv[1];
    if (mode == "dependency") {
      dependent_result(argv[3]);
    } else if (mode == "revision") {
      revised_running_goal(argv[2]);
    } else if (mode == "uncertainty") {
      unresolved_observation(argv[3]);
    } else if (mode == "termination") {
      failure_and_shutdown(argv[2], argv[3], argv[4]);
    } else {
      throw std::runtime_error("unknown test case");
    }
    std::cout << "finite task " << mode << " passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
