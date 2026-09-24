// Research-only request-context propagation at the actual native BT leaf.
#include <behaviortree_cpp_v3/bt_factory.h>
#include <nav2_behavior_tree/bt_action_node.hpp>
#include <nav2_msgs/action/compute_path_to_pose.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
class ScopedComputePathToPose
    : public nav2_behavior_tree::BtActionNode<nav2_msgs::action::ComputePathToPose> {
public:
  ScopedComputePathToPose(const std::string& name, const BT::NodeConfiguration& config)
      : BtActionNode(name, "compute_path_to_pose", config) {
    link_ = node_->create_publisher<std_msgs::msg::String>(
        "navigation_planner_link", rclcpp::QoS(1).reliable().transient_local());
  }

  static BT::PortsList providedPorts() {
    return providedBasicPorts({BT::InputPort<geometry_msgs::msg::PoseStamped>("goal"),
                               BT::OutputPort<nav_msgs::msg::Path>("path"),
                               BT::InputPort<std::string>("planner_id"),
                               BT::InputPort<std::string>("scope_id")});
  }

  void on_tick() override {
    goal_.use_start = false;
    if (!getInput("goal", goal_.goal) || !getInput("planner_id", goal_.planner_id) ||
        !getInput("scope_id", scope_) || scope_.size() != 32 ||
        !std::all_of(scope_.begin(), scope_.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) {
      throw std::runtime_error("missing scoped ComputePathToPose input");
    }
  }

  BT::NodeStatus on_success() override {
    if (!goal_handle_ || goal_handle_->get_goal_id() != result_.goal_id) {
      throw std::runtime_error("native child UUID mismatch");
    }
    setOutput("path", result_.result->path);
    std::ostringstream uuid;
    for (auto byte : goal_handle_->get_goal_id()) {
      uuid << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    }
    std_msgs::msg::String message;
    message.data = "{\"event\":\"navigation_child_link\",\"scope_id\":\"" + scope_ +
                   "\",\"child_uuid\":\"" + uuid.str() + "\",\"action\":\"compute_path_to_pose\"}";
    link_->publish(message);
    return BT::NodeStatus::SUCCESS;
  }

private:
  std::string scope_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr link_;
};
}  // namespace

BT_REGISTER_NODES(factory) {
  factory.registerNodeType<ScopedComputePathToPose>("ScopedComputePathToPose");
}
