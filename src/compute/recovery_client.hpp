#ifndef ROBOT_HARNESS_RECOVERY_CLIENT_HPP_
#define ROBOT_HARNESS_RECOVERY_CLIENT_HPP_
#include "robot_harness/authority_gate.hpp"
#include <memory>
#include <optional>

namespace robot_harness::detail::recovery {
enum class ClientState { kConnecting, kBlocked, kRecovering, kReady, kClosed };
// Private Linux prototype. Consumes a dedicated launcher-provisioned descriptor.
// Single logical writer; times belong to this Host's monotonic clock.
class Client final {
public:
  Client(int descriptor, MonotonicTime now);
  ~Client();
  void poll(MonotonicTime now);
  bool recover(MonotonicTime now);
  bool submit(std::uint64_t iterations, MonotonicTime now);
  void close() noexcept;
  ClientState state() const noexcept;
  std::uint64_t session() const noexcept;
  std::optional<std::uint64_t> result() const noexcept;
  std::optional<OperationReceipt> receipt() const;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace robot_harness::detail::recovery
#endif
