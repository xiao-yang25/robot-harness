#include "native_leaf_evidence.hpp"
#include <cassert>
int main() {
  robot_harness_nav2::LeafRecord never;
  assert(never.kind() == "not_submitted");
  robot_harness_nav2::LeafRecord pending;
  pending.enter();
  assert(pending.kind() == "unknown");  // Even if no handle was ever received.
  pending.associate(std::string(32, '1'), "failed");
  assert(pending.kind() == "terminal" && pending.terminal == "failed");
  pending.enter();
  assert(pending.kind() == "terminal");  // Further ticks cannot restore never-entered.
  pending.associate(std::string(32, '2'), "failed");
  assert(pending.kind() == "unknown");  // Contradiction latches.
  robot_harness_nav2::LeafRecord invalid;
  invalid.associate(std::string(32, '1'), "failed");
  assert(invalid.kind() == "unknown");
}
