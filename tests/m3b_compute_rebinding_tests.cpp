#include "robot_harness/compute_execution.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace robot_harness;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Predicate> void pump_until(ComputeExecutionHost& host, Predicate predicate) {
  const auto end = ComputeExecutionHost::now() + 7000;
  while (!predicate()) {
    require(ComputeExecutionHost::now() < end, "bounded process observation timed out");
    host.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

OperationAuthority submit(ComputeExecutionHost& host, const char* name, std::uint64_t iterations) {
  const auto submission = host.submit({name, iterations});
  require(submission.status == ComputeSubmissionStatus::kSubmitted && submission.authority,
          "real worker submitted");
  return *submission.authority;
}

void settle_and_verify_reaping(ComputeExecutionHost& host, const OperationAuthority& authority) {
  pump_until(host,
             [&] { return host.receipt(authority)->settlement == SettlementStatus::kSettled; });
  const auto observed = host.observation();
  require(observed.cleanup_confirmed && (observed.exit_code || observed.terminating_signal),
          "native exit and actual channel cleanup recorded");
  int status = 0;
  errno = 0;
  require(waitpid(observed.child_process_id, &status, WNOHANG) == -1 && errno == ECHILD,
          "old child has already been reaped by its owner");
}

void close(ComputeExecutionHost& host) {
  host.begin_shutdown();
  pump_until(host, [&] { return host.shutdown_status() == ComputeShutdownStatus::kClosed; });
  const auto binding = host.binding_status().active;
  require(host.withdraw_binding(binding).status == BindingControlStatus::kClosed &&
              host.rebind_provider(binding, "closed", "/unused").status ==
                  BindingControlStatus::kClosed,
          "permanent shutdown cannot be reopened");
}

void exit_before_handoff(const std::string& normal, const std::string& delayed,
                         const std::string& failing) {
  ComputeExecutionHost host({delayed, 10000, 2000});
  require(host.initialize().state == InitializationState::kReady, "initial adapter ready");
  const auto a = submit(host, "A", 1003);
  pump_until(host, [&] { return host.observation().completed_iterations == 1003; });
  const int old_pid = host.observation().child_process_id;
  require(!host.observation().exit_code && !host.observation().cleanup_confirmed,
          "calculation has completed but actual process still lives");
  const auto withdrawn = host.withdraw_binding(a.binding);
  require(withdrawn.status == BindingControlStatus::kApplied && withdrawn.native_stop_authority &&
              host.receipt(a)->binding_withdrawn_at && !host.receipt(a)->cancellation_requested_at,
          "withdrawal has its own reason and requests native stop");
  const auto stop_time = host.observation().stop_requested_at;
  require(stop_time.has_value() &&
              host.withdraw_binding(a.binding).status == BindingControlStatus::kAlreadyInPhase &&
              host.observation().stop_requested_at == stop_time,
          "repeat withdrawal does not restart stopping");
  require(host.rebind_provider(a.binding, "early-exit", failing).status ==
                  BindingControlStatus::kUnresolvedWork &&
              host.observation().child_process_id == old_pid &&
              host.binding_status().active == a.binding,
          "completed calculation cannot bypass exit and channel cleanup");
  require(host.submit({"blocked", 3}).status == ComputeSubmissionStatus::kAdmissionBlocked,
          "withdrawal blocks new tasks while the old child remains owned");
  settle_and_verify_reaping(host, a);
  require(host.receipt(a)->native_outcome == NativeOutcome::kSucceeded &&
              host.receipt(a)->output == OutputDisposition::kNotDelivered && !host.result() &&
              !host.observation().termination_requested,
          "old natural success stays truthful but its result is revoked");
  require(host.rebind_provider(a.binding, "early-exit", failing).status ==
              BindingControlStatus::kApplied,
          "new provider installed only after actual old closure");
  const auto b_binding = host.binding_status().active;
  require(
      b_binding.provider_id == "early-exit" && b_binding.effect_domain == a.binding.effect_domain &&
          b_binding.composition_revision == a.binding.composition_revision &&
          b_binding.provider_generation > a.binding.provider_generation &&
          host.observation().child_process_id == old_pid && host.observation().cleanup_confirmed,
      "same host/domain retains historical observation without spawning on commit");
  host.poll();
  require(host.receipt(a)->native_outcome == NativeOutcome::kSucceeded &&
              host.receipt(a)->output == OutputDisposition::kNotDelivered,
          "post-commit polling preserves the old receipt");
  const auto b = submit(host, "B", 3);
  settle_and_verify_reaping(host, b);
  require(b.binding == b_binding && b.operation_id > a.operation_id &&
              host.receipt(b)->native_outcome == NativeOutcome::kFailed && !host.result(),
          "new executable actually used: early-exit fixture fails instead of calculating");
  require(host.withdraw_binding(b.binding).status == BindingControlStatus::kApplied &&
              host.rebind_provider(b.binding, "normal", normal).status ==
                  BindingControlStatus::kApplied,
          "a later explicit handoff can replace the failed provider");
  require(host.submit({"over-budget", 10001}).admission_status ==
                  AdmissionStatus::kCapabilitiesUnavailable &&
              host.submit({"unsupported", 1, std::nullopt, "hard-stop"}).admission_status ==
                  AdmissionStatus::kCapabilitiesUnavailable,
          "rebind preserves trusted work limit and supported profile");
  const auto c = *host.prepare({"C", 3}).authority;
  require(c.binding.effect_domain == a.binding.effect_domain &&
              c.binding.provider_generation > b.binding.provider_generation &&
              c.operation_id > b.operation_id &&
              host.request_cancel(a).status == ControlStatus::kNoActiveOperation &&
              host.request_cancel(b).status == ControlStatus::kNoActiveOperation &&
              !host.dispatch_prepared(a) && !host.dispatch_prepared(b) &&
              host.withdraw_binding(b.binding).status == BindingControlStatus::kStaleIdentity &&
              host.rebind_provider(a.binding, "stale", delayed).status ==
                  BindingControlStatus::kStaleIdentity,
          "old controls cannot affect prepared current work or its provider");
  require(host.dispatch_prepared(c), "current authority dispatches");
  require(host.request_cancel(a).status == ControlStatus::kNoActiveOperation &&
              !host.observation().stop_requested_at,
          "old cancellation cannot signal the new native child");
  settle_and_verify_reaping(host, c);
  require(host.result() && host.result()->operation_id == c.operation_id &&
              host.result()->request_reference == "C" && host.result()->sum == 5,
          "new worker delivers the independently known sum 0+1+4");
  close(host);
}

struct TemporaryWorkerPath {
  std::string directory;
  std::string path;
  explicit TemporaryWorkerPath(const std::string& worker) {
    char name[] = "/tmp/robot-harness-rebind-XXXXXX";
    const auto made = mkdtemp(name);
    require(made != nullptr, "create isolated worker path");
    directory = made;
    path = directory + "/worker";
    require(symlink(worker.c_str(), path.c_str()) == 0, "link worker");
  }
  ~TemporaryWorkerPath() {
    unlink(path.c_str());
    rmdir(directory.c_str());
  }
};

void lost_dependency_and_candidate_failure(const std::string& normal) {
  for (const bool prepared_first : {false, true}) {
    TemporaryWorkerPath link(normal);
    ComputeExecutionHost host({link.path, 1000, 1000});
    host.initialize();
    const auto old = host.binding_status().active;
    std::optional<OperationAuthority> prepared;
    if (prepared_first) {
      prepared = host.prepare({"never-spawn", 3}).authority;
      require(prepared.has_value(), "prepare before capability disappears");
    }
    require(unlink(link.path.c_str()) == 0, "remove native launch prerequisite");
    if (prepared) {
      require(!host.dispatch_prepared(*prepared), "loss at dispatch prevents spawn");
      require(host.receipt(*prepared)->dispatch == DispatchStatus::kNotSubmitted &&
                  host.receipt(*prepared)->settlement == SettlementStatus::kSettled &&
                  host.receipt(*prepared)->binding_withdrawn_at,
              "prepared withdrawal closes through non-submission evidence");
    } else {
      require(host.initialize().state == InitializationState::kRecoveryRequired,
              "repeated initialize observes loss instead of silently retaining readiness");
    }
    require(host.binding_status().phase == BindingPhase::kWithdrawn && !host.observation().launched,
            "dependency loss withdraws the binding without inventing a child");
    require(host.rebind_provider(old, "bad-candidate", link.path).status ==
                    BindingControlStatus::kInvalidEvidence &&
                host.binding_status().phase == BindingPhase::kWithdrawn &&
                !host.binding_status().candidate,
            "failed candidate readiness is discarded without reopening or losing old state");
    require(symlink(normal.c_str(), link.path.c_str()) == 0, "restore native prerequisite");
    host.initialize();
    host.poll();
    require(host.submit({"no-auto-resume", 3}).status == ComputeSubmissionStatus::kAdmissionBlocked,
            "restored path and initialize cannot undo withdrawal");
    require(host.rebind_provider(old, "restored", link.path).status ==
                BindingControlStatus::kApplied,
            "explicit handoff works after fresh observations");
    const auto restored = host.binding_status().active;
    require(restored.provider_generation == old.provider_generation + 2,
            "failed candidate's identity is retired, not reused");
    const auto a = submit(host, "restored", 3);
    settle_and_verify_reaping(host, a);
    require(host.result() && host.result()->sum == 5, "restored provider actually executes");
    // No work after commit: retained old diagnostics must not become a shutdown obstacle.
    require(host.withdraw_binding(restored).status == BindingControlStatus::kApplied &&
                host.rebind_provider(restored, "last", normal).status ==
                    BindingControlStatus::kApplied,
            "rebind a settled provider without submitting another operation");
    close(host);
    require(host.result() && host.result()->sum == 5,
            "already delivered result remains a fact across rebind and shutdown");
  }
}

void running_dependency_loss(const std::string& normal) {
  TemporaryWorkerPath link(normal);
  ComputeExecutionHost host({link.path, 100000000, 1000});
  host.initialize();
  const auto a = submit(host, "running", 100000000);
  pump_until(host, [&] { return host.observation().completed_iterations > 0; });
  require(unlink(link.path.c_str()) == 0, "remove dependency while child runs");
  host.poll();
  require(host.binding_status().phase == BindingPhase::kWithdrawn &&
              host.receipt(a)->binding_withdrawn_at && !host.receipt(a)->cancellation_requested_at,
          "running prerequisite loss is reported as withdrawal, not user cancellation");
  settle_and_verify_reaping(host, a);
  require(!host.result(), "withdrawn operation cannot publish output");
  require(host.rebind_provider(a.binding, "replacement", normal).status ==
              BindingControlStatus::kApplied,
          "replace a lost prerequisite after actual native cleanup");
  const auto b = submit(host, "replacement", 3);
  settle_and_verify_reaping(host, b);
  require(host.result() && host.result()->sum == 5, "replacement remains usable");
  close(host);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 5, "usage: test CASE NORMAL_WORKER DELAYED_WORKER FAILING_WORKER");
    const std::string mode = argv[1];
    if (mode == "handoff") {
      exit_before_handoff(argv[2], argv[3], argv[4]);
    } else if (mode == "unavailable") {
      lost_dependency_and_candidate_failure(argv[2]);
    } else if (mode == "running_loss") {
      running_dependency_loss(argv[2]);
    } else {
      throw std::runtime_error("unknown case");
    }
    std::cout << "compute rebind " << mode << " passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
