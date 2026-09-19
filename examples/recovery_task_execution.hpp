#ifndef ROBOT_HARNESS_EXAMPLES_RECOVERY_TASK_EXECUTION_HPP_
#define ROBOT_HARNESS_EXAMPLES_RECOVERY_TASK_EXECUTION_HPP_
#include "finite_compute_task.hpp"
#include "recovery_client.hpp"

namespace robot_harness::examples {
// Linux prototype bridge. Client outlives this port; only one task drives it.
// Recovery is explicit outside the task. No per-operation cancellation protocol:
// interruption closes the whole session and cannot report a cancellation ACK.
class RecoveryTaskExecution final : public TaskExecution {
public:
  explicit RecoveryTaskExecution(detail::recovery::Client& client) : client_(client) {
  }
  void poll() override;
  BindingPhase phase() const override;
  ComputeSubmission submit(std::uint64_t iterations, const std::string& reference) override;
  bool request_cancel(const OperationAuthority& authority) override;
  std::optional<OperationReceipt> receipt(const OperationAuthority& authority) const override;
  const std::optional<ComputeResult>& result() const override {
    return result_;
  }
  bool ownership_lost() const override;
  void begin_shutdown() override;

private:
  detail::recovery::Client& client_;
  std::optional<ComputeResult> pending_result_;
  std::optional<ComputeResult> result_;
};
}  // namespace robot_harness::examples
#endif
