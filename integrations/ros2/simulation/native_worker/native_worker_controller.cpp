// Research-only one-shot native FollowPath completion observation, Nav2 1.1.20.
#include "native_log.hpp"
#include <nav2_controller/controller_server.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;

std::string uuid_text(const rclcpp_action::GoalUUID& uuid) {
  std::ostringstream out;
  for (auto byte : uuid) {
    out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  }
  return out.str();
}

class WorkerController : public nav2_controller::ControllerServer {
public:
  explicit WorkerController(int delay_ms) : delay_ms_(delay_ms) {
    log_.open(native_log_path("worker-native.jsonl"));
    if (!log_) {
      throw std::runtime_error("native observation log unavailable");
    }
  }

  ~WorkerController() override {
    // Captured derived state must outlive the real native execution callback.
    timer_.reset();
    readiness_.reset();
    if (action_server_) {
      action_server_->deactivate();
      action_server_.reset();
    }
  }

protected:
  nav2_util::CallbackReturn on_configure(const rclcpp_lifecycle::State& state) override {
    const auto result = ControllerServer::on_configure(state);
    if (result != nav2_util::CallbackReturn::SUCCESS) {
      return result;
    }
    // Replace before activation. Use the ordinary base type, not deletion through
    // a non-virtual SimpleActionServer destructor. All its ROS callbacks now share
    // this node's mutually-exclusive group and the single-threaded main executor.
    action_server_.reset();
    action_server_ = std::make_unique<ActionServer>(
        shared_from_this(), "follow_path", [this] { execute(); }, [] {}, 500ms, false);
    auto node = shared_from_this();
    ack_ = rclcpp::create_publisher<std_msgs::msg::String>(
        node, "follow_path_worker_ack", rclcpp::QoS(1).reliable().transient_local());
    timer_ = create_wall_timer(10ms, [this] { observe_worker(); });
    // Read the same TF buffer used by the controller plugins. Lifecycle active
    // and another node's AMCL/TF reception do not establish local readiness.
    readiness_ = create_service<std_srvs::srv::Trigger>(
        "controller_transform_ready", [this](const std_srvs::srv::Trigger::Request::SharedPtr,
                                             std_srvs::srv::Trigger::Response::SharedPtr response) {
          geometry_msgs::msg::PoseStamped pose;
          response->success = getRobotPose(pose) &&
                              costmap_ros_->getTfBuffer()->canTransform(
                                  "map", pose.header.frame_id, rclcpp::Time(pose.header.stamp),
                                  rclcpp::Duration::from_seconds(0));
          response->message = response->success ? "controller map transform available"
                                                : "controller map transform unavailable";
        });
    return result;
  }

private:
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr readiness_;
  void record(const std::string& event, const std::string& uuid) {
    const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_ << "{\"event\":\"" << event << "\",\"goal_uuid\":\"" << uuid
         << "\",\"steady_ns\":" << stamp << ",\"tail_delay_ms\":" << delay_ms_ << "}" << std::endl;
  }

  void execute() {
    const auto uuid = uuid_text(action_server_->get_current_goal_id());
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      uuid_ = uuid;
      ++calls_;
    }
    record("worker_enter", uuid);
    // The installed ControllerServer still computes and publishes actual motion.
    computeControl();
    record("callback_tail_enter", uuid);
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      in_tail_ = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    record("callback_tail_return", uuid);
    // Returning here is not enough: SimpleActionServer::work still has its tail.
  }

  void observe_worker() {
    std::string uuid;
    bool in_tail;
    unsigned calls;
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      uuid = uuid_;
      in_tail = in_tail_;
      calls = calls_;
    }
    if (published_ || calls == 0 || !action_server_) {
      return;
    }
    // execution_future_ is assigned by handle_accepted only. That callback and
    // this timer run on the same executor/group; the worker never assigns it.
    if (action_server_->is_running()) {
      if (in_tail && !withheld_) {
        record("closure_withheld_worker_running", uuid);
        withheld_ = true;
      }
      return;
    }
    // Seal this activation before publishing its observation. Lifecycle reactivation
    // is outside this one-shot experiment; the base class could reactivate it.
    // This does not implement reusable admission, recovery, or parent settlement.
    action_server_->deactivate();
    published_ = true;
    if (calls != 1 || uuid == std::string(32, '0')) {
      record("unsupported_worker_scope", uuid);
      return;
    }
    record("worker_future_ready_endpoint_sealed", uuid);
    std_msgs::msg::String message;
    message.data = "{\"event\":\"follow_path_worker_ack\",\"goal_uuid\":\"" + uuid +
                   "\",\"worker_future_ready\":true,\"endpoint_sealed\":true}";
    ack_->publish(message);
  }

  const int delay_ms_;
  std::mutex state_mutex_, log_mutex_;
  std::string uuid_;
  unsigned calls_ = 0;
  bool in_tail_ = false, published_ = false, withheld_ = false;
  std::ofstream log_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr ack_;
  rclcpp::TimerBase::SharedPtr timer_;
};
}  // namespace

int main(int argc, char** argv) {
  const char* isolated = std::getenv("M4_ISOLATED_SIMULATION");
  const char* delay = std::getenv("M4_NATIVE_TAIL_MS");
  if (!isolated || std::string(isolated) != "1" || !delay ||
      (std::string(delay) != "0" && std::string(delay) != "2500")) {
    return 2;
  }
  rclcpp::init(argc, argv);
  auto node = std::make_shared<WorkerController>(std::stoi(delay));
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node->get_node_base_interface());
  executor.spin();
  executor.remove_node(node->get_node_base_interface());
  node.reset();
  rclcpp::shutdown();
}
