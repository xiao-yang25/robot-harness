// One normal navigation observation in an exclusively owned, fresh simulator.
// This is not a reusable ROS Host or a physical-motion settlement profile.
#include "motion_window.hpp"
#include "robot_harness/authority_gate.hpp"

#include <geometry_msgs/msg/pose_with_covariance_stamped.hpp>
#include <lifecycle_msgs/srv/get_state.hpp>
#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <rosgraph_msgs/msg/clock.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {
namespace rh = robot_harness;
using rh::nav2_example::MotionStatus;
using rh::nav2_example::MotionWindow;
using Action = nav2_msgs::action::NavigateToPose;
using GoalHandle = rclcpp_action::ClientGoalHandle<Action>;
using SteadyClock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto kProfile = "nav2-normal-observation-only-v1";

rh::MonotonicTime now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             SteadyClock::now().time_since_epoch())
      .count();
}

std::string uuid_text(const rclcpp_action::GoalUUID& id) {
  std::ostringstream out;
  for (const auto byte : id) {
    out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  }
  return out.str();
}

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void accepted(rh::EvidenceDisposition value) {
  require(value == rh::EvidenceDisposition::kAccepted, "Core rejected evidence");
}

rh::AuthorityGateConfig config(const std::string& session) {
  rh::AuthorityGateConfig result;
  result.binding = {"isolated-nav2-" + session, "simulated-mobile-base", 1, 1};
  result.capability_id = "fixed-corridor-navigation";
  result.binding_observer = "fresh-exclusive-launcher";
  result.worker_observer = "nav2-readiness";
  result.result_sink_observer = "local-navigation-result";
  result.native_evidence_source = "correlated-nav2-action";
  result.settlement_evidence_source = "unimplemented-navigation-settlement";
  result.settlement_scope = "unimplemented-navigation-settlement";
  result.execution_capability_observer = "fixed-navigation-example";
  result.started_at = now();
  return result;
}

