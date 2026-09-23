// One normal navigation observation in an exclusively owned, fresh simulator.
// This is not a reusable ROS Host or a physical-motion settlement profile.
#include "motion_window.hpp"
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
#include "cancel_response.hpp"
#include "native_closure.hpp"
#include "runtime_observation_watch.hpp"
#include <fstream>
#include <std_srvs/srv/trigger.hpp>
#endif
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
#include <exception>
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
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
constexpr auto kProfile = "nav2-fixed-context-sequence-v1";
#else
constexpr auto kProfile = "nav2-normal-observation-only-v1";
#endif

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
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
  result.settlement_evidence_source = "owner-native-closure";
  result.settlement_scope = "fresh-context-generation-drive-v1";
#endif
  result.execution_capability_observer = "fixed-navigation-example";
  result.started_at = now();
  return result;
}

rclcpp::NodeOptions observation_options(const std::string& mode) {
  auto options =
      rclcpp::NodeOptions().parameter_overrides({rclcpp::Parameter("use_sim_time", true)});
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
  if (mode.rfind("runtime-", 0) == 0)
    options.arguments({"--ros-args", "-r", "odom:=/owner_odom", "-r", "clock:=/owner_clock"});
#else
  (void)mode;
#endif
  return options;
}

class NavigationObservation {
public:
  NavigationObservation(const std::string& session, std::string mode)
      : config_(config(session)), gate_(config_), mode_(std::move(mode)),
        node_(std::make_shared<rclcpp::Node>("harness_navigation_observation",
                                             observation_options(mode_))) {
    clock_subscription_ = node_->create_subscription<rosgraph_msgs::msg::Clock>(
        "clock", rclcpp::SensorDataQoS(),
        [this](rosgraph_msgs::msg::Clock::ConstSharedPtr message) {
          const auto stamp = rclcpp::Time(message->clock).nanoseconds();
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
          runtime_watch_.observe_clock(stamp, now());
#endif
          if (stamp != clock_ns_) {
            clock_ns_ = stamp;
            clock_seen_ = now();
          }
        });
    odometry_subscription_ = node_->create_subscription<nav_msgs::msg::Odometry>(
        "odom", rclcpp::SensorDataQoS(), [this](nav_msgs::msg::Odometry::ConstSharedPtr message) {
          odometry_ = message->pose.pose.position;
          odometry_speed_ =
              std::hypot(message->twist.twist.linear.x, message->twist.twist.linear.y);
          odometry_frame_valid_ = message->header.frame_id == "odom";
          odometry_seen_ = now();
          odometry_stamp_ns_ = rclcpp::Time(message->header.stamp).nanoseconds();
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
          const auto& orientation = message->pose.pose.orientation;
          const double norm = orientation.x * orientation.x + orientation.y * orientation.y +
                              orientation.z * orientation.z + orientation.w * orientation.w;
          runtime_watch_.observe_odometry(
              odometry_stamp_ns_, now(),
              odometry_frame_valid_ && std::isfinite(odometry_.x) && std::isfinite(odometry_.y) &&
                  std::isfinite(odometry_.z) && std::isfinite(odometry_speed_) &&
                  std::isfinite(message->twist.twist.angular.z) && std::isfinite(norm) &&
                  std::abs(norm - 1.0) <= .01 &&
                  std::abs(clock_ns_ - odometry_stamp_ns_) <= 250000000);
#endif
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
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    std::ifstream input("/output/producer-context.json");
    const auto contexts = nlohmann::json::parse(input);
    a_scope_ = contexts.at("a_scope").get<std::string>();
    b_scope_ = contexts.at("b_scope").get<std::string>();
    require(a_scope_ != b_scope_, "task scopes must differ");
    closure_ = std::make_unique<rh::nav2_example::NativeClosure>(node_, executor_, closure_mode(),
                                                                 a_scope_, 1);
#endif
  }

  ~NavigationObservation() {
    // Only this thread spins. Destroy executor references and native callbacks
    // before their captured context (this) and Core disappear. No remote stop claim.
    gate_.close_admission();
    executor_.remove_node(node_);
    client_.reset();
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    closure_.reset();
#endif
  }

  int run() {
    prepare_navigation();
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    closure_->prepare();
#endif
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

#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    closure_->open_drive();
#endif
    run_goal(0.7);
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    if (runtime_loss_seen_)
      return finish_runtime_loss();
    require(!is_runtime_loss_case(), "runtime fault was not observed");
    if (is_cancellation_case()) {
      observe_cancelled_terminal();
      if (!is_replacement_case())
        return finish_cancellation();
    }
    return handoff();
#else
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
    if (callback_error_)
      std::rethrow_exception(callback_error_);
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
#endif
  }

private:
  template <typename Callback> void capture_callback(Callback callback) noexcept {
    if (callback_error_)
      return;
    try {
      callback();
    } catch (...) {
      // ROS may have fulfilled its promise before invoking our callback. Never
      // throw through that promise machinery; report from the owning run loop.
      callback_error_ = std::current_exception();
    }
  }
  void run_goal(double target_x) {
    motion_window_.reset();
    feedback_.reset();
    result_slot_.reset();
    handle_.reset();
    feedback_count_ = 0;
    done_ = false;
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    stop_requested_ = false;
    native_stop_sent_ = false;
    cancel_response_seen_ = false;
#endif
    callback_error_ = nullptr;
    const auto operation = authority_.operation_id;
    Action::Goal goal;
    goal.pose.header.frame_id = "map";
    goal.pose.header.stamp = node_->now();
    goal.pose.pose.position.x = target_x;
    goal.pose.pose.position.y = -0.5;
    goal.pose.pose.orientation.w = 1.0;
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    goal.behavior_tree = closure_->goal_tree();
#endif
    rclcpp_action::Client<Action>::SendGoalOptions options;
    options.goal_response_callback = [this, operation](GoalHandle::SharedPtr handle) {
      if (authority_.operation_id != operation)
        return;
      capture_callback([&] {
        handle_ = handle;
        if (!handle) {
          native(rh::NativeEventKind::kRejected);
          throw std::runtime_error("Nav2 rejected goal");
        }
        native(rh::NativeEventKind::kAccepted);
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
        closure_->bind_parent(uuid_text(handle->get_goal_id()));
        if (stop_requested_)
          send_native_stop();
#endif
        std::cout << "{\"event\":\"accepted\",\"goal_uuid\":\"" << uuid_text(handle->get_goal_id())
                  << "\",\"operation_id\":" << authority_.operation_id << "}" << std::endl;
      });
    };
    options.feedback_callback = [this,
                                 operation](GoalHandle::SharedPtr handle,
                                            std::shared_ptr<const Action::Feedback> feedback) {
      if (authority_.operation_id != operation)
        return;
      capture_callback([&] {
        if (mode_ == "withhold-feedback")
          return;
        require(handle_ && handle->get_goal_id() == handle_->get_goal_id(),
                "feedback UUID mismatch");
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
                  << ",\"goal_uuid\":\"" << uuid_text(handle->get_goal_id())
                  << "\",\"x\":" << point.x << ",\"y\":" << point.y
                  << ",\"distance_remaining\":" << feedback->distance_remaining << "}" << std::endl;
      });
    };
    options.result_callback = [this, operation, target_x](const GoalHandle::WrappedResult& result) {
      if (authority_.operation_id != operation)
        return;
      capture_callback([&] {
        require(handle_ && result.goal_id == handle_->get_goal_id(), "result UUID mismatch");
        std::cout << "{\"event\":\"native_result\",\"result_code\":"
                  << static_cast<int>(result.code) << ",\"steady_ms\":" << now()
                  << ",\"operation_id\":" << authority_.operation_id << ",\"goal_uuid\":\""
                  << uuid_text(result.goal_id) << "\"}" << std::endl;
        switch (result.code) {
        case rclcpp_action::ResultCode::SUCCEEDED:
          native(rh::NativeEventKind::kTerminalSucceeded);
          break;
        case rclcpp_action::ResultCode::ABORTED:
          native(rh::NativeEventKind::kTerminalFailed);
          break;
        case rclcpp_action::ResultCode::CANCELED:
          native(rh::NativeEventKind::kTerminalCancelled);
          break;
        default:
          throw std::runtime_error("unknown navigation outcome");
        }
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
        check_runtime_observations();
        if (stop_requested_ || runtime_loss_seen_) {
          require(!gate_.can_deliver_result(authority_) && !result_slot_,
                  "cancelled operation retained result authority");
          const bool produced = result.code == rclcpp_action::ResultCode::SUCCEEDED;
          accepted(gate_.observe_output_not_delivered(
              {authority_,
               produced ? rh::OutputNonDeliveryReason::kAuthorityRevoked
                        : rh::OutputNonDeliveryReason::kNoOutputProduced,
               produced ? "local-navigation-result" : "",
               {produced ? config_.result_sink_observer : config_.settlement_evidence_source, now(),
                produced ? ++output_sequence_ : ++settlement_sequence_}}));
          done_ = true;
          return;
        }
#endif
        require(result.code == rclcpp_action::ResultCode::SUCCEEDED, "navigation did not succeed");
        require(feedback_.has_value(), "result without pose feedback");
        const auto& pose = feedback_->current_pose;
        const auto stamp = rclcpp::Time(pose.header.stamp).nanoseconds();
        const auto error = std::hypot(pose.pose.position.x - target_x, pose.pose.position.y + 0.5);
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
        accepted(gate_.observe_output_accepted(
            {authority_,
             "local-navigation-result",
             {config_.result_sink_observer, delivery_time, ++output_sequence_}}));
        done_ = true;
      });
    };
    submitted_sim_ns_ = clock_ns_;
    require(client_->action_server_is_ready(), "navigation server lost before dispatch");
    require(gate_.claim_dispatch(authority_, now()), "Core dispatch claim denied");
    // No callbacks run between the authority claim and the native submission.
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    runtime_watch_.start(now());
#endif
    ++native_send_count_;
    const auto pending_goal = client_->async_send_goal(goal, options);
    (void)pending_goal;
    wait(
        [this] {
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
          if (!done_ && !callback_error_)
            check_runtime_observations();
          if (!runtime_loss_seen_)
            maybe_cancel_moving();
          if (runtime_loss_seen_ && now() - runtime_loss_at_ >= 8000)
            return true;
#endif
          return done_ || callback_error_;
        },
        100s, "navigation result timed out; settlement unresolved");
    if (callback_error_)
      std::rethrow_exception(callback_error_);
  }

#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
  bool is_replacement_case() const {
    return mode_ == "replace-moving" || mode_.rfind("replace-moving-", 0) == 0;
  }

  bool is_cancellation_case() const {
    return is_replacement_case() || mode_ == "cancel-moving" ||
           mode_ == "cancel-moving-withhold-planner-ack" ||
           mode_ == "cancel-moving-withhold-drive-ack" ||
           mode_ == "cancel-moving-withhold-odometry";
  }

  std::string closure_mode() const {
    if (!is_cancellation_case())
      return mode_;
    const auto prefix =
        is_replacement_case() ? std::string("replace-moving") : std::string("cancel-moving");
    return mode_.size() == prefix.size() ? "normal" : mode_.substr(prefix.size() + 1);
  }

  void maybe_cancel_moving() {
    if (!is_cancellation_case() || native_send_count_ != 1 || stop_requested_ || done_ ||
        callback_error_ || !handle_ || !feedback_ || feedback_count_ == 0) {
      return;
    }
    const auto displacement = std::hypot(odometry_.x + 2.0, odometry_.y + 0.5);
    if (!odometry_frame_valid_ || !std::isfinite(displacement) ||
        odometry_stamp_ns_ <= submitted_sim_ns_ ||
        std::abs(clock_ns_ - odometry_stamp_ns_) > 250000000 || now() - odometry_seen_ >= 500 ||
        now() - feedback_seen_ >= 500 || now() - clock_seen_ >= 500 ||
        !std::isfinite(odometry_speed_) || odometry_speed_ < 0.05 || displacement < 0.5) {
      return;
    }
    const auto decision = gate_.request_cancel(authority_, now());
    require(decision.status == rh::ControlStatus::kApplied && decision.should_request_native_stop,
            "moving cancellation did not issue native stop");
    stop_requested_ = true;
    require(!gate_.request_cancel(authority_, now()).should_request_native_stop &&
                !gate_.can_deliver_result(authority_),
            "cancel was not idempotent/revoked");
    require(gate_.admit(request()).status == rh::AdmissionStatus::kDomainOccupied,
            "cancel request released the domain");
    std::cout << nlohmann::json{{"event", "owner_cancel_requested"},
                                {"operation_id", authority_.operation_id},
                                {"goal_uuid", uuid_text(handle_->get_goal_id())},
                                {"steady_ms", now()},
                                {"sim_ns", clock_ns_},
                                {"x", odometry_.x},
                                {"y", odometry_.y},
                                {"speed_m_s", odometry_speed_},
                                {"displacement_m", displacement}}
                     .dump()
              << std::endl;
    send_native_stop();
  }

  void send_native_stop() {
    if (!handle_ || native_stop_sent_)
      return;
    native_stop_sent_ = true;
    const auto operation = authority_.operation_id;
    const auto uuid = handle_->get_goal_id();
    ++native_cancel_count_;
    // Exact accepted goal only; never cancel every goal on the shared ROS service.
    client_->async_cancel_goal(handle_, [this, operation, uuid](auto response) {
      if (authority_.operation_id != operation)
        return;
      capture_callback([&] {
        require(response && !cancel_response_seen_, "missing/duplicate cancel response");
        const auto acknowledgement = rh::nav2_example::cancel_acknowledgement(*response, uuid);
        accepted(gate_.observe_stop_acknowledgement(
            {authority_, acknowledgement, {config_.stop_evidence_source, now(), 1}}));
        cancel_response_seen_ = true;
        const auto receipt = gate_.receipt(authority_);
        require(receipt && receipt->settlement == rh::SettlementStatus::kPending &&
                    receipt->authority_disposition != rh::AuthorityDisposition::kReleased,
                "cancel response released authority");
        std::cout << nlohmann::json{{"event", "owner_cancel_response"},
                                    {"operation_id", operation},
                                    {"goal_uuid", uuid_text(uuid)},
                                    {"steady_ms", now()},
                                    {"return_code", response->return_code},
                                    {"acknowledged",
                                     acknowledgement == rh::StopAcknowledgement::kAcknowledged},
                                    {"settled", false}}
                         .dump()
                  << std::endl;
      });
    });
  }

  void observe_cancelled_terminal() {
    require(stop_requested_, "navigation ended before moving cancellation trigger");
    wait([this] { return cancel_response_seen_ || callback_error_; }, 5s,
         "cancel response unavailable; settlement unresolved");
    if (callback_error_)
      std::rethrow_exception(callback_error_);
    const auto terminal = gate_.receipt(authority_);
    require(terminal && terminal->stop_acknowledgement == rh::StopAcknowledgement::kAcknowledged &&
                terminal->native_outcome == rh::NativeOutcome::kCancelled &&
                terminal->output == rh::OutputDisposition::kNotDelivered && !result_slot_,
            "moving cancel did not produce expected distinct response/result");
    require(gate_.admit(request()).status == rh::AdmissionStatus::kDomainOccupied,
            "cancel terminal released the domain");
  }

  int finish_cancellation() {
    const bool native_closed = closure_->close();
    if (closure_mode() == "withhold-odometry")
      odometry_subscription_.reset();
    const bool motion_observed = native_closed && quiet();
    if (callback_error_)
      std::rethrow_exception(callback_error_);
    const auto receipt = gate_.receipt(authority_);
    const auto second = gate_.admit(request());
    require(receipt && receipt->settlement == rh::SettlementStatus::kPending &&
                receipt->authority_disposition == rh::AuthorityDisposition::kRevoked &&
                second.status == rh::AdmissionStatus::kDomainOccupied && !second.authority &&
                native_send_count_ == 1 && native_cancel_count_ == 1,
            "cancelled task admitted conflicting work");
    const bool expect_closure =
        closure_mode() != "withhold-planner-ack" && closure_mode() != "withhold-drive-ack";
    require(native_closed == expect_closure && motion_observed == (mode_ == "cancel-moving"),
            "unexpected cancellation closure/motion observation");
    gate_.close_admission();
    // No prepared replacement context in this slice: quiet alone grants no settlement.
    std::cout << nlohmann::json{{"event", "owner_cancellation_result"},
                                {"passed", true},
                                {"case", mode_},
                                {"steady_ms", now()},
                                {"native_closed", native_closed},
                                {"motion_observed", motion_observed},
                                {"native_cancel_count", native_cancel_count_},
                                {"native_send_count", native_send_count_},
                                {"output_delivered", false},
                                {"settled", false},
                                {"second_admission_blocked", true}}
                     .dump()
              << std::endl;
    return 0;
  }

  bool is_runtime_loss_case() const {
    return mode_.rfind("runtime-", 0) == 0;
  }

  void check_runtime_observations() {
    if (runtime_loss_seen_ || callback_error_)
      return;
    const auto at = now();
    const auto stale = runtime_watch_.stale(at);
    if (!stale)
      return;
    const auto withdrawn = gate_.withdraw_binding(config_.binding, at);
    require(withdrawn.status == rh::BindingControlStatus::kApplied,
            "runtime loss failed to withdraw binding");
    runtime_loss_seen_ = true;
    runtime_loss_at_ = at;
    require(!gate_.can_deliver_result(authority_) &&
                gate_.admit(request()).status == rh::AdmissionStatus::kRecoveryRequired &&
                !gate_.withdraw_binding(config_.binding, now()).native_stop_authority,
            "runtime withdrawal retained authority or repeated stop");
    std::cout << nlohmann::json{{"event", "owner_runtime_observation_lost"},
                                {"operation_id", authority_.operation_id},
                                {"steady_ms", at},
                                {"observation", *stale == rh::nav2_example::StaleObservation::kClock
                                                    ? "clock"
                                                    : "odometry"},
                                {"budget_ms", rh::nav2_example::RuntimeObservationWatch::kBudgetMs},
                                {"binding_withdrawn", true},
                                {"odometry_message_age_ms", at - odometry_seen_},
                                {"output_authority", false}}
                     .dump()
              << std::endl;
    closure_->request_drive_close();
    if (withdrawn.native_stop_authority) {
      require(withdrawn.native_stop_authority->operation_id == authority_.operation_id,
              "runtime stop authority mismatch");
      stop_requested_ = true;
      send_native_stop();
    }
  }

  int finish_runtime_loss() {
    // Keep processing observations within a bound, even if the result/ACK is
    // missing. A timeout never manufactures a terminal result or closure.
    const auto until = SteadyClock::now() + 5s;
    while (native_stop_sent_ && !cancel_response_seen_ && !callback_error_ && rclcpp::ok() &&
           SteadyClock::now() < until) {
      executor_.spin_some(10ms);
      std::this_thread::sleep_for(5ms);
    }
    const bool native_closed = closure_->close();
    const bool drive_isolated = closure_->drive_isolated();
    const bool motion_observed = drive_isolated && quiet();
    if (callback_error_)
      std::rethrow_exception(callback_error_);
    const auto receipt = gate_.receipt(authority_);
    require(receipt && receipt->binding_withdrawn_at &&
                gate_.binding_status(now()).phase == rh::BindingPhase::kWithdrawn &&
                receipt->settlement == rh::SettlementStatus::kPending &&
                receipt->authority_disposition == rh::AuthorityDisposition::kRevoked &&
                receipt->output != rh::OutputDisposition::kAccepted && !result_slot_ &&
                gate_.admit(request()).status == rh::AdmissionStatus::kRecoveryRequired &&
                !gate_.can_deliver_result(authority_),
            "observation restoration reauthorized withdrawn work");
    std::cout << nlohmann::json{{"event", "owner_runtime_loss_result"},
                                {"passed", is_runtime_loss_case()},
                                {"case", mode_},
                                {"steady_ms", now()},
                                {"native_closed", native_closed},
                                {"drive_isolated", drive_isolated},
                                {"motion_observed", motion_observed},
                                {"native_cancelled",
                                 receipt->native_outcome == rh::NativeOutcome::kCancelled},
                                {"native_pending",
                                 receipt->native_outcome == rh::NativeOutcome::kPending},
                                {"stop_acknowledged", receipt->stop_acknowledgement ==
                                                          rh::StopAcknowledgement::kAcknowledged},
                                {"output_pending",
                                 receipt->output == rh::OutputDisposition::kPending},
                                {"output_delivered", false},
                                {"settled", false},
                                {"binding_withdrawn", true},
                                {"second_admission_blocked", true},
                                {"native_send_count", native_send_count_},
                                {"native_cancel_count", native_cancel_count_}}
                     .dump()
              << std::endl;
    require(is_runtime_loss_case(), "unexpected runtime observation loss");
    gate_.close_admission();
    return 0;
  }

  rh::nav2_example::RuntimeObservationWatch runtime_watch_;
  bool runtime_loss_seen_ = false;
  rh::MonotonicTime runtime_loss_at_ = 0;
  bool stop_requested_ = false, native_stop_sent_ = false, cancel_response_seen_ = false;
  // No-output and settlement observations share this evidence source.
  std::uint64_t settlement_sequence_ = 0;
  std::uint64_t native_cancel_count_ = 0;

  bool quiet() {
    motion_window_.emplace(std::max({clock_ns_, odometry_stamp_ns_, closure_->drive_stamp_ns()}));
    const auto until = SteadyClock::now() + 8s;
    while (motion_window_->status() == MotionStatus::kPending && rclcpp::ok() &&
           SteadyClock::now() < until) {
      executor_.spin_some(10ms);
      std::this_thread::sleep_for(5ms);
    }
    const bool observed = !callback_error_ && motion_window_->status() == MotionStatus::kObserved &&
                          now() - clock_seen_ < 1000 && now() - odometry_seen_ < 1000;
    std::cout << nlohmann::json{{"event", "owner_quiet_observation"},
                                {"steady_ms", now()},
                                {"observed", observed},
                                {"operation_id", authority_.operation_id},
                                {"samples", motion_window_->sample_count()},
                                {"duration_ns", motion_window_->duration_ns()},
                                {"x", odometry_.x},
                                {"y", odometry_.y}}
                     .dump()
              << std::endl;
    return observed;
  }

  int blocked(const char* boundary) {
    if (callback_error_)
      std::rethrow_exception(callback_error_);
    const auto receipt = gate_.receipt(authority_);
    const auto decision = gate_.admit(request());
    require(receipt && receipt->settlement == rh::SettlementStatus::kPending &&
                receipt->authority_disposition != rh::AuthorityDisposition::kReleased &&
                decision.status == rh::AdmissionStatus::kDomainOccupied && !decision.authority &&
                native_send_count_ == 1,
            "missing handoff evidence released authority");
    std::cout << nlohmann::json{{"event", "owner_handoff_blocked"},
                                {"boundary", boundary},
                                {"a_settled", false},
                                {"b_admitted", false},
                                {"native_send_count", native_send_count_}}
                     .dump()
              << std::endl;
    require(mode_ != "normal" && mode_ != "replace-moving", "normal handoff incomplete");
    gate_.close_admission();
    return 0;
  }

  int handoff() {
    if (!closure_->close())
      return blocked("native-closure");
    if (closure_mode() == "withhold-odometry")
      odometry_subscription_.reset();
    if (!quiet())
      return blocked("fresh-motion");
    std::cout << "{\"event\":\"owner_a_closed\"}" << std::endl;
    // The fixture creates processes only. Owner independently probes the new
    // namespace's services, native readiness and command producers before Core.
    auto prepare = node_->create_client<std_srvs::srv::Trigger>("/prepare_next_context");
    const std::string context = "/m4_task/s_" + b_scope_;
    std::unique_ptr<rh::nav2_example::NativeClosure> next;
    rclcpp_action::Client<Action>::SharedPtr next_client;
    try {
      wait([&] { return prepare->service_is_ready(); }, 5s, "context launcher absent");
      auto pending =
          prepare->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
      wait([&] { return pending.wait_for(0s) == std::future_status::ready; }, 5s,
           "context launcher response absent");
      require(pending.get()->success, "context creation refused");
      next = std::make_unique<rh::nav2_example::NativeClosure>(node_, executor_, "normal", b_scope_,
                                                               2, context);
      next_client = rclcpp_action::create_client<Action>(node_, context + "/navigate_to_pose");
      active(context + "/bt_navigator");
      next->prepare();
      wait([&] { return next_client->action_server_is_ready(); }, 5s, "B action unavailable");
      require(quiet(), "motion stale after B preparation");
      // Recheck native readiness after the quiet window. No ROS callback is
      // dispatched between the final local checks and settlement/admission.
      next->prepare();
      require(next_client->action_server_is_ready() && now() - clock_seen_ < 1000 &&
                  now() - odometry_seen_ < 1000,
              "B readiness lost before settlement");
    } catch (const std::exception& error) {
      std::cerr << "Next context unavailable: " << error.what() << std::endl;
      return blocked("next-context");
    }
    require(mode_ == "normal" || mode_ == "replace-moving",
            "fault case unexpectedly reached settlement");
    require(!callback_error_ && closure_->closed(), "A closure invalidated before settlement");
    accepted(gate_.observe_settlement(
        {authority_,
         config_.settlement_scope,
         true,
         {config_.settlement_evidence_source, now(), ++settlement_sequence_}}));
    const auto a_receipt = gate_.receipt(authority_);
    require(a_receipt && a_receipt->settlement == rh::SettlementStatus::kSettled &&
                a_receipt->authority_disposition == rh::AuthorityDisposition::kReleased,
            "A settlement not released");
    const auto ready_at = now();
    accepted(
        gate_.observe_execution_capabilities({config_.binding,
                                              config_.capability_id,
                                              kProfile,
                                              1,
                                              true,
                                              {config_.execution_capability_observer, ready_at, 2},
                                              ready_at + 2000}));
    const auto b = gate_.admit(request());
    require(b.status == rh::AdmissionStatus::kAdmitted && b.authority,
            "B admission denied after A settlement");
    const auto a_operation = authority_.operation_id;
    if (is_replacement_case()) {
      require(a_receipt->native_outcome == rh::NativeOutcome::kCancelled &&
                  a_receipt->output == rh::OutputDisposition::kNotDelivered && !result_slot_ &&
                  native_cancel_count_ == 1,
              "replacement lost A cancellation/output boundary");
    }
    authority_ = *b.authority;
    require(authority_.operation_id != a_operation, "B reused A operation identity");
    std::cout << nlohmann::json{{"event", "owner_core_handoff"},
                                {"steady_ms", now()},
                                {"a_cancelled",
                                 a_receipt->native_outcome == rh::NativeOutcome::kCancelled},
                                {"a_output_delivered",
                                 a_receipt->output == rh::OutputDisposition::kAccepted},
                                {"a_operation", a_operation},
                                {"b_operation", authority_.operation_id},
                                {"a_settled", true},
                                {"b_admitted", true}}
                     .dump()
              << std::endl;
    client_ = std::move(next_client);
    closure_ = std::move(next);
    // Opening is after admission; actual native submission still requires the
    // immediately adjacent Core dispatch claim in run_goal().
    closure_->open_drive();
    run_goal(is_replacement_case() ? 0.0 : -1.5);
    if (runtime_loss_seen_)
      return finish_runtime_loss();
    require(closure_->close() && quiet(), "B closure or motion incomplete");
    const auto receipt = gate_.receipt(authority_);
    const auto third = gate_.admit(request());
    require(receipt && receipt->native_outcome == rh::NativeOutcome::kSucceeded &&
                receipt->output == rh::OutputDisposition::kAccepted &&
                receipt->settlement == rh::SettlementStatus::kPending &&
                third.status == rh::AdmissionStatus::kDomainOccupied && !third.authority &&
                native_send_count_ == 2 &&
                native_cancel_count_ == (is_replacement_case() ? 1u : 0u),
            "B final boundary inconsistent");
    gate_.close_admission();
    std::cout << nlohmann::json{{"event", "owner_handoff_result"},
                                {"case", mode_},
                                {"native_cancel_count", native_cancel_count_},
                                {"passed", true},
                                {"a_settled", true},
                                {"b_admitted", true},
                                {"b_native_closed", true},
                                {"b_settled", false},
                                {"third_admission_blocked", true},
                                {"native_send_count", native_send_count_}}
                     .dump()
              << std::endl;
    return 0;
  }
  std::string a_scope_, b_scope_;
  std::unique_ptr<rh::nav2_example::NativeClosure> closure_;
#endif

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
                                   "session-navigation-" + std::to_string(authority_.operation_id),
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
  SteadyClock::time_point deadline_ = SteadyClock::now() + 300s;
  std::int64_t clock_ns_ = 0, submitted_sim_ns_ = 0, odometry_stamp_ns_ = 0;
  std::optional<MotionWindow> motion_window_;
  rh::MonotonicTime clock_seen_ = 0, odometry_seen_ = 0, pose_seen_ = 0, feedback_seen_ = 0;
  bool odometry_frame_valid_ = false;
  double odometry_speed_ = 0.0;
  geometry_msgs::msg::Point odometry_, pose_;
  std::optional<Action::Feedback> feedback_;
  std::optional<Result> result_slot_;
  std::uint64_t output_sequence_ = 0;
  std::uint64_t native_sequence_ = 0, feedback_count_ = 0, native_send_count_ = 0;
  bool done_ = false;
  std::exception_ptr callback_error_;
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
  if (argc > 2 ||
      (mode != "normal" && mode != "deny-startup" && mode != "cancel-before-dispatch" &&
       mode != "withhold-odometry"
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
       && mode != "withhold-planner-ack" && mode != "withhold-drive-ack" && mode != "missing-map" &&
       mode != "missing-controller" && mode != "withhold-feedback" && mode != "cancel-moving" &&
       mode != "cancel-moving-withhold-planner-ack" && mode != "cancel-moving-withhold-drive-ack" &&
       mode != "cancel-moving-withhold-odometry" && mode != "replace-moving" &&
       mode != "replace-moving-withhold-planner-ack" &&
       mode != "replace-moving-withhold-drive-ack" && mode != "replace-moving-withhold-odometry" &&
       mode != "replace-moving-missing-map" && mode != "replace-moving-missing-controller" &&
       mode != "runtime-odometry-loss" && mode != "runtime-odometry-replay" &&
       mode != "runtime-odometry-resume" && mode != "runtime-clock-loss" &&
       mode != "runtime-stop-unreachable" && mode != "runtime-stop-unreachable-withhold-drive-ack"
#endif
       )) {
    std::cerr
        << "Expected normal, deny-startup, cancel-before-dispatch or withhold-odometry"
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
        << ", withhold-planner-ack, withhold-drive-ack, missing-map, missing-controller, "
           "withhold-feedback, cancel-moving, cancel-moving-withhold-planner-ack, "
           "cancel-moving-withhold-drive-ack, cancel-moving-withhold-odometry, replace-moving, "
           "replace-moving-withhold-planner-ack, replace-moving-withhold-drive-ack, "
           "replace-moving-withhold-odometry, replace-moving-missing-map or "
           "replace-moving-missing-controller, runtime-odometry-loss, runtime-odometry-replay, "
           "runtime-odometry-resume, runtime-clock-loss, runtime-stop-unreachable or "
           "runtime-stop-unreachable-withhold-drive-ack"
#endif
        << '\n';
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
#ifdef ROBOT_HARNESS_NAV2_SETTLEMENT
    std::cout << "{\"event\":\"incomplete\",\"passed\":false}" << std::endl;
#else
    std::cout << "{\"event\":\"incomplete\",\"passed\":false,\"settled\":false}" << std::endl;
#endif
  }
  rclcpp::shutdown();
  return result;
}
