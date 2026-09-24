// Research-only request-context propagation at the actual native BT leaf.
#include <behaviortree_cpp_v3/bt_factory.h>
#include <nav2_behavior_tree/bt_action_node.hpp>
#include <nav2_msgs/action/follow_path.hpp>
#include <std_msgs/msg/string.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
class ScopedFollowPath
    : public nav2_behavior_tree::BtActionNode<nav2_msgs::action::FollowPath> {
public:
  ScopedFollowPath(const std::string &name, const BT::NodeConfiguration &config)
      : BtActionNode(name, "follow_path", config) {
    link_ = node_->create_publisher<std_msgs::msg::String>(
        "navigation_child_link", rclcpp::QoS(1).reliable().transient_local());
  }

  static BT::PortsList providedPorts() {
    return providedBasicPorts({BT::InputPort<nav_msgs::msg::Path>("path"),
                               BT::InputPort<std::string>("controller_id"),
                               BT::InputPort<std::string>("scope_id")});
  }

  void on_tick() override {
    if (!getInput("path", goal_.path) ||
        !getInput("controller_id", goal_.controller_id) ||
        !getInput("scope_id", scope_) || scope_.size() != 32 ||
        !std::all_of(scope_.begin(), scope_.end(), [](char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
      throw std::runtime_error("missing scoped FollowPath input");
    }
  }

  void on_wait_for_result(
      std::shared_ptr<const nav2_msgs::action::FollowPath::Feedback>) override {
    // Humble has resolved the accepted handle before this hook. Identity must
    // be available while moving, not only after SUCCESS (halt/cancel skips it).
    publish_link();
  }

  BT::NodeStatus on_success() override {
    if (!goal_handle_ || goal_handle_->get_goal_id() != result_.goal_id) {
      throw std::runtime_error("native child UUID mismatch");
    }
    publish_link();
    return BT::NodeStatus::SUCCESS;
  }

  BT::NodeStatus on_cancelled() override {
    publish_link();
    return BtActionNode::on_cancelled();
  }

private:
  void publish_link() {
    if (!goal_handle_) {
      throw std::runtime_error("native child handle unavailable");
    }
    std::ostringstream uuid;
    for (auto byte : goal_handle_->get_goal_id()) {
      uuid << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(byte);
    }
    if (!published_uuid_.empty()) {
      if (published_uuid_ != uuid.str())
        throw std::runtime_error("one-shot child identity changed");
      return;
    }
    std_msgs::msg::String message;
    message.data = "{\"event\":\"navigation_child_link\",\"scope_id\":\"" +
                   scope_ + "\",\"child_uuid\":\"" + uuid.str() +
                   "\",\"action\":\"follow_path\"}";
    link_->publish(message);
    published_uuid_ = uuid.str();
  }

  std::string scope_, published_uuid_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr link_;
};
} // namespace

BT_REGISTER_NODES(factory) {
  factory.registerNodeType<ScopedFollowPath>("ScopedFollowPath");
}
