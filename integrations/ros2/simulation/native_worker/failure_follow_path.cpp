#include "scoped_action.hpp"
#include <behaviortree_cpp_v3/bt_factory.h>
#include <nav2_msgs/action/follow_path.hpp>
class FailureFollowPath : public robot_harness_nav2::TrackedAction<nav2_msgs::action::FollowPath> {
  using Base = robot_harness_nav2::TrackedAction<nav2_msgs::action::FollowPath>;

public:
  FailureFollowPath(const std::string& name, const BT::NodeConfiguration& config)
      : Base(name, "follow_path", config, "navigation_child_link") {
  }
  static BT::PortsList providedPorts() {
    return providedBasicPorts({BT::InputPort<nav_msgs::msg::Path>("path"),
                               BT::InputPort<std::string>("controller_id"),
                               BT::InputPort<std::string>("scope_id")});
  }
  void on_tick() override {
    if (!getInput("path", goal_.path) || !getInput("controller_id", goal_.controller_id))
      throw std::runtime_error("missing controller inputs");
  }
};
BT_REGISTER_NODES(factory) {
  factory.registerNodeType<FailureFollowPath>("FailureFollowPath");
}