class NavigationObservation {
public:
  NavigationObservation(const std::string& session, std::string mode)
      : config_(config(session)), gate_(config_), mode_(std::move(mode)),
        node_(std::make_shared<rclcpp::Node>(
            "harness_navigation_observation",
            rclcpp::NodeOptions().parameter_overrides({rclcpp::Parameter("use_sim_time", true)}))) {
    clock_subscription_ = node_->create_subscription<rosgraph_msgs::msg::Clock>(
        "clock", rclcpp::SensorDataQoS(),
        [this](rosgraph_msgs::msg::Clock::ConstSharedPtr message) {
          const auto stamp = rclcpp::Time(message->clock).nanoseconds();
          if (stamp != clock_ns_) {
            clock_ns_ = stamp;
            clock_seen_ = now();
          }
        });
    odometry_subscription_ = node_->create_subscription<nav_msgs::msg::Odometry>(
        "odom", rclcpp::SensorDataQoS(), [this](nav_msgs::msg::Odometry::ConstSharedPtr message) {
          odometry_ = message->pose.pose.position;
          odometry_seen_ = now();
          odometry_stamp_ns_ = rclcpp::Time(message->header.stamp).nanoseconds();
          if (motion_window_) {
            const auto& q = message->pose.pose.orientation;
            const auto& v = message->twist.twist;
            const double norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            if (message->header.frame_id != "odom" || !std::isfinite(norm) ||
                std::abs(norm - 1.0) > 0.01) {
              std::cerr << "Motion frame/orientation rejected: " << message->header.frame_id
                        << " norm=" << norm << std::endl;
              motion_window_->reject();
              return;
            }
            const double yaw =
                std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z));
            motion_window_->observe({odometry_stamp_ns_, odometry_.x, odometry_.y, yaw,
                                     std::hypot(v.linear.x, v.linear.y), v.angular.z},
                                    clock_ns_, now() - clock_seen_);
            if (motion_window_->status() == MotionStatus::kRejected) {
              std::cerr << "Motion sample rejected: stamp=" << odometry_stamp_ns_
                        << " clock=" << clock_ns_ << " clock_age_ms=" << now() - clock_seen_
                        << " x=" << odometry_.x << " y=" << odometry_.y << " yaw=" << yaw
                        << " speed=" << std::hypot(v.linear.x, v.linear.y)
                        << " angular_speed=" << v.angular.z << std::endl;
            }
          }
        });
    pose_subscription_ = node_->create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
        "amcl_pose", rclcpp::QoS(1).transient_local(),
        [this](geometry_msgs::msg::PoseWithCovarianceStamped::ConstSharedPtr message) {
          pose_ = message->pose.pose.position;
          pose_seen_ = now();
        });
    initial_pose_ =
        node_->create_publisher<geometry_msgs::msg::PoseWithCovarianceStamped>("initialpose", 10);
    client_ = rclcpp_action::create_client<Action>(node_, "navigate_to_pose");
    executor_.add_node(node_);
  }

  ~NavigationObservation() {
    // Only this thread spins. Destroy executor references and native callbacks
    // before their captured context (this) and Core disappear. No remote stop claim.
    gate_.close_admission();
    executor_.remove_node(node_);
    client_.reset();
  }

  int run() {
    prepare_navigation();
    const auto start = now();
    // The launcher supplies a fresh, exclusive deployment premise. Discovery is
    // only readiness; it is never presented as proof that an existing server is idle.
    if (mode_ != "deny-startup") {
      accepted(gate_.observe_startup({rh::StartupEvidenceKind::kBindingIdleAndSettled,
                                      config_.binding,
                                      {config_.binding_observer, start, 1},
                                      true}));
    }
    accepted(gate_.observe_startup({rh::StartupEvidenceKind::kWorkerReady,
                                    config_.binding,
                                    {config_.worker_observer, start, 1},
                                    true}));
    accepted(gate_.observe_startup({rh::StartupEvidenceKind::kResultSinkReady,
                                    config_.binding,
                                    {config_.result_sink_observer, start, 1},
                                    true}));
    accepted(
        gate_.observe_execution_capabilities({config_.binding,
                                              config_.capability_id,
                                              kProfile,
                                              1,
                                              true,
                                              {config_.execution_capability_observer, start, 1},
                                              start + 2000}));
    const auto decision = gate_.admit(request());
    if (mode_ == "deny-startup") {
      require(decision.status == rh::AdmissionStatus::kRecoveryRequired && !decision.authority,
              "missing startup evidence did not deny admission");
      std::cout << "{\"event\":\"harness_negative_result\",\"case\":\"deny-startup\","
                   "\"passed\":true,\"native_send_count\":0}"
                << std::endl;
      return 0;
    }
    require(decision.status == rh::AdmissionStatus::kAdmitted && decision.authority.has_value(),
            "Core admission failed");
    authority_ = *decision.authority;
    std::cout << "{\"event\":\"harness_admitted\",\"operation_id\":" << authority_.operation_id
              << "}" << std::endl;
    if (mode_ == "cancel-before-dispatch") {
      require(gate_.request_cancel(authority_, now()).status == rh::ControlStatus::kApplied,
              "pre-dispatch cancellation failed");
      require(!gate_.claim_dispatch(authority_, now()), "revoked authority dispatched");
      accepted(gate_.observe_non_submission(
          {authority_, {config_.settlement_evidence_source, now(), 1}}));
      std::cout << "{\"event\":\"harness_negative_result\",\"case\":\"cancel-before-dispatch\","
                   "\"passed\":true,\"native_send_count\":0}"
                << std::endl;
      return 0;
    }

    Action::Goal goal;
    goal.pose.header.frame_id = "map";
    goal.pose.header.stamp = node_->now();
    goal.pose.pose.position.x = 0.7;
    goal.pose.pose.position.y = -0.5;
    goal.pose.pose.orientation.w = 1.0;
    rclcpp_action::Client<Action>::SendGoalOptions options;
    options.goal_response_callback = [this](GoalHandle::SharedPtr handle) {
      handle_ = handle;
      if (!handle) {
        native(rh::NativeEventKind::kRejected);
        throw std::runtime_error("Nav2 rejected goal");
      }
      native(rh::NativeEventKind::kAccepted);
      std::cout << "{\"event\":\"accepted\",\"goal_uuid\":\"" << uuid_text(handle->get_goal_id())
                << "\",\"operation_id\":" << authority_.operation_id << "}" << std::endl;
    };
    options.feedback_callback = [this](GoalHandle::SharedPtr handle,
                                       std::shared_ptr<const Action::Feedback> feedback) {
      require(handle_ && handle->get_goal_id() == handle_->get_goal_id(), "feedback UUID mismatch");
      if (feedback_count_++ == 0) {
        native(rh::NativeEventKind::kStarted);
      }
      feedback_ = *feedback;
      feedback_seen_ = now();
      const auto& point = feedback->current_pose.pose.position;
      require(std::isfinite(point.x) && std::isfinite(point.y) &&
                  std::isfinite(feedback->distance_remaining),
              "non-finite navigation feedback");
      std::cout << "{\"event\":\"feedback\",\"operation_id\":" << authority_.operation_id
                << ",\"goal_uuid\":\"" << uuid_text(handle->get_goal_id()) << "\",\"x\":" << point.x
                << ",\"y\":" << point.y
                << ",\"distance_remaining\":" << feedback->distance_remaining << "}" << std::endl;
    };
    options.result_callback = [this](const GoalHandle::WrappedResult& result) {
      require(handle_ && result.goal_id == handle_->get_goal_id(), "result UUID mismatch");
      std::cout << "{\"event\":\"native_result\",\"result_code\":" << static_cast<int>(result.code)
                << ",\"operation_id\":" << authority_.operation_id << ",\"goal_uuid\":\""
                << uuid_text(result.goal_id) << "\"}" << std::endl;
      switch (result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        native(rh::NativeEventKind::kTerminalSucceeded);
        break;
      case rclcpp_action::ResultCode::ABORTED:
        native(rh::NativeEventKind::kTerminalFailed);
        throw std::runtime_error("navigation aborted");
      case rclcpp_action::ResultCode::CANCELED:
        native(rh::NativeEventKind::kTerminalCancelled);
        throw std::runtime_error("navigation canceled");
      default:
        throw std::runtime_error("unknown navigation outcome");
      }
      require(feedback_.has_value(), "result without pose feedback");
      const auto& pose = feedback_->current_pose;
      const auto stamp = rclcpp::Time(pose.header.stamp).nanoseconds();
      const auto error = std::hypot(pose.pose.position.x - 0.7, pose.pose.position.y + 0.5);
      require(pose.header.frame_id == "map" && stamp > submitted_sim_ns_ &&
                  std::abs(clock_ns_ - stamp) < 2000000000 && now() - feedback_seen_ < 2000 &&
                  now() - clock_seen_ < 2000 && now() - odometry_seen_ < 2000 && error < 0.35,
              "terminal pose is missing, stale or outside target tolerance");
      // Stage a POD result before checking the actual sink. No callback spin or
      // re-entrant user code is allowed between permission and this local commit.
      const Result staged{result.goal_id, pose.pose.position.x, pose.pose.position.y};
      const auto delivery_time = now();
      gate_.observe_time(authority_, delivery_time);
      require(gate_.can_deliver_result(authority_), "result authority denied");
      result_slot_ = staged;
      accepted(gate_.observe_output_accepted({authority_,
                                              "local-navigation-result",
                                              {config_.result_sink_observer, delivery_time, 1}}));
      done_ = true;
    };
    submitted_sim_ns_ = clock_ns_;
    require(client_->action_server_is_ready(), "navigation server lost before dispatch");
    require(gate_.claim_dispatch(authority_, now()), "Core dispatch claim denied");
    // No callbacks run between the authority claim and the native submission.
    ++native_send_count_;
    const auto pending_goal = client_->async_send_goal(goal, options);
    (void)pending_goal;
    wait([this] { return done_; }, 100s, "navigation result timed out; settlement unresolved");
    const auto after_motion_stamp = std::max(clock_ns_, odometry_stamp_ns_);
    std::cout << "{\"event\":\"motion_observation_started\",\"operation_id\":"
              << authority_.operation_id << ",\"goal_uuid\":\"" << uuid_text(handle_->get_goal_id())
              << "\"}" << std::endl;
    motion_window_.emplace(after_motion_stamp);
    if (mode_ == "withhold-odometry") {
      // Explicit fault injection: stop receiving odometry after accepting the result.
      odometry_subscription_.reset();
    }
    const auto observation_deadline = std::min(SteadyClock::now() + 8s, deadline_);
    while (motion_window_->status() == MotionStatus::kPending && rclcpp::ok() &&
           SteadyClock::now() < observation_deadline) {
      executor_.spin_some(10ms);
      std::this_thread::sleep_for(5ms);
    }
    const bool motion_observed = motion_window_->status() == MotionStatus::kObserved;
    std::cout << "{\"event\":\"motion_observation\",\"observed\":"
              << (motion_observed ? "true" : "false")
              << ",\"operation_id\":" << authority_.operation_id << ",\"goal_uuid\":\""
              << uuid_text(handle_->get_goal_id())
              << "\",\"sample_count\":" << motion_window_->sample_count()
              << ",\"duration_ns\":" << motion_window_->duration_ns()
              << ",\"max_speed_m_s\":" << motion_window_->max_speed() << "}" << std::endl;
    const auto receipt = gate_.receipt(authority_);
    require(receipt && receipt->native_outcome == rh::NativeOutcome::kSucceeded &&
                receipt->output == rh::OutputDisposition::kAccepted && result_slot_,
            "normal receipt incomplete");
    // Intentionally no SettlementEvidence: result arrival cannot reopen this domain.
    const auto second = gate_.admit(request());
    require(second.status == rh::AdmissionStatus::kDomainOccupied && !second.authority &&
                receipt->settlement == rh::SettlementStatus::kPending && native_send_count_ == 1 &&
                receipt->authority_disposition != rh::AuthorityDisposition::kReleased,
            "unsettled navigation admitted conflicting work");
    const bool expected_motion = mode_ == "normal";
    require(motion_observed == expected_motion, "unexpected post-result motion observation");
    gate_.close_admission();
    std::cout << "{\"event\":\"harness_observation_result\",\"passed\":true,"
                 "\"native_send_count\":1,\"output_accepted\":true,\"settled\":false,"
                 "\"second_admission_blocked\":true,\"operation_id\":"
              << authority_.operation_id << ",\"goal_uuid\":\"" << uuid_text(result_slot_->id)
              << "\",\"feedback_count\":" << feedback_count_
              << ",\"motion_observed\":" << (motion_observed ? "true" : "false") << ",\"case\":\""
              << mode_ << "\"}" << std::endl;
    return 0;
  }

