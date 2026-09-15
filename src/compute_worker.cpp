#include "compute_protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <system_error>

#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;
using robot_harness::detail::compute_protocol::Frame;
using robot_harness::detail::compute_protocol::FrameKind;

constexpr auto kRequiredFrameDeadline = std::chrono::milliseconds(1000);

enum class SendResult { kSent, kDropped, kFailed };

SendResult send_frame(const Frame& frame, bool required) noexcept {
  const auto deadline = Clock::now() + kRequiredFrameDeadline;
  while (true) {
    const ssize_t count =
        ::write(robot_harness::detail::compute_protocol::kEventDescriptor, &frame, sizeof(frame));
    if (count == static_cast<ssize_t>(sizeof(frame))) {
      return SendResult::kSent;
    }
    if (count >= 0) {
      return SendResult::kFailed;
    }
    if (errno == EINTR) {
      if (!required) {
        return SendResult::kDropped;
      }
      if (Clock::now() >= deadline) {
        return SendResult::kFailed;
      }
      continue;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      return SendResult::kFailed;
    }
    if (!required) {
      return SendResult::kDropped;
    }

    const auto now = Clock::now();
    if (now >= deadline) {
      return SendResult::kFailed;
    }
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    remaining = std::max<std::int64_t>(1, std::min<std::int64_t>(remaining, 10));
    struct pollfd descriptor{robot_harness::detail::compute_protocol::kEventDescriptor, POLLOUT, 0};
    const int result = ::poll(&descriptor, 1, static_cast<int>(remaining));
    if (result < 0 && errno != EINTR) {
      return SendResult::kFailed;
    }
  }
}

bool install_local_sigpipe_handling() noexcept {
  struct sigaction action{};
  action.sa_handler = SIG_IGN;
  if (sigemptyset(&action.sa_mask) != 0) {
    return false;
  }
  return ::sigaction(SIGPIPE, &action, nullptr) == 0;
}

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

int run(std::uint64_t limit) noexcept {
  using namespace robot_harness::detail::compute_protocol;

  if (send_frame(Frame{}, true) != SendResult::kSent) {
    return kFailureExitCode;
  }

  std::uint64_t completed = 0;
  std::uint64_t sum = 0;
  std::uint64_t dropped = 0;
  while (completed < limit) {
    unsigned char command = 0;
    const ssize_t received = ::read(kControlDescriptor, &command, sizeof(command));
    if (received == static_cast<ssize_t>(sizeof(command))) {
      if (command != kStopCommand) {
        return kFailureExitCode;
      }
      const Frame acknowledged{kFrameMagic, kFrameVersion, FrameKind::kStopAcknowledged,
                               completed,   sum,           dropped};
      if (send_frame(acknowledged, true) != SendResult::kSent) {
        return kFailureExitCode;
      }
      const Frame cancelled{kFrameMagic, kFrameVersion, FrameKind::kCancelled,
                            completed,   sum,           dropped};
      if (send_frame(cancelled, true) != SendResult::kSent) {
        return kFailureExitCode;
      }
      return kCancelledExitCode;
    }
    if (received == 0) {
      return kFailureExitCode;
    }
    if (received < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      return kFailureExitCode;
    }

    const std::uint64_t end = std::min(completed + kBatchIterations, limit);
    for (; completed < end; ++completed) {
      const std::uint64_t value = completed % 1000;
      sum += value * value;
    }
    const Frame progress{kFrameMagic, kFrameVersion, FrameKind::kProgress, completed, sum, dropped};
    const SendResult progress_result = send_frame(progress, false);
    if (progress_result == SendResult::kDropped) {
      ++dropped;
    } else if (progress_result == SendResult::kFailed) {
      return kFailureExitCode;
    }
  }

  const Frame completed_frame{kFrameMagic, kFrameVersion, FrameKind::kCompleted,
                              completed,   sum,           dropped};
  return send_frame(completed_frame, true) == SendResult::kSent ? 0 : kFailureExitCode;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::uint64_t iterations = 0;
  if (!install_local_sigpipe_handling() || !parse_iterations(argc, argv, &iterations)) {
    return robot_harness::detail::compute_protocol::kFailureExitCode;
  }
  return run(iterations);
}
