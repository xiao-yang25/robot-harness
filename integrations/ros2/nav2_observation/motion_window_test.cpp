#include "motion_window.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using robot_harness::nav2_example::MotionSample;
using robot_harness::nav2_example::MotionStatus;
using robot_harness::nav2_example::MotionWindow;
void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("motion observation counterexample failed");
  }
}
MotionSample quiet(std::int64_t stamp) {
  return {stamp, 0, 0, 0, 0, 0};
}
void complete(MotionWindow& window) {
  for (int i = 1; i <= 11; ++i) {
    window.observe(quiet(i * 100000000LL), i * 100000000LL, 0);
  }
}
}  // namespace
int main() {
  try {
    MotionWindow normal(0);
    complete(normal);
    require(normal.status() == MotionStatus::kObserved && normal.sample_count() == 11 &&
            normal.duration_ns() == 1000000000);
    // Action arrival may precede deceleration. The one quiet window starts only
    // after the first low-speed sample; motion during that window still rejects.
    MotionWindow decelerating(0);
    auto moving = quiet(10000000);
    moving.speed = 0.09;
    moving.angular_speed = 0.26;
    decelerating.observe(moving, 10000000, 0);
    complete(decelerating);
    require(decelerating.status() == MotionStatus::kObserved);
    // Clock and odometry topics have different publication rates.
    MotionWindow clock_lag(150000000);
    clock_lag.observe(quiet(200000000), 100000000, 0);
    require(clock_lag.status() == MotionStatus::kPending);
    for (int i = 3; i <= 13; ++i) {
      clock_lag.observe(quiet(i * 100000000LL), i * 100000000LL, 0);
    }
    require(clock_lag.status() == MotionStatus::kObserved);
    MotionWindow retained(2000000000);
    retained.observe(quiet(100000000), 2500000000, 0);
    require(retained.status() == MotionStatus::kPending);
    for (int i = 26; i <= 36; ++i) {
      retained.observe(quiet(i * 100000000LL), i * 100000000LL, 0);
    }
    require(retained.status() == MotionStatus::kObserved);
    MotionWindow missing(0);
    missing.observe(quiet(100000000), 100000000, 0);
    missing.reject();
    complete(missing);
    require(missing.status() == MotionStatus::kRejected);
    for (int variant = 0; variant < 8; ++variant) {
      MotionWindow window(0);
      window.observe(quiet(100000000), 100000000, 0);
      auto sample = quiet(200000000);
      auto clock = sample.stamp_ns;
      std::int64_t age = 0;
      if (variant == 0)
        sample.x = 0.03;  // Drift despite zero reported velocity.
      if (variant == 1)
        sample.yaw = 0.1;
      if (variant == 2)
        sample.speed = 0.02;
      if (variant == 3)
        sample.speed = std::numeric_limits<double>::quiet_NaN();
      if (variant == 4)
        sample.stamp_ns = 100000000;  // Repeated observation.
      if (variant == 5) {
        sample.stamp_ns = 500000000;
        clock = sample.stamp_ns;
      }
      if (variant == 6)
        age = 1001;  // Frozen clock publication cannot certify motion.
      if (variant == 7)
        clock = 600000000;
      window.observe(sample, clock, age);
      complete(window);
      require(window.status() == MotionStatus::kRejected);
    }
    std::cout << "finite motion window and refusal counterexamples passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
