#ifndef ROBOT_HARNESS_NAV2_RUNTIME_OBSERVATION_WATCH_HPP_
#define ROBOT_HARNESS_NAV2_RUNTIME_OBSERVATION_WATCH_HPP_

#include <cstdint>
#include <optional>

namespace robot_harness::nav2_example {

enum class StaleObservation { kClock, kOdometry };

// Private progress detector. Monotonic receipt time, not simulation time, ages
// each stream. Repeated/backward stamps and invalid samples cannot renew it.
// It grants no authority; withdrawal and recovery belong to the owning caller.
class RuntimeObservationWatch {
public:
  static constexpr std::uint64_t kBudgetMs = 2000;

  void start(std::uint64_t at) {
    lost_.reset();
    active_ = true;
    clock_.advanced_at = odometry_.advanced_at = at;
  }
  void observe_clock(std::int64_t stamp, std::uint64_t at) {
    latch_expiry(at);
    clock_.observe(stamp, at);
  }
  void observe_odometry(std::int64_t stamp, std::uint64_t at, bool valid) {
    latch_expiry(at);
    if (valid)
      odometry_.observe(stamp, at);
  }
  std::optional<StaleObservation> stale(std::uint64_t at) const {
    if (lost_)
      return lost_;
    if (active_ && clock_.expired(at))
      return StaleObservation::kClock;
    if (active_ && odometry_.expired(at))
      return StaleObservation::kOdometry;
    return {};
  }

private:
  void latch_expiry(std::uint64_t at) {
    // A queued restoration callback can precede the next Owner poll. It must
    // not erase the already-expired interval by renewing the receipt time.
    if (!lost_)
      lost_ = stale(at);
  }
  struct Stream {
    std::int64_t stamp = 0;
    std::uint64_t advanced_at = 0;
    void observe(std::int64_t value, std::uint64_t at) {
      if (value > stamp && at >= advanced_at) {
        stamp = value;
        advanced_at = at;
      }
    }
    bool expired(std::uint64_t at) const {
      return at >= advanced_at && at - advanced_at >= kBudgetMs;
    }
  };
  std::optional<StaleObservation> lost_;
  bool active_ = false;
  Stream clock_, odometry_;
};
}  // namespace robot_harness::nav2_example
#endif
