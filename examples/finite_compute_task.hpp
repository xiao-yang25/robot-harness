#ifndef ROBOT_HARNESS_EXAMPLES_FINITE_COMPUTE_TASK_HPP_
#define ROBOT_HARNESS_EXAMPLES_FINITE_COMPUTE_TASK_HPP_

#include "robot_harness/compute_execution.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace robot_harness::examples {

struct ComputeGoal {
  std::uint64_t revision = 0;
  std::uint64_t first_iterations = 0;
};
enum class TaskOutcome { kPending, kSucceeded, kFailed, kNeedsAttention, kCancelled };
struct TaskOperation {
  std::uint64_t goal_revision = 0;
  unsigned step = 1;
  OperationAuthority authority;
  std::string request_reference;
  std::uint64_t iterations = 0;
};

// Example-owned application logic, not a Harness API. The caller has exclusive
// use of an initialized host, which outlives this object. No threads or retries.
class FiniteComputeTask {
public:
  explicit FiniteComputeTask(ComputeExecutionHost& host,
                             MonotonicTime observation_budget_ms = 7000);
  FiniteComputeTask(const FiniteComputeTask&) = delete;
  FiniteComputeTask& operator=(const FiniteComputeTask&) = delete;

  bool revise_goal(ComputeGoal goal);
  // A nondecreasing caller observation clock, separate from the host's native clock.
  // Every call drives the host, including after failure/unknown and during shutdown.
  void tick(MonotonicTime caller_time);
  void begin_shutdown();

  const std::optional<ComputeGoal>& goal() const noexcept;
  const std::optional<TaskOperation>& operation() const noexcept;
  TaskOutcome outcome() const noexcept;
  const char* detail() const noexcept;
  unsigned step() const noexcept;
  std::uint64_t admitted_steps() const noexcept;
  std::optional<std::uint64_t> first_sum() const noexcept;
  std::optional<std::uint64_t> final_sum() const noexcept;

private:
  void needs_attention(const char* detail);
  bool observation_expired(MonotonicTime time) const noexcept;
  void submit_step(MonotonicTime time);

  ComputeExecutionHost& host_;
  const MonotonicTime observation_budget_ms_;
  std::optional<ComputeGoal> goal_;
  std::optional<TaskOperation> operation_;
  TaskOutcome outcome_ = TaskOutcome::kPending;
  const char* detail_ = "no goal";
  unsigned step_ = 1;
  std::uint64_t admitted_steps_ = 0;
  std::optional<std::uint64_t> first_sum_;
  std::optional<std::uint64_t> final_sum_;
  std::optional<MonotonicTime> waiting_since_;
  std::optional<MonotonicTime> last_tick_;
  bool closing_ = false;
};

}  // namespace robot_harness::examples
#endif  // ROBOT_HARNESS_EXAMPLES_FINITE_COMPUTE_TASK_HPP_
