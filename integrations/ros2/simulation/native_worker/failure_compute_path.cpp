#include "scoped_action.hpp"
#include <behaviortree_cpp_v3/bt_factory.h>
#include <nav2_msgs/action/compute_path_to_pose.hpp>
class FailureComputePathToPose
    : public robot_harness_nav2::TrackedAction<nav2_msgs::action::ComputePathToPose> {
  using Base = robot_harness_nav2::TrackedAction<nav2_msgs::action::ComputePathToPose>;

public:
  FailureComputePathToPose(const std::string& name, const BT::NodeConfiguration& config)
      : Base(name, "compute_path_to_pose", config, "navigation_planner_link") {
  }
  static BT::PortsList providedPorts() {
    return providedBasicPorts({BT::InputPort<geometry_msgs::msg::PoseStamped>("goal"),
                               BT::OutputPort<nav_msgs::msg::Path>("path"),
                               BT::InputPort<std::string>("planner_id"),
                               BT::InputPort<std::string>("scope_id")});
  }
  void on_tick() override {
    goal_.use_start = false;
    if (!getInput("goal", goal_.goal) || !getInput("planner_id", goal_.planner_id))
      throw std::runtime_error("missing planner inputs");
  }
  BT::NodeStatus on_success() override {
    setOutput("path", result_.result->path);
    return Base::on_success();
  }
};
BT_REGISTER_NODES(factory) {
  factory.registerNodeType<FailureComputePathToPose>("FailureComputePathToPose");
}
