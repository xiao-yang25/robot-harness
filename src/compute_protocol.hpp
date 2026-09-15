#ifndef ROBOT_HARNESS_COMPUTE_PROTOCOL_HPP_
#define ROBOT_HARNESS_COMPUTE_PROTOCOL_HPP_

#include <cstddef>
#include <cstdint>

namespace robot_harness::detail::compute_protocol {

constexpr int kControlDescriptor = 3;
constexpr int kEventDescriptor = 4;
constexpr std::uint32_t kFrameMagic = 0x52484350U;
constexpr std::uint16_t kFrameVersion = 1;
constexpr std::uint64_t kMaximumIterations = 100000000;
constexpr std::uint64_t kBatchIterations = 4096;
constexpr int kCancelledExitCode = 20;
constexpr int kFailureExitCode = 21;
constexpr unsigned char kStopCommand = 0x53;

enum class FrameKind : std::uint16_t {
  kReady = 1,
  kProgress = 2,
  kStopAcknowledged = 3,
  kStopIgnored = 4,
  kCompleted = 5,
  kCancelled = 6,
};

struct Frame {
  std::uint32_t magic = kFrameMagic;
  std::uint16_t version = kFrameVersion;
  FrameKind kind = FrameKind::kReady;
  std::uint64_t iterations = 0;
  std::uint64_t value = 0;
  std::uint64_t dropped_progress = 0;
};

static_assert(sizeof(Frame) == 32, "compute protocol frame layout changed");
static_assert(sizeof(Frame) <= 512, "compute protocol frame must fit POSIX minimum PIPE_BUF");

}  // namespace robot_harness::detail::compute_protocol

#endif  // ROBOT_HARNESS_COMPUTE_PROTOCOL_HPP_
