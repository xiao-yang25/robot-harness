#include "closure_observations.hpp"
#include <iostream>
#include <stdexcept>

using robot_harness::nav2_example::ClosureObservations;
using Json = nlohmann::json;
namespace {
const std::string scope(32, 'a'), parent(32, 'b'), planner(32, 'c'), controller(32, 'd');
void require(bool value) {
  if (!value) {
    throw std::runtime_error("closure test failed");
  }
}
Json link(const char* action, const std::string& id) {
  return {{"event", "navigation_child_link"},
          {"scope_id", scope},
          {"action", action},
          {"child_uuid", id}};
}
Json ack(const char* action, const std::string& id) {
  return {{"event", std::string(action) + "_worker_ack"},
          {"goal_uuid", id},
          {"worker_future_ready", true},
          {"endpoint_sealed", true}};
}
Json bt() {
  return {{"event", "navigation_bt_closed"}, {"scope_id", scope}, {"bt_worker_joined", true},
          {"endpoint_sealed", true},         {"tree_idle", true}, {"steady_ns", 300}};
}
Json drive() {
  return {{"event", "motion_scope_applied"},
          {"scope_id", scope},
          {"sealed", true},
          {"wheel_targets_zero", true},
          {"steady_ns", 500},
          {"update_sequence", 5},
          {"sim_ns", 600}};
}
void workers(ClosureObservations& facts) {
  // DDS can deliver acknowledgements before the corresponding child links.
  facts.observe_child(0, true, ack("compute_path_to_pose", planner).dump());
  facts.observe_child(1, true, ack("follow_path", controller).dump());
  facts.observe_child(0, false, link("compute_path_to_pose", planner).dump());
  facts.observe_child(1, false, link("follow_path", controller).dump());
  require(!facts.workers_closed());
  facts.bind_parent(parent);
  require(facts.workers_closed() && !facts.closed());
}
template <class Attempt> void rejected(ClosureObservations& facts, Attempt attempt) {
  bool threw = false;
  try {
    attempt();
  } catch (const std::exception&) {
    threw = true;
  }
  require(threw && facts.rejected() && !facts.closed());
}
}  // namespace
int main() try {
  ClosureObservations good(scope);
  workers(good);
  auto closure = bt();
  closure["optional_metadata"] = {{"ignored", true}};
  good.observe_bt(closure.dump(), 250, 350);
  require(!good.closed());
  good.observe_drive(drive().dump(), 450, 550);
  require(good.closed() && good.drive_stamp_ns() == 600);
  good.observe_child(1, true, ack("follow_path", controller).dump());
  require(good.closed());  // Exact duplicate does not revoke a sound observation.

  ClosureObservations missing(scope);
  missing.bind_parent(parent);
  missing.observe_child(1, false, link("follow_path", controller).dump());
  missing.observe_child(1, true, ack("follow_path", controller).dump());
  require(!missing.workers_closed() && !missing.closed());

  ClosureObservations wrong(scope);
  auto wrong_link = link("follow_path", controller);
  wrong_link["scope_id"] = parent;
  rejected(wrong, [&] { wrong.observe_child(1, false, wrong_link.dump()); });

  ClosureObservations mismatch(scope);
  mismatch.observe_child(1, false, link("follow_path", controller).dump());
  rejected(mismatch, [&] { mismatch.observe_child(1, true, ack("follow_path", planner).dump()); });

  ClosureObservations early(scope);
  workers(early);
  early.observe_bt(bt().dump(), 250, 350);
  rejected(early, [&] { early.observe_drive(drive().dump(), 501, 550); });

  ClosureObservations admission(scope);
  workers(admission);
  admission.observe_bt(bt().dump(), 250, 350);
  rejected(admission, [&] { admission.observe_drive("{\"accepted\":true}", 450, 550); });

  ClosureObservations malformed(scope);
  workers(malformed);
  malformed.observe_bt(bt().dump(), 250, 350);
  auto invalid = drive();
  invalid["wheel_targets_zero"] = "true";
  rejected(malformed, [&] { malformed.observe_drive(invalid.dump(), 450, 550); });

  ClosureObservations missing_field(scope);
  workers(missing_field);
  missing_field.observe_bt(bt().dump(), 250, 350);
  missing_field.observe_drive(drive().dump(), 450, 550);
  auto partial = ack("follow_path", controller);
  partial.erase("goal_uuid");
  rejected(missing_field, [&] { missing_field.observe_child(1, true, partial.dump()); });
  ClosureObservations generation(scope, 2);
  auto retained = drive();
  retained["generation"] = 1;
  retained["scope_id"] = parent;
  generation.observe_drive(retained.dump(), 0, 550);
  require(!generation.rejected() && !generation.closed());
  workers(generation);
  generation.observe_bt(bt().dump(), 250, 350);
  auto current = drive();
  current["generation"] = 2;
  generation.observe_drive(current.dump(), 450, 550);
  require(generation.closed());

  ClosureObservations wrong_generation(scope, 2);
  current["generation"] = 3;
  rejected(wrong_generation, [&] { wrong_generation.observe_drive(current.dump(), 450, 550); });
  ClosureObservations missing_generation(scope, 2);
  rejected(missing_generation, [&] { missing_generation.observe_drive(drive().dump(), 450, 550); });

  std::cout << "closure identity, missing evidence, ordering and malformed observations passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
