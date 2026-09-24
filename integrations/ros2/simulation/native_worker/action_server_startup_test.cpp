#include "action_server_startup.hpp"

#include <nav2_msgs/action/compute_path_to_pose.hpp>
#include <nav2_msgs/action/follow_path.hpp>

#include <atomic>
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Action> void check_response(const std::string& name) {
  auto node = std::make_shared<rclcpp::Node>(name, "/startup_test/task_b");
  std::atomic<unsigned> executions{0};
  auto server = std::make_unique<nav2_util::SimpleActionServer<Action>>(
      node, name, [&] { ++executions; }, [] {}, 500ms, true);
  require(await_inactive_action_server(node, name, *server), "inactive server did not respond");
  require(executions == 0 && !server->is_server_active(), "startup probe executed a goal");
  server->activate();
  require(!await_inactive_action_server(node, name, *server),
          "active server accepted for replacement");
  server->deactivate();
  server.reset();
}

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    // Establish the pinned executor behavior behind the original race:
    // cancel() before spin() does not prevent the subsequent spin.
    auto node = std::make_shared<rclcpp::Node>("pre_spin_cancel");
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    bool callback_ran = false;
    auto timer = node->create_wall_timer(1ms, [&] {
      callback_ran = true;
      executor.cancel();
    });
    executor.cancel();
    executor.spin();
    require(callback_ran, "pinned pre-spin cancellation behavior changed");
    executor.remove_node(node);
    timer.reset();

    // A discoverable endpoint whose executor has not started cannot satisfy
    // the handshake. This would fail if the helper only checked discovery.
    using Action = nav2_msgs::action::FollowPath;
    using Result = Action::Impl::GetResultService;
    auto dormant = std::make_unique<nav2_util::SimpleActionServer<Action>>(
        node, "dormant_path", [] {}, [] {}, 500ms, false);
    auto witness = std::make_shared<rclcpp::Node>("discovery_witness");
    auto client = witness->create_client<Result>("dormant_path/_action/get_result");
    require(client->wait_for_service(3s), "test endpoint was not discoverable");
    require(!await_inactive_action_server(node, "dormant_path", *dormant, 200ms),
            "discovery alone was treated as executor progress");
    dormant.reset();

    check_response<nav2_msgs::action::FollowPath>("follow_path");
    check_response<nav2_msgs::action::ComputePathToPose>("compute_path_to_pose");
    std::cout << "Inactive action executor startup checks passed" << std::endl;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
}
