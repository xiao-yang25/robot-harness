// Research-only ComputePathToPose worker boundary, Nav2 1.1.20.
#include "action_server_startup.hpp"
#include "native_log.hpp"
#include <nav2_planner/planner_server.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;

class WorkerPlanner : public nav2_planner::PlannerServer {
public:
  explicit WorkerPlanner(bool hold) : hold_(hold), log_(native_log_path("planner-native.jsonl")) {
    if (!log_) {
      throw std::runtime_error("planner observation log unavailable");
    }
  }

  ~WorkerPlanner() override {
    timer_.reset();
    release_.reset();
    readiness_.reset();
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      released_ = true;
    }
    ready_.notify_all();
    if (action_server_pose_) {
      action_server_pose_->deactivate();
      action_server_pose_.reset();
    }
  }

protected:
  nav2_util::CallbackReturn on_configure(const rclcpp_lifecycle::State& state) override {
    const auto result = PlannerServer::on_configure(state);
    if (result != nav2_util::CallbackReturn::SUCCESS) {
      return result;
    }
    if (!await_inactive_action_server(shared_from_this(), "compute_path_to_pose",
                                      *action_server_pose_)) {
      RCLCPP_ERROR(get_logger(), "Inactive planner action executor did not respond");
      return nav2_util::CallbackReturn::FAILURE;
    }
    action_server_pose_.reset();
    action_server_pose_ = std::make_unique<ActionServerToPose>(
        shared_from_this(), "compute_path_to_pose", [this] { execute(); }, [] {}, 500ms, false);
    auto node = shared_from_this();
    ack_ = rclcpp::create_publisher<std_msgs::msg::String>(
        node, "compute_path_to_pose_worker_ack", rclcpp::QoS(1).reliable().transient_local());
    release_ = create_service<std_srvs::srv::Trigger>(
        "release_planner_worker",
        [this](std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          std::lock_guard<std::mutex> lock(state_mutex_);
          response->success = hold_ && in_tail_ && !released_ && !timed_out_;
          if (response->success) {
            record("planner_release", uuid_);
            released_ = true;
            ready_.notify_all();
          }
        });
    timer_ = create_wall_timer(10ms, [this] { observe(); });
    readiness_ = create_service<std_srvs::srv::Trigger>(
        "planner_map_ready", [this](std::shared_ptr<std_srvs::srv::Trigger::Request>,
                                    std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          geometry_msgs::msg::PoseStamped pose;
          unsigned int x = 0, y = 0;
          auto* map = costmap_ros_->getCostmap();
          std::lock_guard<nav2_costmap_2d::Costmap2D::mutex_t> lock(*map->getMutex());
          response->success = costmap_ros_->isCurrent() && costmap_ros_->getRobotPose(pose) &&
                              map->worldToMap(pose.pose.position.x, pose.pose.position.y, x, y);
          response->message = response->success ? "planner costmap current and robot in bounds"
                                                : "planner costmap not ready";
        });
    return result;
  }

private:
  void record(const std::string& event, const std::string& uuid) {
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_ << "{\"event\":\"" << event << "\",\"goal_uuid\":\"" << uuid
         << "\",\"steady_ns\":" << stamp << "}" << std::endl;
  }

  void execute() {
    std::ostringstream out;
    for (auto byte : action_server_pose_->get_current_goal_id()) {
      out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    }
    const auto uuid = out.str();
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      uuid_ = uuid;
      ++calls_;
    }
    record("worker_enter", uuid);
    computePlan();  // Real installed planner computation and result publication.
    record("callback_tail_enter", uuid);
    {
      std::unique_lock<std::mutex> lock(state_mutex_);
      in_tail_ = true;
      if (hold_ && !ready_.wait_for(lock, 60s, [this] { return released_; })) {
        timed_out_ = true;
        record("planner_barrier_timeout", uuid);
      }
    }
    record("callback_tail_return", uuid);
  }

  void observe() {
    std::string uuid;
    unsigned calls;
    bool in_tail;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      uuid = uuid_;
      calls = calls_;
      in_tail = in_tail_;
    }
    if (published_ || calls == 0 || !action_server_pose_) {
      return;
    }
    // handle_accepted and this timer share the main executor; only the worker
    // touches the barrier. No concurrent assignment of the future is possible.
    if (action_server_pose_->is_running()) {
      if (in_tail && !withheld_) {
        record("closure_withheld_worker_running", uuid);
        withheld_ = true;
      }
      return;
    }
    action_server_pose_->deactivate();
    published_ = true;
    // The worker may have timed out between the earlier snapshot and future
    // readiness. Read its final outcome only after it can no longer change.
    bool timed_out;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      timed_out = timed_out_;
    }
    if (calls != 1 || timed_out || uuid == std::string(32, '0')) {
      record("unsupported_worker_scope", uuid);
      return;
    }
    record("worker_future_ready_endpoint_sealed", uuid);
    std_msgs::msg::String message;
    message.data = "{\"event\":\"compute_path_to_pose_worker_ack\",\"goal_uuid\":\"" + uuid +
                   "\",\"worker_future_ready\":true,\"endpoint_sealed\":true}";
    ack_->publish(message);
  }

  const bool hold_;
  std::mutex state_mutex_, log_mutex_;
  std::condition_variable ready_;
  std::string uuid_;
  unsigned calls_ = 0;
  bool in_tail_ = false, released_ = false, timed_out_ = false;
  bool published_ = false, withheld_ = false;
  std::ofstream log_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr ack_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr release_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr readiness_;
  rclcpp::TimerBase::SharedPtr timer_;
};
}  // namespace

int main(int argc, char** argv) {
  const char* isolated = std::getenv("M4_ISOLATED_SIMULATION");
  const char* mode = std::getenv("M4_WORK_SCOPE_CASE");
  if (!isolated || std::string(isolated) != "1" || !mode ||
      (std::string(mode) != "normal" && std::string(mode) != "held-planner" &&
       std::string(mode) != "missing-bt" && std::string(mode) != "planner-timeout")) {
    return 2;
  }
  rclcpp::init(argc, argv);
  auto node = std::make_shared<WorkerPlanner>(std::string(mode) == "held-planner" ||
                                              std::string(mode) == "planner-timeout");
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node->get_node_base_interface());
  executor.spin();
  executor.remove_node(node->get_node_base_interface());
  node.reset();
  rclcpp::shutdown();
}
