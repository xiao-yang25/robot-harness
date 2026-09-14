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
};

struct SampleResult {
  OperationId operation_id = 0;
  std::string request_reference;
  std::vector<std::int64_t> squared_values;
  std::int64_t sum_of_squares = 0;
};

enum class CompletionMode { kSynchronous, kDeferred };
enum class SampleSubmissionStatus {
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

  SampleSubmission submit(const SampleRequest& request);
  bool complete_deferred();
  bool process_next_staged_event();
  void process_all_staged_events();
  std::size_t staged_event_count() const noexcept;

  std::optional<OperationReceipt> receipt(const OperationAuthority& authority) const;
  const std::vector<SampleResult>& results() const noexcept;
  std::size_t worker_submission_count() const noexcept;
  bool has_pending_native_work() const noexcept;

  void shutdown();
  bool is_shutdown() const noexcept;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace robot_harness

#endif  // ROBOT_HARNESS_SAMPLE_EXECUTION_HPP_
