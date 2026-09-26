#include "obstacle_watch.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using robot_harness::nav2_example::ObstacleDecision;
using robot_harness::nav2_example::ObstacleWatch;
void expect(bool condition) {
  if (!condition) {
    std::cerr << "obstacle policy mismatch\n";
    std::exit(1);
  }
}
int main() {
  ObstacleWatch watch;
  expect(watch.decision(1000, 1000000000) == ObstacleDecision::kUnknown);
  watch.observe_scan(1000000000, 1000, 2.0, true);
  watch.observe_costmap(1000000000, 1000, false, true);
  expect(watch.ready(1000, 1000000000));
  watch.observe_costmap(1100000000, 1100, true, true);
  for (int i = 1; i <= 2; ++i)
    watch.observe_scan(1000000000 + i * 100000000, 1000 + i * 100, .8, true);
  expect(watch.decision(1200, 1200000000) == ObstacleDecision::kContinue);
  watch.observe_scan(1300000000, 1300, .8, true);
  expect(watch.decision(1300, 1300000000) == ObstacleDecision::kBlocked);
  // Replayed scan cannot renew freshness or create clear-road evidence.
  watch.observe_scan(1300000000, 2800, 9.0, true);
  expect(watch.decision(2800, 2800000000) == ObstacleDecision::kUnknown);
  watch.observe_scan(2900000000, 2900, std::numeric_limits<double>::quiet_NaN(), true);
  expect(watch.decision(2900, 2900000000) == ObstacleDecision::kUnknown);
  watch.observe_scan(3000000000, 3000, 2.0, true);
  watch.observe_costmap(3000000000, 3000, false, false);
  expect(watch.decision(3000, 3000000000) == ObstacleDecision::kUnknown);
  watch.observe_costmap(3100000000, 3100, false, true);
  expect(watch.ready(3100, 3100000000));
  expect(watch.decision(4600, 4600000000) == ObstacleDecision::kUnknown);
  // Future clocks and costmap-only obstruction are not a blocked-road verdict.
  expect(watch.decision(3100, 2000000000) == ObstacleDecision::kUnknown);
  watch.observe_scan(3200000000, 3200, .8, true);
  watch.observe_scan(3300000000, 3300, .8, true);
  watch.observe_scan(3400000000, 3400, .8, true);
  expect(watch.decision(3400, 3400000000) == ObstacleDecision::kContinue);
  ObstacleWatch delayed;
  delayed.observe_scan(1000000000, 1000, .8, true);
  delayed.observe_scan(1100000000, 1100, .8, true);
  delayed.observe_scan(4000000000, 4000, .8, true);
  delayed.observe_costmap(4000000000, 4000, true, true);
  expect(delayed.decision(4000, 4000000000) == ObstacleDecision::kContinue);
  delayed.observe_scan(4100000000, 4100, .8, true);
  expect(delayed.decision(4100, 4100000000) == ObstacleDecision::kContinue);
  delayed.observe_scan(4200000000, 4200, .8, true);
  expect(delayed.decision(4200, 4200000000) == ObstacleDecision::kBlocked);
}
