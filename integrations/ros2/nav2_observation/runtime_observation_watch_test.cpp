#include "runtime_observation_watch.hpp"

#include <stdexcept>

namespace {
using robot_harness::nav2_example::RuntimeObservationWatch;
using robot_harness::nav2_example::StaleObservation;
void check(bool value) {
  if (!value)
    throw std::runtime_error("runtime observation progress check failed");
}
}  // namespace

int main() {
  RuntimeObservationWatch watch;
  check(!watch.stale(9000));
  watch.observe_clock(100, 1000);
  watch.observe_odometry(100, 1000, true);
  watch.start(1100);
  check(!watch.stale(3099));
  check(watch.stale(3100) == StaleObservation::kClock);
  // Restoration processed before the next Owner poll still latches the gap.
  watch.observe_clock(200, 3100);
  watch.observe_odometry(200, 3101, true);
  check(watch.stale(3101) == StaleObservation::kClock);

  RuntimeObservationWatch odometry;
  odometry.start(1000);
  odometry.observe_clock(100, 1000);
  odometry.observe_odometry(100, 1000, true);
  odometry.observe_clock(200, 2999);
  odometry.observe_odometry(100, 2999, true);
  odometry.observe_odometry(99, 2999, true);
  odometry.observe_odometry(200, 2999, false);
  check(!odometry.stale(2999));
  odometry.observe_odometry(200, 3000, true);
  check(odometry.stale(3000) == StaleObservation::kOdometry);
  // Only a new execution's explicit start resets the detector, not fresh data.
  odometry.start(4000);
  odometry.observe_clock(300, 5999);
  odometry.observe_odometry(200, 5999, true);
  check(odometry.stale(6000) == StaleObservation::kOdometry);

  RuntimeObservationWatch healthy;
  healthy.start(1000);
  for (unsigned index = 1; index <= 10; ++index) {
    const auto at = 1000 + index * 1900;
    healthy.observe_clock(index, at);
    healthy.observe_odometry(index, at, true);
    check(!healthy.stale(at));
  }
}
