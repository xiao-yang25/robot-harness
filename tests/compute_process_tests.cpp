#include "compute_process.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

namespace {

using Clock = std::chrono::steady_clock;
using robot_harness::detail::ComputeProcess;
using robot_harness::detail::ProcessObservation;

struct Workers {
  std::string normal;
  std::string ignore;
  std::string startup_delay;
  std::string malformed;
  std::string early_exit;
  std::string blocked_control;
  std::string delayed_exit;
  std::string unavailable_control;
};

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void pause_one_millisecond() {
  const int result = ::poll(nullptr, 0, 1);
  require(result == 0 || (result < 0 && errno == EINTR), "poll delay failed");
}

template <typename Predicate>
void await(ComputeProcess& process, std::uint64_t now_ms, Predicate predicate,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
  const auto deadline = Clock::now() + timeout;
  while (!predicate(process.observation())) {
    process.poll(now_ms);
    require(Clock::now() < deadline, "bounded test observation timed out");
    pause_one_millisecond();
  }
}

std::uint64_t expected_sum(std::uint64_t count) {
  const std::uint64_t remainder = count % 1000;
  const std::uint64_t partial =
      remainder == 0 ? 0 : (remainder - 1) * remainder * (2 * remainder - 1) / 6;
  return (count / 1000) * 332833500ULL + partial;
}

void require_clean_exit(const ProcessObservation& observation, int code) {
  require(observation.exit_status.has_value(), "missing child exit status");
  require(WIFEXITED(*observation.exit_status), "child did not exit normally");
  require(WEXITSTATUS(*observation.exit_status) == code, "unexpected child exit code");
}

void finish_channels(ComputeProcess& process) {
  require(process.close_channels(), "channels were not proven closed");
  require(process.close_channels(), "repeated channel close was not idempotent");
  require(process.observation().channels_closed, "closed observation missing");
}

void test_normal(const Workers& workers) {
  constexpr std::uint64_t kIterations = 123456;
  require(ComputeProcess::can_launch(workers.normal), "normal worker failed capability check");
  ComputeProcess process(workers.normal, kIterations, 20);
  require(process.launch(), "normal worker launch failed");
  require(!process.close_channels(), "channels closed before exit and EOF");
  await(process, 0, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });

  const auto& observation = process.observation();
  require_clean_exit(observation, 0);
  require(observation.started, "normal worker never started");
  require(!observation.malformed, "normal frames marked malformed");
  require(observation.terminal == ProcessObservation::Terminal::kCompleted,
          "normal terminal missing");
  require(observation.terminal_iterations == kIterations, "normal terminal count mismatch");
  require(observation.terminal_value == expected_sum(kIterations), "normal sum mismatch");
  finish_channels(process);
}

void test_progress_then_cancel(const Workers& workers) {
  ComputeProcess process(workers.normal, 100000000, 1000);
  require(process.launch(), "cancel worker launch failed");
  await(process, 100, [](const ProcessObservation& observation) {
    return observation.started && observation.progress > 0;
  });
  const std::uint64_t progress_before_cancel = process.observation().progress;
  process.request_stop(100);
  process.request_stop(999);
  await(process, 100, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });

  const auto& observation = process.observation();
  require(progress_before_cancel > 0, "cancel test did not perform real work");
  require(observation.stop_requested_at == 100, "stop anchor changed on repeated request");
  require(observation.stop_delivered, "stop command was not delivered");
  require(observation.stop_acknowledged, "stop command was not acknowledged");
  require(!observation.termination_requested, "cooperative cancellation escalated");
  require(observation.terminal == ProcessObservation::Terminal::kCancelled,
          "cancel terminal missing");
  require_clean_exit(observation, 20);
  finish_channels(process);
}

void test_cancel_before_start(const Workers& workers) {
  ComputeProcess process(workers.startup_delay, 100000000, 1000);
  require(process.launch(), "startup-delay worker launch failed");
  process.request_stop(500);
  await(process, 500, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });

  const auto& observation = process.observation();
  require(observation.started, "startup-delay worker did not emit ready");
  require(observation.stop_acknowledged, "startup cancellation was not acknowledged");
  require(observation.terminal == ProcessObservation::Terminal::kCancelled,
          "startup cancellation terminal missing");
  require(observation.terminal_iterations == 0, "startup cancellation performed work first");
  require_clean_exit(observation, 20);
  finish_channels(process);
}

void test_ignored_stop_is_killed_and_reaped(const Workers& workers) {
  ComputeProcess process(workers.ignore, 100000000, 10);
  require(process.launch(), "ignore worker launch failed");
  await(process, 1000,
        [](const ProcessObservation& observation) { return observation.progress > 0; });
  process.request_stop(1000);
  await(process, 1000,
        [](const ProcessObservation& observation) { return observation.stop_ignored; });
  process.poll(1010);
  await(process, 1010, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });

  const auto& observation = process.observation();
  require(observation.stop_delivered && observation.stop_ignored, "ignored stop evidence missing");
  require(observation.termination_requested, "ignored worker was not escalated once grace elapsed");
  require(observation.exit_status.has_value() && WIFSIGNALED(*observation.exit_status) &&
              WTERMSIG(*observation.exit_status) == SIGKILL,
          "ignored worker was not killed and reaped");
  finish_channels(process);
}

