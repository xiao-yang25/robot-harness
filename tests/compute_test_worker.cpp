#include "compute_protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <system_error>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#ifndef ROBOT_HARNESS_TEST_WORKER_MODE
#error "A test worker mode must be selected at compile time"
#endif

namespace {

using robot_harness::detail::compute_protocol::Frame;
using robot_harness::detail::compute_protocol::FrameKind;

bool parse_iterations(int argc, char* argv[], std::uint64_t* iterations) noexcept {
  if (argc != 3 || std::string_view(argv[1]) != "--iterations") {
    return false;
  }
  const std::string_view argument(argv[2]);
  const auto conversion =
      std::from_chars(argument.data(), argument.data() + argument.size(), *iterations);
  return conversion.ec == std::errc{} && conversion.ptr == argument.data() + argument.size() &&
         *iterations > 0 &&
         *iterations <= robot_harness::detail::compute_protocol::kMaximumIterations;
}

[[maybe_unused]] bool send_required(const Frame& frame) noexcept {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
  while (std::chrono::steady_clock::now() < deadline) {
    const ssize_t count =
        ::write(robot_harness::detail::compute_protocol::kEventDescriptor, &frame, sizeof(frame));
    if (count == static_cast<ssize_t>(sizeof(frame))) {
      return true;
    }
    if (count >= 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
      return false;
    }
    struct pollfd descriptor{robot_harness::detail::compute_protocol::kEventDescriptor, POLLOUT, 0};
    if (::poll(&descriptor, 1, 1) < 0 && errno != EINTR) {
      return false;
    }
  }
  return false;
}

int run_fixture(std::uint64_t limit) noexcept {
  using namespace robot_harness::detail::compute_protocol;

#if ROBOT_HARNESS_TEST_WORKER_MODE == 4
  (void)limit;
  return kFailureExitCode;
#elif ROBOT_HARNESS_TEST_WORKER_MODE == 3
  (void)limit;
  const unsigned char truncated = 0x7f;
  ::write(kEventDescriptor, &truncated, sizeof(truncated));
  return kFailureExitCode;
#else
#if ROBOT_HARNESS_TEST_WORKER_MODE == 2
  ::poll(nullptr, 0, 100);
#endif
  if (!send_required(Frame{})) {
    return kFailureExitCode;
  }

#if ROBOT_HARNESS_TEST_WORKER_MODE == 9
  ::close(kEventDescriptor);
  ::poll(nullptr, 0, 3000);  // Test-only finite lifetime after losing all event reporting.
  return kFailureExitCode;
#elif ROBOT_HARNESS_TEST_WORKER_MODE == 10
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  unsigned char stop = 0;
  while (::read(kControlDescriptor, &stop, 1) != 1) {
    if (std::chrono::steady_clock::now() >= end)
      return kFailureExitCode;
    ::poll(nullptr, 0, 1);
  }
  if (stop != kStopCommand ||
      !send_required({kFrameMagic, kFrameVersion, FrameKind::kProgress, 10, 0, 0}) ||
      !send_required({kFrameMagic, kFrameVersion, FrameKind::kStopAcknowledged, 100, 0, 0}) ||
      !send_required({kFrameMagic, kFrameVersion, FrameKind::kCancelled, 50, 0, 0})) {
    return kFailureExitCode;
  }
  return kCancelledExitCode;
#endif

#if ROBOT_HARNESS_TEST_WORKER_MODE == 7
  ::close(kControlDescriptor);
  ::poll(nullptr, 0, 3000);
  return kFailureExitCode;
#endif

  std::uint64_t completed = 0;
  std::uint64_t sum = 0;
  std::uint64_t dropped = 0;
  while (completed < limit) {
#if ROBOT_HARNESS_TEST_WORKER_MODE != 5
    unsigned char command = 0;
    const ssize_t received = ::read(kControlDescriptor, &command, sizeof(command));
    if (received == static_cast<ssize_t>(sizeof(command))) {
      if (command != kStopCommand) {
        return kFailureExitCode;
      }
#if ROBOT_HARNESS_TEST_WORKER_MODE == 1
      const Frame ignored{kFrameMagic, kFrameVersion, FrameKind::kStopIgnored,
                          completed,   sum,           dropped};
      if (!send_required(ignored)) {
        return kFailureExitCode;
      }
#else
      const Frame acknowledged{kFrameMagic, kFrameVersion, FrameKind::kStopAcknowledged,
                               completed,   sum,           dropped};
      const Frame cancelled{kFrameMagic, kFrameVersion, FrameKind::kCancelled,
                            completed,   sum,           dropped};
      if (!send_required(acknowledged) || !send_required(cancelled)) {
        return kFailureExitCode;
      }
      return kCancelledExitCode;
#endif
    } else if (received == 0) {
      return kFailureExitCode;
    } else if (received < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      return kFailureExitCode;
    }
#endif

#if ROBOT_HARNESS_TEST_WORKER_MODE == 8
    const std::uint64_t end =
        completed + 1;  // Real work, deliberately saturating progress traffic.
#else
    const std::uint64_t end = std::min(completed + kBatchIterations, limit);
#endif
    for (; completed < end; ++completed) {
      const std::uint64_t value = completed % 1000;
      sum += value * value;
    }
    const Frame progress{kFrameMagic, kFrameVersion, FrameKind::kProgress, completed, sum, dropped};
    const ssize_t count = ::write(kEventDescriptor, &progress, sizeof(progress));
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      ++dropped;
    } else if (count != static_cast<ssize_t>(sizeof(progress))) {
      return kFailureExitCode;
    }
  }

  const Frame completed_frame{kFrameMagic, kFrameVersion, FrameKind::kCompleted,
                              completed,   sum,           dropped};
  if (!send_required(completed_frame)) {
    return kFailureExitCode;
  }
#if ROBOT_HARNESS_TEST_WORKER_MODE == 6
  ::poll(nullptr, 0, 1000);
#endif
  return 0;
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  struct sigaction action{};
  action.sa_handler = SIG_IGN;
  sigemptyset(&action.sa_mask);
  if (::sigaction(SIGPIPE, &action, nullptr) != 0) {
    return robot_harness::detail::compute_protocol::kFailureExitCode;
  }

  std::uint64_t iterations = 0;
  if (!parse_iterations(argc, argv, &iterations)) {
    return robot_harness::detail::compute_protocol::kFailureExitCode;
  }
  return run_fixture(iterations);
}
