#ifndef ROBOT_HARNESS_SAMPLE_EXECUTION_HPP_
#define ROBOT_HARNESS_SAMPLE_EXECUTION_HPP_

#include "robot_harness/authority_gate.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace robot_harness {

struct SampleRequest {
  std::string request_reference;
  std::vector<std::int32_t> values;
  std::optional<MonotonicTime> deadline;
};

struct SampleResult {
  OperationId operation_id = 0;
  std::string request_reference;
  std::vector<std::int64_t> squared_values;
  std::int64_t sum_of_squares = 0;
};

enum class CompletionMode { kSynchronous, kDeferred };
// Fixture behavior after a stop request; acknowledgement never completes the work.
enum class CancellationMode { kCooperative, kIgnore, kRefuse, kUnavailable, kNoAcknowledgement };
enum class SampleSubmissionStatus {
  kPrepared,
  kSubmitted,
  kInvalidArgument,
  kAdmissionBlocked,
  kDispatchBlocked
};

struct SampleSubmission {
  SampleSubmissionStatus status = SampleSubmissionStatus::kInvalidArgument;
  AdmissionStatus admission_status = AdmissionStatus::kInvalidRequest;
  std::optional<OperationAuthority> authority;
};

class SampleExecutionHost {
public:
  // The clock target must outlive the host and return nondecreasing values without throwing.
  using Clock = std::function<MonotonicTime()>;

  explicit SampleExecutionHost(CompletionMode completion_mode, Clock clock);
  ~SampleExecutionHost();

  SampleExecutionHost(const SampleExecutionHost&) = delete;
  SampleExecutionHost& operator=(const SampleExecutionHost&) = delete;
  SampleExecutionHost(SampleExecutionHost&&) = delete;
  SampleExecutionHost& operator=(SampleExecutionHost&&) = delete;

  StartupStatus initialize();
  void set_worker_ready(bool is_ready);
  void set_result_sink_ready(bool is_ready);
  void reject_next_native_submission() noexcept;
  void fail_next_native_execution() noexcept;

  void set_cancellation_mode(CancellationMode mode) noexcept;
  // Suppress cleanup evidence to exercise a missing report, even during shutdown.
  void set_settlement_reporting_enabled(bool enabled) noexcept;
  // Fixture-only duplicate delivery: clear the slot, then retain the first result
  // generated while enabled. Disabled by default; disabling/shutdown clears it.
  void set_result_replay_enabled(bool enabled) noexcept;
  // Stage the retained callback as the next event to exercise out-of-order delivery.
  // Preserve its original authority and data; false if no retained result or closed.
  bool replay_retained_result();
  std::size_t result_permission_denial_count() const noexcept;

  SampleSubmission prepare(const SampleRequest& request);
  bool dispatch_prepared(const OperationAuthority& authority);
  SampleSubmission submit(const SampleRequest& request);
  ControlDecision request_cancel(const OperationAuthority& authority);
  ControlDecision poll();
  std::size_t native_stop_request_count() const noexcept;
  bool complete_deferred();
  bool process_next_staged_event();
  void process_all_staged_events();
  std::size_t staged_event_count() const noexcept;

  std::optional<OperationReceipt> receipt(const OperationAuthority& authority) const;
  const std::vector<SampleResult>& results() const noexcept;
  std::size_t worker_submission_count() const noexcept;
  std::size_t worker_execution_count() const noexcept;
  bool has_pending_native_work() const noexcept;

  void shutdown();
  bool is_shutdown() const noexcept;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace robot_harness

#endif  // ROBOT_HARNESS_SAMPLE_EXECUTION_HPP_