void test_blocked_control_is_killed_and_reaped(const Workers& workers) {
  ComputeProcess process(workers.blocked_control, 100000000, 5);
  require(process.launch(), "blocked-control worker launch failed");
  await(process, 300,
        [](const ProcessObservation& observation) { return observation.progress > 0; });
  process.request_stop(300);
  require(process.observation().stop_delivered, "blocked control did not accept queued stop");
  process.poll(305);
  await(process, 305, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });

  const auto& observation = process.observation();
  require(!observation.stop_acknowledged, "blocked control unexpectedly acknowledged stop");
  require(observation.termination_requested, "blocked control was not escalated");
  require(observation.exit_status.has_value() && WIFSIGNALED(*observation.exit_status) &&
              WTERMSIG(*observation.exit_status) == SIGKILL,
          "blocked-control worker was not killed and reaped");
  finish_channels(process);
}

void test_unavailable_control_is_killed_and_reaped(const Workers& workers) {
  ComputeProcess process(workers.unavailable_control, 100000000, 1000);
  require(process.launch(), "unavailable-control worker launch failed");
  await(process, 300, [](const ProcessObservation& observation) { return observation.started; });
  pause_one_millisecond();
  process.request_stop(300);
  require(!process.observation().stop_delivered, "closed control unexpectedly delivered stop");
  require(process.observation().stop_error != 0, "closed control omitted stop error");
  require(process.observation().io_error == 0, "closed control polluted event I/O status");
  process.poll(300);
  await(process, 300, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });

  const auto& observation = process.observation();
  require(observation.termination_requested, "unavailable control was not escalated immediately");
  require(observation.exit_status.has_value() && WIFSIGNALED(*observation.exit_status) &&
              WTERMSIG(*observation.exit_status) == SIGKILL,
          "unavailable-control worker was not killed and reaped");
  finish_channels(process);
}

void test_malformed_event(const Workers& workers) {
  ComputeProcess process(workers.malformed, 1000, 5);
  require(process.launch(), "malformed worker launch failed");
  await(process, 0, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });
  const auto& observation = process.observation();
  require(observation.malformed, "truncated event was accepted");
  require(observation.terminal == ProcessObservation::Terminal::kNone,
          "malformed worker fabricated terminal evidence");
  require_clean_exit(observation, 21);
  finish_channels(process);
}

void test_early_exit(const Workers& workers) {
  ComputeProcess process(workers.early_exit, 1000, 5);
  require(process.launch(), "early-exit worker launch failed");
  await(process, 0, [](const ProcessObservation& observation) {
    return observation.exit_status.has_value() && observation.event_eof;
  });
  const auto& observation = process.observation();
  require(!observation.started, "early-exit worker emitted ready");
  require(observation.terminal == ProcessObservation::Terminal::kNone,
          "early-exit worker fabricated terminal evidence");
  require_clean_exit(observation, 21);
  finish_channels(process);
}

void test_completed_before_actual_exit(const Workers& workers) {
  constexpr std::uint64_t kIterations = 8192;
  ComputeProcess process(workers.delayed_exit, kIterations, 2000);
  require(process.launch(), "delayed-exit worker launch failed");
  await(process, 700, [](const ProcessObservation& observation) {
    return observation.terminal == ProcessObservation::Terminal::kCompleted;
  });
  require(!process.observation().exit_status.has_value(),
          "delayed fixture exited before terminal/exit boundary was observed");
  require(!process.close_channels(), "channels closed without actual exit and EOF");
  process.request_stop(700);
  await(
      process, 700,
      [](const ProcessObservation& observation) {
        return observation.exit_status.has_value() && observation.event_eof;
      },
      std::chrono::milliseconds(2500));
  const auto& observation = process.observation();
  require(observation.stop_requested, "late stop was not recorded");
  require(observation.terminal == ProcessObservation::Terminal::kCompleted,
          "completed evidence changed after late stop");
  require_clean_exit(observation, 0);
  finish_channels(process);
}

void test_launch_failure(const Workers& workers) {
  const std::string invalid = workers.normal + "/child";
  require(!ComputeProcess::can_launch(invalid), "invalid path passed capability check");
  require(!ComputeProcess::can_launch("relative-worker"), "relative path passed capability check");

  ComputeProcess process(invalid, 1000, 5);
  require(!process.launch(), "invalid worker unexpectedly launched");
  const auto& observation = process.observation();
  require(!observation.launched && observation.child_pid == -1,
          "launch failure claimed child ownership");
  require(observation.launch_error != 0, "launch failure omitted error");

  ComputeProcess invalid_limit(workers.normal, 0, 5);
  require(!invalid_limit.launch(), "zero workload unexpectedly launched");
  require(invalid_limit.observation().launch_error == EINVAL,
          "zero workload did not report EINVAL");
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 9) {
    std::fprintf(stderr, "expected eight worker executable paths\n");
    return EXIT_FAILURE;
  }
  const Workers workers{argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]};

  const std::pair<const char*, std::function<void()>> tests[] = {
      {"normal", [&] { test_normal(workers); }},
      {"progress_then_cancel", [&] { test_progress_then_cancel(workers); }},
      {"cancel_before_start", [&] { test_cancel_before_start(workers); }},
      {"ignored_stop", [&] { test_ignored_stop_is_killed_and_reaped(workers); }},
      {"blocked_control", [&] { test_blocked_control_is_killed_and_reaped(workers); }},
      {"unavailable_control", [&] { test_unavailable_control_is_killed_and_reaped(workers); }},
      {"malformed_event", [&] { test_malformed_event(workers); }},
      {"early_exit", [&] { test_early_exit(workers); }},
      {"completed_before_actual_exit", [&] { test_completed_before_actual_exit(workers); }},
      {"launch_failure", [&] { test_launch_failure(workers); }},
  };

  for (const auto& test : tests) {
    try {
      test.second();
      std::printf("PASS %s\n", test.first);
    } catch (const std::exception& error) {
      std::fprintf(stderr, "FAIL %s: %s\n", test.first, error.what());
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}
