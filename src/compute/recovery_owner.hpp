#ifndef ROBOT_HARNESS_RECOVERY_OWNER_HPP_
#define ROBOT_HARNESS_RECOVERY_OWNER_HPP_

#include "compute_process.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace robot_harness::detail::recovery {
// Private Linux prototype, one trusted scope. Call on one owner thread in a
// process that outlives the Host. Never copy this owner into a restarted Host.
class Owner final {
public:
  explicit Owner(std::string worker, std::uint64_t stop_grace_ms = 100);
  ~Owner();
  // Consumes fd on success only. Refuses replacement of a live session.
  bool attach(int fd, std::uint64_t now);
  void poll(std::uint64_t now);
  bool connected() const noexcept;
  bool clear() const noexcept;
  bool faulted() const noexcept;
  std::uint64_t session() const noexcept;
  std::uint64_t launches() const noexcept;
  std::uint64_t discarded_results() const noexcept;
  ProcessObservation observation() const;
  Owner(const Owner&) = delete;
  Owner& operator=(const Owner&) = delete;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace robot_harness::detail::recovery
#endif
