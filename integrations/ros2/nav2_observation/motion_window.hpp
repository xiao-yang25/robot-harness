#ifndef ROBOT_HARNESS_NAV2_MOTION_WINDOW_HPP_
#define ROBOT_HARNESS_NAV2_MOTION_WINDOW_HPP_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace robot_harness::nav2_example {

struct MotionSample {
  std::int64_t stamp_ns;
  double x;
  double y;
  double yaw;
  double speed;
  double angular_speed;
};

enum class MotionStatus { kPending, kObserved, kRejected };

// One finite planar observation after a result. Never grants settlement authority.
class MotionWindow {
public:
  explicit MotionWindow(std::int64_t after_stamp_ns) : after_stamp_ns_(after_stamp_ns) {
  }

  void observe(const MotionSample& sample, std::int64_t clock_ns, std::int64_t clock_age_ms) {
    if (status_ != MotionStatus::kPending) {
      return;
    }
    if (sample.stamp_ns <= after_stamp_ns_) {
      return;  // A retained pre-window observation cannot establish new progress.
    }
    if (!first_ && clock_ns < after_stamp_ns_) {
      return;  // Wait for the independently published clock to enter this window.
    }
    if (!std::isfinite(sample.x) || !std::isfinite(sample.y) || !std::isfinite(sample.yaw) ||
        !std::isfinite(sample.speed) || !std::isfinite(sample.angular_speed) || sample.speed < 0 ||
        clock_age_ms < 0 || clock_age_ms > 1000 || clock_ns < after_stamp_ns_ ||
        std::abs(static_cast<long double>(sample.stamp_ns) - clock_ns) > 250000000.0L) {
      status_ = MotionStatus::kRejected;
      return;
    }
    if (previous_ && (sample.stamp_ns <= previous_->stamp_ns ||
                      sample.stamp_ns - previous_->stamp_ns > 250000000)) {
      status_ = MotionStatus::kRejected;
      return;
    }
    if (!first_) {
      if (sample.speed > 0.01 || std::abs(sample.angular_speed) > 0.02) {
        previous_ = sample;
        ++settling_sample_count_;
        return;  // Bounded deceleration capture, not part of the quiet window.
      }
      first_ = sample;
    }
    const auto yaw_change =
        std::atan2(std::sin(sample.yaw - first_->yaw), std::cos(sample.yaw - first_->yaw));
    if (std::hypot(sample.x - first_->x, sample.y - first_->y) > 0.01 ||
        std::abs(yaw_change) > 0.02 || sample.speed > 0.01 ||
        std::abs(sample.angular_speed) > 0.02) {
      status_ = MotionStatus::kRejected;
      return;
    }
    previous_ = sample;
    ++sample_count_;
    max_speed_ = std::max(max_speed_, sample.speed);
    if (sample_count_ >= 10 && sample.stamp_ns - first_->stamp_ns >= 1000000000) {
      status_ = MotionStatus::kObserved;
    }
  }

  void reject() {
    status_ = MotionStatus::kRejected;
  }
  MotionStatus status() const {
    return status_;
  }
  std::size_t sample_count() const {
    return sample_count_;
  }
  std::size_t settling_sample_count() const {
    return settling_sample_count_;
  }
  double max_speed() const {
    return max_speed_;
  }
  std::int64_t duration_ns() const {
    return first_ && previous_ ? previous_->stamp_ns - first_->stamp_ns : 0;
  }

private:
  std::int64_t after_stamp_ns_;
  MotionStatus status_ = MotionStatus::kPending;
  std::optional<MotionSample> first_, previous_;
  std::size_t sample_count_ = 0, settling_sample_count_ = 0;
  double max_speed_ = 0;
};

}  // namespace robot_harness::nav2_example
#endif
