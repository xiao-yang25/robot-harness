#ifndef ROBOT_HARNESS_NAV2_OBSTACLE_WATCH_HPP_
#define ROBOT_HARNESS_NAV2_OBSTACLE_WATCH_HPP_

#include <array>
#include <cmath>
#include <cstdint>

namespace robot_harness::nav2_example {

enum class ObstacleDecision { kContinue, kBlocked, kUnknown };

// Fixed-corridor caller policy, not an authority or physical-safety predicate.
// Receipt of a duplicate stamp cannot renew observation freshness.
class ObstacleWatch {
public:
  void observe_scan(std::int64_t stamp, std::int64_t received_ms, double clearance, bool valid) {
    if (stamp <= scan_.stamp)
      return;
    scan_ = {stamp, received_ms, valid && std::isfinite(clearance) && clearance >= 0};
    clearance_ = clearance;
    if (scan_.valid && clearance <= 1.05) {
      close_window_[0] = close_window_[1];
      close_window_[1] = close_window_[2];
      close_window_[2] = scan_;
    } else {
      close_window_ = {};
    }
  }

  void observe_costmap(std::int64_t stamp, std::int64_t received_ms, bool occupied, bool valid) {
    if (stamp <= costmap_.stamp)
      return;
    costmap_ = {stamp, received_ms, valid};
    occupied_ = occupied;
  }

  bool ready(std::int64_t at_ms, std::int64_t clock_ns) const {
    return scan_fresh(at_ms, clock_ns) && costmap_fresh(at_ms, clock_ns) && clearance_ > 1.3 &&
           !occupied_;
  }

  ObstacleDecision decision(std::int64_t at_ms, std::int64_t clock_ns) const {
    if (!scan_fresh(at_ms, clock_ns) || !costmap_fresh(at_ms, clock_ns))
      return ObstacleDecision::kUnknown;
    for (const auto& sample : close_window_)
      if (!fresh(sample, at_ms, clock_ns, 1500))
        return ObstacleDecision::kContinue;
    return occupied_ ? ObstacleDecision::kBlocked : ObstacleDecision::kContinue;
  }

  bool scan_fresh(std::int64_t at_ms, std::int64_t clock_ns) const {
    return fresh(scan_, at_ms, clock_ns, 1500);
  }
  bool costmap_fresh(std::int64_t at_ms, std::int64_t clock_ns) const {
    return fresh(costmap_, at_ms, clock_ns, 3000);
  }
  std::int64_t scan_progress_ms() const {
    return scan_.received;
  }
  double clearance() const {
    return clearance_;
  }
  bool occupied() const {
    return occupied_;
  }
  std::int64_t scan_stamp() const {
    return scan_.stamp;
  }
  std::int64_t costmap_stamp() const {
    return costmap_.stamp;
  }

private:
  struct Sample {
    std::int64_t stamp = 0;
    std::int64_t received = 0;
    bool valid = false;
  };
  static bool fresh(const Sample& sample, std::int64_t at, std::int64_t clock,
                    std::int64_t limit_ms) {
    return sample.valid && sample.stamp > 0 && at >= sample.received &&
           at - sample.received < limit_ms && clock > 0 && sample.stamp <= clock + 250000000 &&
           clock - sample.stamp < limit_ms * 1000000;
  }
  Sample scan_;
  Sample costmap_;
  double clearance_ = 0;
  bool occupied_ = false;
  std::array<Sample, 3> close_window_{};
};
}  // namespace robot_harness::nav2_example

#endif
