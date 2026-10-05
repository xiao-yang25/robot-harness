#ifndef ROBOT_HARNESS_NAV2_SCOPED_ACTION_HPP_
#define ROBOT_HARNESS_NAV2_SCOPED_ACTION_HPP_
#include "native_leaf_evidence.hpp"
#include <iomanip>
#include <nav2_behavior_tree/bt_action_node.hpp>
#include <sstream>
#include <std_msgs/msg/string.hpp>

namespace robot_harness_nav2 {
template <class Action>
class TrackedAction : public nav2_behavior_tree::BtActionNode<Action>, public LeafEvidence {
  using Base = nav2_behavior_tree::BtActionNode<Action>;

public:
  TrackedAction(const std::string& name, const std::string& action,
                const BT::NodeConfiguration& config, const std::string& topic)
      : Base(name, action, config) {
    evidence_.action = action;
    if (!this->getInput("scope_id", evidence_.scope) || evidence_.scope.size() != 32 ||
        evidence_.scope == std::string(32, '0') ||
        evidence_.scope.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("invalid immutable failure scope");
    link_ = this->node_->template create_publisher<std_msgs::msg::String>(
        topic, rclcpp::QoS(1).reliable().transient_local());
  }
  BT::NodeStatus tick() override {
    evidence_.enter();  // Before Base can issue any RPC; halt never clears this.
    return Base::tick();
  }
  const LeafRecord& leaf_record() const override {
    return evidence_;
  }
  void on_wait_for_result(std::shared_ptr<const typename Action::Feedback>) override {
    publish_link();
  }
  BT::NodeStatus on_success() override {
    terminal(rclcpp_action::ResultCode::SUCCEEDED, "succeeded");
    return BT::NodeStatus::SUCCESS;
  }
  BT::NodeStatus on_aborted() override {
    terminal(rclcpp_action::ResultCode::ABORTED, "failed");
    return BT::NodeStatus::FAILURE;
  }
  BT::NodeStatus on_cancelled() override {
    // Base::halt can call this without a confirmed cancellation result.
    if (this->goal_handle_ && this->goal_result_available_ &&
        this->result_.code == rclcpp_action::ResultCode::CANCELED)
      terminal(rclcpp_action::ResultCode::CANCELED, "cancelled");
    else
      evidence_.ambiguous = true;
    publish_link();
    return BT::NodeStatus::SUCCESS;
  }

private:
  std::string uuid() const {
    if (!this->goal_handle_)
      throw std::runtime_error("child handle unavailable");
    std::ostringstream out;
    for (auto byte : this->goal_handle_->get_goal_id())
      out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    return out.str();
  }
  void terminal(rclcpp_action::ResultCode code, const std::string& outcome) {
    if (!this->goal_handle_ || !this->goal_result_available_ || this->result_.code != code ||
        this->goal_handle_->get_goal_id() != this->result_.goal_id)
      throw std::runtime_error("unconfirmed child terminal");
    evidence_.associate(uuid(), outcome);
    publish_link();
  }
  void publish_link() {
    const auto id = uuid();
    if (!published_.empty()) {
      if (id != published_)
        throw std::runtime_error("one-shot child identity changed");
      return;
    }
    std_msgs::msg::String message;
    message.data = "{\"event\":\"navigation_child_link\",\"scope_id\":\"" + evidence_.scope +
                   "\",\"child_uuid\":\"" + id + "\",\"action\":\"" + evidence_.action + "\"}";
    link_->publish(message);
    published_ = id;
  }
  LeafRecord evidence_;
  std::string published_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr link_;
};
}  // namespace robot_harness_nav2

#endif
