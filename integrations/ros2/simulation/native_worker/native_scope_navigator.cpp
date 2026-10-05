// Close only the navigation Action; retain the installed navigator and BT
// engine.
#include "native_leaf_evidence.hpp"
#include "native_log.hpp"
#include <nav2_bt_navigator/bt_navigator.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace {
class ScopeNavigator : public nav2_bt_navigator::BtNavigator {
public:
  ScopeNavigator() : log_(native_log_path("bt-native.jsonl")) {
    if (!log_) {
      throw std::runtime_error("BT observation log unavailable");
    }
  }

  ~ScopeNavigator() override {
    close_.reset();
    if (pose_navigator_ && pose_navigator_->getActionServer()) {
      pose_navigator_->getActionServer()->on_deactivate();
    }
  }

protected:
  nav2_util::CallbackReturn on_configure(const rclcpp_lifecycle::State& state) override {
    const auto result = BtNavigator::on_configure(state);
    if (result != nav2_util::CallbackReturn::SUCCESS) {
      return result;
    }
    if (!has_parameter("failure_proof_enabled"))
      declare_parameter<bool>("failure_proof_enabled", false);
    close_ = create_service<std_srvs::srv::Trigger>(
        "close_navigation_scope",
        [this](std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          close_scope(response);
        });
    return result;
  }

private:
  void close_scope(std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
    if (closed_ || !pose_navigator_ || !pose_navigator_->getActionServer()) {
      response->success = false;
      return;
    }
    auto& server = *pose_navigator_->getActionServer();
    // Service and native Action admission use the default group on a single
    // executor. The caller requests this only after its native terminal result.
    // In this installed version deactivate returns only after future readiness;
    // its timeout logs do not shorten the wait. Never fabricate success on
    // timeout.
    server.on_deactivate();
    closed_ = true;
    const auto path = server.getCurrentBTFilename();
    std::smatch match;
    if (!std::regex_match(path, match, std::regex("/output/request-([0-9a-f]{32})\\.xml"))) {
      response->success = false;
      return;
    }
    // Humble's server haltTree only calls root->halt(), leaving the control
    // root RUNNING after cancellation. The actual BT::Tree lifecycle also
    // resets node statuses after halt. The owned tree is non-const; its getter
    // exposes only a const view. This version-pinned fixture mutates it only
    // after the native worker has joined and the endpoint is sealed (no
    // concurrent tick).
    auto& tree = const_cast<BT::Tree&>(server.getTree());
    if (tree.nodes.empty() || !tree.rootNode()) {
      response->success = false;
      return;
    }
    const auto root_before_halt = static_cast<int>(tree.rootNode()->status());
    tree.haltTree();
    bool idle = !tree.nodes.empty() && tree.rootNode()->status() == BT::NodeStatus::IDLE;
    for (const auto& node : tree.nodes) {
      idle = idle && node->status() != BT::NodeStatus::RUNNING;
    }
    std::string children = "[";
    unsigned planner = 0, controller = 0;
    bool proof_ok = true;
    const bool require_proof = get_parameter("failure_proof_enabled").as_bool();
    if (require_proof)
      for (const auto& node : tree.nodes) {
        const auto* leaf = dynamic_cast<const robot_harness_nav2::LeafEvidence*>(node.get());
        if (!leaf)
          continue;
        const auto& row = leaf->leaf_record();
        if (row.action == "compute_path_to_pose")
          ++planner;
        else if (row.action == "follow_path")
          ++controller;
        else
          proof_ok = false;
        proof_ok = proof_ok && row.scope == match[1].str() && row.kind() != "unknown";
        if (children != "[")
          children += ",";
        children += row.json();
      }
    children += "]";
    proof_ok = proof_ok && planner == 1 && controller == 1;

    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    response->success = idle && (!require_proof || proof_ok);
    response->message =
        "{\"event\":\"navigation_bt_closed\",\"scope_id\":\"" + match[1].str() +
        "\",\"bt_worker_joined\":true,\"endpoint_sealed\":true,\"tree_idle\":" +
        (idle ? "true" : "false") + ",\"steady_ns\":" + std::to_string(stamp) +
        (require_proof ? ",\"leaf_proof_complete\":" + std::string(proof_ok ? "true" : "false") +
                             ",\"children\":" + children
                       : "") +
        "}";
    log_ << "{\"event\":\"navigation_tree_halted\",\"root_before_halt\":" << root_before_halt
         << ",\"root_after_halt\":" << static_cast<int>(tree.rootNode()->status()) << "}"
         << std::endl;
    log_ << response->message << std::endl;
  }

  bool closed_ = false;
  std::ofstream log_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr close_;
};
}  // namespace

int main(int argc, char** argv) {
  const char* isolated = std::getenv("M4_ISOLATED_SIMULATION");
  if (!isolated || std::string(isolated) != "1") {
    return 2;
  }
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ScopeNavigator>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node->get_node_base_interface());
  executor.spin();
  executor.remove_node(node->get_node_base_interface());
  node.reset();
  rclcpp::shutdown();
}
