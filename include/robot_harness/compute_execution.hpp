#ifndef ROBOT_HARNESS_COMPUTE_EXECUTION_HPP_
#define ROBOT_HARNESS_COMPUTE_EXECUTION_HPP_

#include "robot_harness/authority_gate.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace robot_harness {

// Exact supported guarantee set; it has no hard stopping or process-memory bound.
inline constexpr const char* kLocalComputeProfile = "host-polled-local-compute-v1";

struct ComputeHostConfig {
  // Trusted deployment path to the same-build worker; never supplied by an Agent request.
  std::string worker_executable;
  std::uint64_t maximum_iterations = 100000000;
  std::uint64_t stop_grace_ms = 100;
};

struct ComputeRequest {
  std::string request_reference;
  std::uint64_t iterations = 0;
  std::optional<MonotonicTime> deadline;
  std::string required_profile_id = kLocalComputeProfile;
};

struct ComputeResult {
  OperationId operation_id = 0;
  std::string request_reference;
  std::uint64_t iterations = 0;
  std::uint64_t sum = 0;
};

enum class ComputeSubmissionStatus {
  kPrepared,
  kSubmitted,
  kInvalidArgument,
  kAdmissionBlocked,
  kNotSubmitted
};
struct ComputeSubmission {
  ComputeSubmissionStatus status = ComputeSubmissionStatus::kInvalidArgument;
  AdmissionStatus admission_status = AdmissionStatus::kInvalidRequest;
  std::optional<OperationAuthority> authority;
};

enum class ComputeShutdownStatus { kOpen, kClosing, kClosed, kUnresolved };

struct ComputeObservation {
  bool launched = false;
  bool started = false;
  std::uint64_t completed_iterations = 0;
  std::uint64_t dropped_progress = 0;
  bool stop_sent = false;
  int stop_send_error = 0;
  std::optional<MonotonicTime> stop_requested_at;
  bool termination_requested = false;
  bool protocol_error = false;
  bool ownership_lost = false;
  int launch_error = 0;
  int system_error = 0;
  // Historical identity for diagnostics. Callers must not signal or reap it.
  int child_process_id = -1;
  std::optional<int> exit_code;
  std::optional<int> terminating_signal;
  bool cleanup_confirmed = false;
};

// Single host thread; no callbacks, background supervisor, or hidden request queue.
// Each host has its own result domain and one-worker limit; this is not a global quota.
// Authorities are process-local and cannot be transferred between host instances.
// The host must poll while work or shutdown is pending. Times are steady-clock ms.
class ComputeExecutionHost final {
public:
  explicit ComputeExecutionHost(ComputeHostConfig config);
  // Fallback terminates/reaps owned work synchronously; it has no bounded return
  // guarantee and cannot report a successful shutdown. Prefer begin_shutdown/poll.
  ~ComputeExecutionHost();
  ComputeExecutionHost(const ComputeExecutionHost&) = delete;
  ComputeExecutionHost& operator=(const ComputeExecutionHost&) = delete;
  ComputeExecutionHost(ComputeExecutionHost&&) = delete;
  ComputeExecutionHost& operator=(ComputeExecutionHost&&) = delete;

  static MonotonicTime now() noexcept;
  StartupStatus initialize();
  BindingStatus binding_status() const;
  BindingDecision withdraw_binding(const BindingIdentity& expected_binding);
  // Trusted deployment selection, never taken from ComputeRequest. Same-build
  // protocol/profile only; workload and stop policy remain fixed for this host.
  // Requires explicit withdrawal and old child exit/reaping/channel cleanup.
  // Prepares configuration without spawning; the next request launches the worker.
  BindingDecision rebind_provider(const BindingIdentity& expected_old_binding,
                                  const std::string& provider_id,
                                  const std::string& worker_executable);
  ComputeSubmission prepare(const ComputeRequest& request);
  bool dispatch_prepared(const OperationAuthority& authority);
  ComputeSubmission submit(const ComputeRequest& request);
  ControlDecision request_cancel(const OperationAuthority& authority);
  // Advance currently observable work once; this is not the OS poll/epoll API.
  // Keep calling while work or shutdown is pending, even without worker events.
  // Event reads and exit checks do not wait, but capability probes and receipt
  // processing have no hard runtime bound. Delayed calls delay stop escalation.
  void poll();
  void begin_shutdown();
  ComputeShutdownStatus shutdown_status() const noexcept;
  ComputeObservation observation() const;
  std::optional<OperationReceipt> receipt(const OperationAuthority& authority) const;
  const std::optional<ComputeResult>& result() const noexcept;

private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace robot_harness
#endif  // ROBOT_HARNESS_COMPUTE_EXECUTION_HPP_