private:
  template <typename Predicate>
  void wait(Predicate predicate, std::chrono::seconds limit, const char* error) {
    const auto deadline = std::min(SteadyClock::now() + limit, deadline_);
    while (!predicate()) {
      require(rclcpp::ok() && SteadyClock::now() < deadline, error);
      executor_.spin_some(10ms);
      std::this_thread::sleep_for(5ms);
    }
  }

  void active(const std::string& name) {
    auto lifecycle = node_->create_client<lifecycle_msgs::srv::GetState>(name + "/get_state");
    wait([&] { return lifecycle->service_is_ready(); }, 30s, "lifecycle service unavailable");
    bool is_active = false;
    const auto until = SteadyClock::now() + 30s;
    while (!is_active && SteadyClock::now() < until) {
      auto future =
          lifecycle->async_send_request(std::make_shared<lifecycle_msgs::srv::GetState::Request>());
      wait([&] { return future.wait_for(0s) == std::future_status::ready; }, 5s,
           "lifecycle response unavailable");
      is_active = future.get()->current_state.id == 3;
    }
    require(is_active, "lifecycle is not active");
  }

  void prepare_navigation() {
    wait([this] { return clock_ns_ > 0 && odometry_seen_ > 0; }, 45s, "no clock or odometry");
    require(std::hypot(odometry_.x + 2.0, odometry_.y + 0.5) < 0.1,
            "robot not at the fresh experiment spawn");
    active("amcl");
    const auto initialized_at = now();
    for (int attempt = 0; attempt < 15; ++attempt) {
      geometry_msgs::msg::PoseWithCovarianceStamped pose;
      pose.header.frame_id = "map";
      pose.header.stamp = node_->now();
      pose.pose.pose.position.x = -2.0;
      pose.pose.pose.position.y = -0.5;
      pose.pose.pose.orientation.w = 1.0;
      pose.pose.covariance[0] = 0.25;
      pose.pose.covariance[7] = 0.25;
      pose.pose.covariance[35] = 0.0685;
      initial_pose_->publish(pose);
      const auto until = now() + 1000;
      wait([&] { return now() >= until; }, 2s, "localization observation timed out");
      if (pose_seen_ > initialized_at && std::hypot(pose_.x + 2.0, pose_.y + 0.5) < 0.35) {
        break;
      }
    }
    require(pose_seen_ > initialized_at && std::hypot(pose_.x + 2.0, pose_.y + 0.5) < 0.35,
            "no fresh localization near spawn");
    active("bt_navigator");
    wait([this] { return client_->action_server_is_ready(); }, 10s, "Nav2 action unavailable");
    require(now() - clock_seen_ < 2000 && now() - odometry_seen_ < 2000,
            "startup clock or odometry stale");
  }

  rh::OperationRequest request() const {
    return {config_.capability_id,
            config_.binding.effect_domain,
            "fixed-corridor-goal",
            "local-navigation-result",
            now(),
            {},
            rh::ExecutionRequirements{kProfile, 1}};
  }

  void native(rh::NativeEventKind kind) {
    accepted(gate_.observe_native({authority_,
                                   "session-navigation-1",
                                   kind,
                                   {config_.native_evidence_source, now(), ++native_sequence_}}));
  }

  struct Result {
    rclcpp_action::GoalUUID id;
    double x;
    double y;
  };
  rh::AuthorityGateConfig config_;
  rh::AuthorityGate gate_;
  std::string mode_;
  rh::OperationAuthority authority_;
  SteadyClock::time_point deadline_ = SteadyClock::now() + 180s;
  std::int64_t clock_ns_ = 0, submitted_sim_ns_ = 0, odometry_stamp_ns_ = 0;
  std::optional<MotionWindow> motion_window_;
  rh::MonotonicTime clock_seen_ = 0, odometry_seen_ = 0, pose_seen_ = 0, feedback_seen_ = 0;
  geometry_msgs::msg::Point odometry_, pose_;
  std::optional<Action::Feedback> feedback_;
  std::optional<Result> result_slot_;
  std::uint64_t native_sequence_ = 0, feedback_count_ = 0, native_send_count_ = 0;
  bool done_ = false;
  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<rosgraph_msgs::msg::Clock>::SharedPtr clock_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr pose_subscription_;
  rclcpp::Publisher<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr initial_pose_;
  rclcpp_action::Client<Action>::SharedPtr client_;
  GoalHandle::SharedPtr handle_;
  rclcpp::executors::SingleThreadedExecutor executor_;
};
}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc == 2 ? argv[1] : "normal";
  if (argc > 2 || (mode != "normal" && mode != "deny-startup" && mode != "cancel-before-dispatch" &&
                   mode != "withhold-odometry")) {
    std::cerr << "Expected normal, deny-startup, cancel-before-dispatch or withhold-odometry\n";
    return 2;
  }
  const char* isolated = std::getenv("M4_ISOLATED_SIMULATION");
  const char* fresh = std::getenv("M4_FRESH_NAV2_RUN");
  if (!isolated || std::string(isolated) != "1" || !fresh || std::string(fresh).empty() ||
      !std::filesystem::exists("/.dockerenv")) {
    std::cerr << "Requires an exclusively owned fresh simulator launcher\n";
    return 2;
  }
  rclcpp::init(argc, argv);
  int result = 1;
  try {
    // Fail closed on an accidental second invocation in the same output/session.
    require(std::filesystem::create_directory("/output/harness-navigation.claim"),
            "this session already claimed navigation");
    NavigationObservation observation(fresh, mode);
    result = observation.run();
  } catch (const std::exception& error) {
    std::cerr << "Navigation observation incomplete: " << error.what() << '\n';
    std::cout << "{\"event\":\"incomplete\",\"passed\":false,\"settled\":false}" << std::endl;
  }
  rclcpp::shutdown();
  return result;
}
