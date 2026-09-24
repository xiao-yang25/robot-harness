#ifndef ROBOT_HARNESS_SIMULATION_ACTION_SERVER_STARTUP_HPP_
#define ROBOT_HARNESS_SIMULATION_ACTION_SERVER_STARTUP_HPP_

#include <action_msgs/msg/goal_status.hpp>
#include <nav2_util/simple_action_server.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

// Nav2 1.1.20 starts its default action executor asynchronously. Immediately
// destroying it can cancel before spin() begins, then wait forever in join().
// Before replacing a fresh, inactive server, require an actual response from
// that executor. Discovery alone is insufficient. No goal is submitted.
template <typename Action, typename Node>
bool await_inactive_action_server(const std::shared_ptr<Node>& node, const std::string& action_name,
                                  nav2_util::SimpleActionServer<Action>& server,
                                  std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  if (server.is_server_active()) {
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const auto remaining = [&] {
    const auto duration = deadline - std::chrono::steady_clock::now();
    return std::max(duration, std::chrono::steady_clock::duration::zero());
  };
  // This runs inside the lifecycle callback. A distinct node/executor avoids
  // waiting on that callback's own mutually exclusive group. Ignore process
  // name remappings, but preserve the task namespace and ROS context.
  rclcpp::NodeOptions options;
  options.context(node->get_node_base_interface()->get_context());
  options.use_global_arguments(false);
  auto probe = std::make_shared<rclcpp::Node>(std::string(node->get_name()) + "_startup_probe",
                                              node->get_namespace(), options);
  using ResultService = typename Action::Impl::GetResultService;
  auto client = probe->template create_client<ResultService>(action_name + "/_action/get_result");
  if (!client->wait_for_service(remaining())) {
    return false;
  }
  // The fresh inactive server has accepted no UUID, including the zero UUID.
  auto response = client->async_send_request(std::make_shared<typename ResultService::Request>());
  if (rclcpp::spin_until_future_complete(probe, response, remaining()) !=
      rclcpp::FutureReturnCode::SUCCESS) {
    return false;
  }
  return response.get()->status == action_msgs::msg::GoalStatus::STATUS_UNKNOWN &&
         !server.is_server_active();
}

#endif
