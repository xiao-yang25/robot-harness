#include "robot_harness/compute_execution.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace robot_harness;
void require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <typename Predicate> void pump_until(ComputeExecutionHost& host, Predicate predicate) {
  const auto end = ComputeExecutionHost::now() + 7000;
  while (!predicate()) {
    require(ComputeExecutionHost::now() < end, "bounded integration observation timed out");
    host.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
void initialize(ComputeExecutionHost& host) {
  const auto status = host.initialize();
  require(status.state == InitializationState::kReady && status.is_adapter_ready &&
              !status.is_worker_ready,
          "adapter ready before any child starts");
  require(!host.observation().launched, "initialization must not spawn");
}
OperationAuthority submit(ComputeExecutionHost& host, std::uint64_t count = 100000000) {
  const auto submitted = host.submit({"operation", count, std::nullopt, kLocalComputeProfile});
  require(submitted.status == ComputeSubmissionStatus::kSubmitted &&
              submitted.authority.has_value(),
          "submit");
  return *submitted.authority;
}
void settle(ComputeExecutionHost& host, const OperationAuthority& authority) {
  pump_until(host,
             [&] { return host.receipt(authority)->settlement == SettlementStatus::kSettled; });
  const auto observed = host.observation();
  require(observed.cleanup_confirmed && (observed.exit_code || observed.terminating_signal),
          "real exit and cleanup");
  int status = 0;
  errno = 0;
  require(waitpid(observed.child_process_id, &status, WNOHANG) == -1 && errno == ECHILD,
          "settled child already reaped");
}
void close(ComputeExecutionHost& host) {
  host.begin_shutdown();
  host.begin_shutdown();
  pump_until(host, [&] { return host.shutdown_status() == ComputeShutdownStatus::kClosed; });
  require(host.submit({"after-close", 1, std::nullopt, kLocalComputeProfile}).admission_status ==
              AdmissionStatus::kClosed,
          "closed rejects new work");
}
void normal(const std::string& worker) {
  ComputeExecutionHost host({worker, 100000000, 1000});
  initialize(host);
  require(host.submit({"unsupported", 1, std::nullopt, "hard-stop-100ms"}).admission_status ==
              AdmissionStatus::kCapabilitiesUnavailable,
          "unsupported guarantees rejected");
  require(!host.observation().launched, "denied admission creates no child");
  const auto first = submit(host, 1003);
  settle(host, first);
  require(host.result() && host.result()->sum == 332833505 && host.result()->iterations == 1003,
          "independent known arithmetic result");
  require(host.receipt(first)->output == OutputDisposition::kAccepted, "result delivered");
  const auto second = submit(host, 3);
  require(second.operation_id != first.operation_id && !host.result(),
          "fresh authority and bounded result slot");
  settle(host, second);
  require(host.result() && host.result()->sum == 5, "second operation result");
  require(!host.receipt(first), "old authority does not select new operation");
  close(host);
}
void replacement(const std::string& worker) {
  ComputeExecutionHost host({worker, 100000000, 1000});
  initialize(host);
  const auto a = submit(host);
  pump_until(host, [&] { return host.observation().completed_iterations > 0; });
  const int a_pid = host.observation().child_process_id;
  require(host.observation().completed_iterations < 100000000 && !host.observation().exit_code,
          "replace A while native calculation is unfinished");
  require(host.request_cancel(a).status == ControlStatus::kApplied, "cancel A for replacement");
  require(host.submit({"B", 3}).admission_status == AdmissionStatus::kDomainOccupied &&
              host.observation().child_process_id == a_pid,
          "B cannot replace the owned child before settlement");
  settle(host, a);  // Includes OS waitpid/ECHILD evidence before admitting B.
  require(!host.result() && host.receipt(a)->output == OutputDisposition::kNotDelivered,
          "cancelled A cannot publish its result");
  const auto prepared = host.prepare({"B", 3});
  require(prepared.status == ComputeSubmissionStatus::kPrepared && prepared.authority,
          "prepare B only after A exit and cleanup");
  const auto b = *prepared.authority;
  require(b.operation_id != a.operation_id && b.binding == a.binding && !host.result(),
          "same host grants a fresh operation authority");
  require(!host.dispatch_prepared(a) &&
              host.request_cancel(a).status == ControlStatus::kNoActiveOperation &&
              !host.receipt(b)->cancellation_requested_at,
          "old controls cannot discard or revoke prepared B");
  require(host.dispatch_prepared(b), "dispatch B with fresh authority");
  require(host.request_cancel(a).status == ControlStatus::kNoActiveOperation &&
              !host.observation().stop_requested_at,
          "old cancel cannot send native stop to B");
  settle(host, b);
  require(host.result() && host.result()->operation_id == b.operation_id &&
              host.result()->request_reference == "B" && host.result()->sum == 5 &&
              host.receipt(b)->native_outcome == NativeOutcome::kSucceeded,
          "B computes its own known result after A was reaped");
  close(host);
}

void cancel_running(const std::string& worker, bool ignored) {
  ComputeExecutionHost host({worker, 100000000, ignored ? 20U : 1000U});
  initialize(host);
  const auto authority = submit(host);
  pump_until(host, [&] { return host.observation().completed_iterations > 0; });
  require(host.observation().completed_iterations < 100000000 && !host.observation().exit_code,
          "cancel while actual computation is unfinished");
  auto wrong = authority;
  ++wrong.binding.provider_generation;
  require(host.request_cancel(wrong).status == ControlStatus::kNoActiveOperation,
          "wrong authority denied");
  require(host.request_cancel(authority).status == ControlStatus::kApplied,
          "cancel current operation");
  require(host.request_cancel(authority).status == ControlStatus::kAlreadyRequested,
          "repeat cancel idempotent");
  require(host.submit({"conflict", 1, std::nullopt, kLocalComputeProfile}).admission_status ==
              AdmissionStatus::kDomainOccupied,
          "cancel does not reopen domain");
  settle(host, authority);
  const auto observed = host.observation();
  require(!host.result() && observed.stop_requested_at && observed.stop_sent,
          "cancel retains output revocation");
  if (ignored) {
    require(observed.termination_requested && observed.terminating_signal == 9,
            "ignored stop escalated and reaped");
    require(host.receipt(authority)->native_outcome == NativeOutcome::kFailed,
            "SIGKILL not cooperative success");
  } else {
    require(!observed.termination_requested && observed.exit_code == 20, "cooperative actual exit");
    require(host.receipt(authority)->native_outcome == NativeOutcome::kCancelled,
            "cancelled outcome");
  }
  close(host);
}
void startup_shutdown(const std::string& worker, bool deadline) {
  ComputeExecutionHost host({worker, 100000000, 20});
  initialize(host);
  ComputeRequest request{"startup", 100000000, std::nullopt, kLocalComputeProfile};
  if (deadline)
    request.deadline = ComputeExecutionHost::now() + 40;
  const auto submission = host.submit(request);
  require(submission.status == ComputeSubmissionStatus::kSubmitted,
          "spawn before startup interruption");
  const auto authority = *submission.authority;
  require(!host.observation().started, "readiness has not been observed");
  if (!deadline)
    host.begin_shutdown();
  settle(host, authority);
  require(!host.result() && host.observation().termination_requested,
          "startup interruption closes actual process");
  if (deadline)
    require(host.receipt(authority)->expiry_observed_at.has_value(), "deadline recorded");
  close(host);
}
void delayed_exit(const std::string& worker) {
  ComputeExecutionHost host({worker, 100000000, 2000});
  initialize(host);
  const auto authority = submit(host, 1003);
  pump_until(host, [&] { return host.observation().completed_iterations == 1003; });
  require(!host.observation().exit_code && !host.observation().cleanup_confirmed,
          "completed calculation is not process exit");
  require(host.receipt(authority)->native_outcome == NativeOutcome::kPending &&
              host.receipt(authority)->settlement == SettlementStatus::kPending,
          "terminal frame alone does not complete host receipt");
  require(host.submit({"too-early", 1, std::nullopt, kLocalComputeProfile}).admission_status ==
              AdmissionStatus::kDomainOccupied,
          "exit cleanup pending blocks next request");
  host.request_cancel(authority);
  settle(host, authority);
  require(host.receipt(authority)->native_outcome == NativeOutcome::kSucceeded &&
              host.receipt(authority)->output == OutputDisposition::kNotDelivered && !host.result(),
          "late success retained but result permission revoked");
  require(!host.observation().termination_requested, "test grace permits observed delayed exit");
  close(host);
}
void cancel_after_exit(const std::string& worker) {
  ComputeExecutionHost host({worker, 100000000, 1000});
  initialize(host);
  const auto authority = submit(host, 3);
  const int pid = host.observation().child_process_id;
  const auto end = ComputeExecutionHost::now() + 7000;
  siginfo_t information{};
  // Observe, but do not reap: the adapter remains the sole child waiter/reaper.
  while (information.si_pid != pid) {
    require(ComputeExecutionHost::now() < end, "child exit observation timed out");
    const int observed =
        waitid(P_PID, static_cast<id_t>(pid), &information, WEXITED | WNOHANG | WNOWAIT);
    require(observed == 0 || errno == EINTR, "non-reaping exit observation");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(information.si_code == CLD_EXITED && information.si_status == 0,
          "native completed before cancel");
  host.request_cancel(authority);
  settle(host, authority);
  require(!host.observation().stop_sent && host.observation().stop_send_error != 0,
          "closed peer fails control send");
  require(host.receipt(authority)->native_outcome == NativeOutcome::kSucceeded && !host.result() &&
              host.receipt(authority)->output == OutputDisposition::kNotDelivered,
          "control error cannot erase prior successful execution");
  require(!host.observation().termination_requested, "reaped exit avoids late kill");
  close(host);
}
void backpressure(const std::string& worker) {
  ComputeExecutionHost host({worker, 100000000, 1000});
  initialize(host);
  const auto authority = submit(host);
  pump_until(host, [&] { return host.observation().completed_iterations > 0; });
  // Leave the bounded event pipe unread while the tiny-batch fixture keeps computing.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  host.request_cancel(authority);
  settle(host, authority);
  require(host.observation().dropped_progress > 0, "event backpressure actually observed");
  require(host.observation().exit_code == 20 && !host.observation().termination_requested &&
              !host.result(),
          "backpressure does not prevent cooperative stop and cleanup");
  close(host);
}
void broken_events(const std::string& worker, bool backwards) {
  ComputeExecutionHost host({worker, 100000000, 1000});
  initialize(host);
  const auto authority = submit(host, 1000);
  if (backwards) {
    pump_until(host, [&] { return host.observation().started; });
    host.request_cancel(authority);
  }
  settle(host, authority);
  require(host.observation().protocol_error && !host.result() &&
              host.receipt(authority)->native_outcome == NativeOutcome::kFailed,
          "broken event stream cannot produce cancelled or successful outcome");
  if (!backwards) {
    require(host.observation().termination_requested && host.observation().terminating_signal == 9,
            "EOF without terminal triggers containment while child still alive");
  }
  close(host);
}
void host_isolation(const std::string& worker) {
  ComputeExecutionHost first({worker, 100000000, 1000}), second({worker, 100000000, 1000});
  initialize(first);
  initialize(second);
  const auto left = submit(first, 3);
  const auto right = submit(second, 1003);
  require(!(left.binding == right.binding) &&
              left.binding.effect_domain != right.binding.effect_domain,
          "independent hosts need distinct bindings and result domains");
  require(second.request_cancel(left).status == ControlStatus::kNoActiveOperation &&
              !second.receipt(left),
          "authority cannot cross host instances");
  settle(first, left);
  settle(second, right);
  require(first.result()->sum == 5 && second.result()->sum == 332833505,
          "independent result sinks");
  close(first);
  close(second);
}
void fault(const std::string& worker) {
  ComputeExecutionHost host({worker, 100000000, 20});
  initialize(host);
  const auto authority = submit(host);
  settle(host, authority);
  require(host.receipt(authority)->native_outcome == NativeOutcome::kFailed && !host.result(),
          "fault never fabricates success");
  close(host);
}
struct TemporaryExecutable {
  std::string directory;
  std::string path;
  TemporaryExecutable() {
    char name[] = "/tmp/robot-harness-compute-test-XXXXXX";
    char* made = mkdtemp(name);
    require(made != nullptr, "temporary directory");
    directory = made;
    path = directory + "/worker";
  }
  ~TemporaryExecutable() {
    unlink(path.c_str());
    rmdir(directory.c_str());
  }
};
void prelaunch(const std::string& worker) {
  TemporaryExecutable link;
  require(symlink(worker.c_str(), link.path.c_str()) == 0, "worker path");
  ComputeExecutionHost host({link.path, 1000, 20});
  initialize(host);
  require(host.submit({"over-budget", 1001, std::nullopt, kLocalComputeProfile}).admission_status ==
              AdmissionStatus::kCapabilitiesUnavailable,
          "lower trusted workload ceiling");
  auto prepared = host.prepare({"prepared", 100, std::nullopt, kLocalComputeProfile});
  require(prepared.status == ComputeSubmissionStatus::kPrepared, "prepare");
  require(unlink(link.path.c_str()) == 0, "remove launch capability");
  require(!host.dispatch_prepared(*prepared.authority), "preflight loss refuses native dispatch");
  require(!host.observation().launched &&
              host.receipt(*prepared.authority)->dispatch == DispatchStatus::kNotSubmitted &&
              host.receipt(*prepared.authority)->settlement == SettlementStatus::kSettled,
          "truthful non-submission closure");
  close(host);

  TemporaryExecutable invalid;
  {
    std::ofstream file(invalid.path);
    file << "not an executable format\n";
  }
  require(chmod(invalid.path.c_str(), 0700) == 0, "executable permission");
  ComputeExecutionHost launch_failure({invalid.path, 1000, 20});
  initialize(launch_failure);
  const auto failed =
      launch_failure.submit({"launch-failure", 10, std::nullopt, kLocalComputeProfile});
  require(failed.status == ComputeSubmissionStatus::kNotSubmitted && failed.authority,
          "spawn failed without child");
  require(launch_failure.observation().launch_error != 0 && !launch_failure.observation().launched,
          "actual spawn error");
  require(launch_failure.receipt(*failed.authority)->settlement == SettlementStatus::kSettled,
          "spawn failure closes claim");
  close(launch_failure);

  ComputeExecutionHost prepared_cancel({worker, 1000, 20});
  initialize(prepared_cancel);
  const auto pending =
      prepared_cancel.prepare({"cancel-before-spawn", 100, std::nullopt, kLocalComputeProfile});
  prepared_cancel.request_cancel(*pending.authority);
  require(!prepared_cancel.dispatch_prepared(*pending.authority) &&
              !prepared_cancel.observation().launched,
          "cancel before spawn prevents child");
  close(prepared_cancel);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 3, "usage: test CASE WORKER");
    const std::string mode = argv[1], worker = argv[2];
    if (mode == "normal")
      normal(worker);
    else if (mode == "replacement")
      replacement(worker);
    else if (mode == "cancel")
      cancel_running(worker, false);
    else if (mode == "ignore")
      cancel_running(worker, true);
    else if (mode == "shutdown")
      startup_shutdown(worker, false);
    else if (mode == "deadline")
      startup_shutdown(worker, true);
    else if (mode == "delayed_exit")
      delayed_exit(worker);
    else if (mode == "cancel_after_exit")
      cancel_after_exit(worker);
    else if (mode == "backpressure")
      backpressure(worker);
    else if (mode == "eof_alive")
      broken_events(worker, false);
    else if (mode == "backwards")
      broken_events(worker, true);
    else if (mode == "host_isolation")
      host_isolation(worker);
    else if (mode == "fault")
      fault(worker);
    else if (mode == "prelaunch")
      prelaunch(worker);
    else
      throw std::runtime_error("unknown case");
    std::cout << mode << " passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
