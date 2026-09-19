#ifndef ROBOT_HARNESS_RECOVERY_PROTOCOL_HPP_
#define ROBOT_HARNESS_RECOVERY_PROTOCOL_HPP_

#include <cerrno>
#include <cstdint>
#include <sys/socket.h>
#include <unistd.h>

// Linux-only, same-build prototype. Dedicated SOCK_SEQPACKET channels are
// provisioned by a trusted launcher; there is no public endpoint or durable wire ABI.
namespace robot_harness::detail::recovery {
constexpr std::uint64_t kMaximumIterations = 100000000;
constexpr std::uint64_t kEvidenceLifetimeMs = 1000;
constexpr std::uint32_t kMagic = 0x52485231;
enum class Kind : std::uint32_t {
  kHello = 1,
  kQuery,
  kSnapshot,
  kActivate,
  kActivated,
  kSubmit,
  kAccepted,
  kResult
};
constexpr std::uint64_t kClear = 1;
constexpr std::uint64_t kSucceeded = 1;
constexpr std::uint64_t kFailed = 2;
constexpr std::uint64_t kCancelled = 3;
constexpr std::uint64_t kNotLaunched = 4;
struct Message {
  std::uint32_t magic = kMagic;
  Kind kind = Kind::kHello;
  std::uint64_t session = 0;
  std::uint64_t request = 0;
  std::uint64_t operation = 0;
  std::uint64_t amount = 0;
  std::uint64_t value = 0;
  std::uint64_t flags = 0;
};
static_assert(sizeof(Message) == 56, "same-build recovery frame layout changed");
enum class Io { kDone, kAgain, kClosed };
inline Io send_message(int fd, const Message& message) noexcept {
  const auto n = ::send(fd, &message, sizeof(message), MSG_DONTWAIT | MSG_NOSIGNAL);
  if (n == static_cast<ssize_t>(sizeof(message)))
    return Io::kDone;
  if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
    return Io::kAgain;
  return Io::kClosed;
}
inline Io receive_message(int fd, Message& message) noexcept {
  const auto n = ::recv(fd, &message, sizeof(message), MSG_DONTWAIT | MSG_TRUNC);
  if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
    return Io::kAgain;
  return n == static_cast<ssize_t>(sizeof(message)) && message.magic == kMagic ? Io::kDone
                                                                               : Io::kClosed;
}
}  // namespace robot_harness::detail::recovery
#endif
