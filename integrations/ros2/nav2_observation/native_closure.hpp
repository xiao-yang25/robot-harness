#ifndef ROBOT_HARNESS_NAV2_NATIVE_CLOSURE_HPP_
#define ROBOT_HARNESS_NAV2_NATIVE_CLOSURE_HPP_

#include "closure_observations.hpp"
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <thread>
#include <vector>

namespace robot_harness::nav2_example {

// Owned by NavigationObservation; borrows its node/executor on the same thread.
class NativeClosure {
public:
  NativeClosure(rclcpp::Node::SharedPtr node, rclcpp::executors::SingleThreadedExecutor& executor,
                std::string mode, std::string scope, std::uint64_t generation,
                std::string context = "");
  void prepare();
  void open_drive();
  std::string goal_tree();
  void bind_parent(const std::string& uuid);
  bool close();
  bool closed() const {
    return facts_.closed();
  }
  std::int64_t drive_stamp_ns() const {
    return facts_.drive_stamp_ns();
  }
  const std::string& scope() const {
    return facts_.scope();
  }

private:
  template <class Predicate>
  void wait(Predicate predicate, std::chrono::seconds budget, const char* error) {
    const auto until = std::chrono::steady_clock::now() + budget;
    while (!predicate()) {
      if (!rclcpp::ok() || std::chrono::steady_clock::now() >= until) {
        throw std::runtime_error(error);
      }
      executor_.spin_some(std::chrono::milliseconds(10));
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  void record(const std::string& source, const std::string& wire);
  rclcpp::Node::SharedPtr node_;
  rclcpp::executors::SingleThreadedExecutor& executor_;
  std::string mode_, context_;
  std::uint64_t generation_;
  ClosureObservations facts_;
  std::int64_t drive_requested_ns_ = 0;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscriptions_;
};
}  // namespace robot_harness::nav2_example
#endif
