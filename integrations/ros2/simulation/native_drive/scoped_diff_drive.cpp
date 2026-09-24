// Copyright (c) 2010, Daniel Hewlett, Antons Rebguns
// All rights reserved.
//
// Software License Agreement (BSD License 2.0)
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//  * Neither the name of the company nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/*
 * \file  gazebo_ros_diff_drive.cpp
 *
 * \brief A differential drive plugin for gazebo. Based on the diffdrive plugin
 * developed for the erratic robot (see copyright notice above). The original
 * plugin can be found in the ROS package gazebo_erratic_plugins.
 *
 * \author  Piyush Khandelwal (piyushk@gmail.com)
 *
 * $ Id: 06/21/2013 11:23:40 AM piyushk $
 */

/*
 *
 * The support of acceleration limit was added by
 * \author   George Todoran <todorangrg@gmail.com>
 * \author   Markus Bader <markus.bader@tuwien.ac.at>
 * \date 22th of May 2014
 */

#include "scoped_diff_drive.hpp"
#include <gazebo/common/Time.hh>
#include <gazebo/physics/Joint.hh>
#include <gazebo/physics/Link.hh>
#include <gazebo/physics/Model.hh>
#include <gazebo/physics/World.hh>
#include <gazebo_ros/conversions/builtin_interfaces.hpp>
#include <gazebo_ros/conversions/geometry_msgs.hpp>
#include <gazebo_ros/node.hpp>
#include <geometry_msgs/msg/pose2_d.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <sdf/sdf.hh>

#ifdef NO_ERROR
// NO_ERROR is a macro defined in Windows that's used as an enum in tf2
#undef NO_ERROR
#endif

#ifdef IGN_PROFILER_ENABLE
#include <ignition/common/Profiler.hh>
#endif

#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/transform_listener.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <m4_drive_probe/msg/scoped_twist.hpp>
#include <m4_drive_probe/srv/close_motion_scope.hpp>
#include <m4_drive_probe/srv/close_scoped_motion.hpp>
#include <m4_drive_probe/srv/open_motion_scope.hpp>
#include <memory>
#include <optional>
#include <std_msgs/msg/string.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace gazebo_plugins {
class ScopedDiffDrivePrivate {
public:
  // Research-only one-shot consumer seal. All accesses share lock_.
  void CloseScope(const std::shared_ptr<m4_drive_probe::srv::CloseMotionScope::Request> request,
                  std::shared_ptr<m4_drive_probe::srv::CloseMotionScope::Response> response);
  void Record(const std::string& event, std::optional<uint64_t> command_generation = {});
  void OpenScope(const std::shared_ptr<m4_drive_probe::srv::OpenMotionScope::Request> request,
                 std::shared_ptr<m4_drive_probe::srv::OpenMotionScope::Response> response);
  void
  CloseGeneration(const std::shared_ptr<m4_drive_probe::srv::CloseScopedMotion::Request> request,
                  std::shared_ptr<m4_drive_probe::srv::CloseScopedMotion::Response> response);
  void OnScopedCmd(const m4_drive_probe::msg::ScopedTwist::SharedPtr message);
  std::string scope_;
  bool generation_mode_{false};  // Immutable after Load; explicit experiment opt-in.
  uint64_t generation_{0};
  bool sealed_{false}, applied_{false};
  uint64_t update_sequence_{0};
  std::ofstream log_;
  rclcpp::Service<m4_drive_probe::srv::CloseMotionScope>::SharedPtr close_service_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr ack_pub_;
  rclcpp::Subscription<m4_drive_probe::msg::ScopedTwist>::SharedPtr scoped_sub_;
  rclcpp::Service<m4_drive_probe::srv::OpenMotionScope>::SharedPtr open_service_;
  rclcpp::Service<m4_drive_probe::srv::CloseScopedMotion>::SharedPtr scoped_close_service_;

  /// Indicates where the odometry info is coming from
  enum OdomSource {
    /// Use an ancoder
    ENCODER = 0,

    /// Use ground truth from simulation world
    WORLD = 1,
  };

  /// Indicates which wheel
  enum {
    /// Right wheel
    RIGHT = 0,

    /// Left wheel
    LEFT = 1,
  };

  /// Callback to be called at every simulation iteration.
  /// \param[in] _info Updated simulation info.
  void OnUpdate(const gazebo::common::UpdateInfo& _info);

  /// Callback when a velocity command is received.
  /// \param[in] _msg Twist command message.
  void OnCmdVel(const geometry_msgs::msg::Twist::SharedPtr _msg);

  /// Update wheel velocities according to latest target velocities.
  void UpdateWheelVelocities();

  /// Update odometry according to encoder.
  /// \param[in] _current_time Current simulation time
  void UpdateOdometryEncoder(const gazebo::common::Time& _current_time);

  /// Update odometry according to world
  void UpdateOdometryWorld();

  /// Publish odometry transforms
  /// \param[in] _current_time Current simulation time
  void PublishOdometryTf(const gazebo::common::Time& _current_time);

  /// Publish trasforms for the wheels
  /// \param[in] _current_time Current simulation time
  void PublishWheelsTf(const gazebo::common::Time& _current_time);

  /// Publish odometry messages
  /// \param[in] _current_time Current simulation time
  void PublishOdometryMsg(const gazebo::common::Time& _current_time);

  /// A pointer to the GazeboROS node.
  gazebo_ros::Node::SharedPtr ros_node_;

  /// Subscriber to command velocities
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;

  /// Odometry publisher
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odometry_pub_;

  /// Connection to event called at every world iteration.
  gazebo::event::ConnectionPtr update_connection_;

  /// Distance between the wheels, in meters.
  std::vector<double> wheel_separation_;

  /// Diameter of wheels, in meters.
  std::vector<double> wheel_diameter_;

  /// Maximum wheel torque, in Nm.
  double max_wheel_torque_;

  /// Maximum wheel acceleration
  double max_wheel_accel_;

  /// Desired wheel speed.
  std::vector<double> desired_wheel_speed_;

  /// Speed sent to wheel.
  std::vector<double> wheel_speed_instr_;

  /// Pointers to wheel joints.
  std::vector<gazebo::physics::JointPtr> joints_;

  /// Pointer to model.
  gazebo::physics::ModelPtr model_;

  /// To broadcast TFs
  std::shared_ptr<tf2_ros::TransformBroadcaster> transform_broadcaster_;

  /// Protect variables accessed on callbacks.
  std::mutex lock_;

  /// Linear velocity in X received on command (m/s).
  double target_x_{0.0};

  /// Angular velocity in Z received on command (rad/s).
  double target_rot_{0.0};

  /// Update period in seconds.
  double update_period_;

  /// Last update time.
  gazebo::common::Time last_update_time_;

  /// Keep encoder data.
  geometry_msgs::msg::Pose2D pose_encoder_;

  /// Odometry frame ID
  std::string odometry_frame_;

  /// Last time the encoder was updated
  gazebo::common::Time last_encoder_update_;

  /// Either ENCODER or WORLD
  OdomSource odom_source_;

  /// Keep latest odometry message
  nav_msgs::msg::Odometry odom_;

  /// Robot base frame ID
  std::string robot_base_frame_;

  /// True to publish odometry messages.
  bool publish_odom_;

  /// True to publish wheel-to-base transforms.
  bool publish_wheel_tf_;

  /// True to publish odom-to-world transforms.
  bool publish_odom_tf_;

  /// Store number of wheel pairs
  unsigned int num_wheel_pairs_;

  /// Covariance in odometry
  double covariance_[3];
};

ScopedDiffDrive::ScopedDiffDrive() : impl_(std::make_shared<ScopedDiffDrivePrivate>()) {
}

ScopedDiffDrive::~ScopedDiffDrive() {
  // Callbacks keep private state alive only while executing; there is no cycle.
  impl_->update_connection_.reset();
  impl_->cmd_vel_sub_.reset();
  impl_->close_service_.reset();
  impl_->scoped_sub_.reset();
  impl_->open_service_.reset();
  impl_->scoped_close_service_.reset();
}

void ScopedDiffDrive::Load(gazebo::physics::ModelPtr _model, sdf::ElementPtr _sdf) {
  const char* isolated = std::getenv("M4_ISOLATED_SIMULATION");
  if (!isolated || std::string(isolated) != "1") {
    throw std::runtime_error("scoped drive requires isolated experiment");
  }
  impl_->log_.open("/output/drive-native.jsonl");
  if (!impl_->log_) {
    throw std::runtime_error("drive log unavailable");
  }
  impl_->model_ = _model;
  impl_->generation_mode_ = _sdf->Get<bool>("generation_mode", false).first;
  impl_->sealed_ = impl_->generation_mode_;

  // Initialize ROS node
  impl_->ros_node_ = gazebo_ros::Node::Get(_sdf);

  std::weak_ptr<ScopedDiffDrivePrivate> weak = impl_;
  impl_->ack_pub_ = impl_->ros_node_->create_publisher<std_msgs::msg::String>(
      "motion_scope_ack", rclcpp::QoS(1).reliable().transient_local());
  impl_->close_service_ = impl_->ros_node_->create_service<m4_drive_probe::srv::CloseMotionScope>(
      "close_motion_scope",
      [weak](std::shared_ptr<m4_drive_probe::srv::CloseMotionScope::Request> request,
             std::shared_ptr<m4_drive_probe::srv::CloseMotionScope::Response> response) {
        if (auto state = weak.lock()) {
          state->CloseScope(request, response);
        }
      });

  if (impl_->generation_mode_) {
    impl_->open_service_ = impl_->ros_node_->create_service<m4_drive_probe::srv::OpenMotionScope>(
        "open_motion_scope",
        [weak](std::shared_ptr<m4_drive_probe::srv::OpenMotionScope::Request> request,
               std::shared_ptr<m4_drive_probe::srv::OpenMotionScope::Response> response) {
          if (auto state = weak.lock()) {
            state->OpenScope(request, response);
          }
        });
    impl_->scoped_close_service_ =
        impl_->ros_node_->create_service<m4_drive_probe::srv::CloseScopedMotion>(
            "close_scoped_motion",
            [weak](std::shared_ptr<m4_drive_probe::srv::CloseScopedMotion::Request> request,
                   std::shared_ptr<m4_drive_probe::srv::CloseScopedMotion::Response> response) {
              if (auto state = weak.lock()) {
                state->CloseGeneration(request, response);
              }
            });
    impl_->scoped_sub_ = impl_->ros_node_->create_subscription<m4_drive_probe::msg::ScopedTwist>(
        "scoped_cmd_vel", rclcpp::QoS(10).reliable(),
        [weak](m4_drive_probe::msg::ScopedTwist::SharedPtr msg) {
          if (auto state = weak.lock()) {
            state->OnScopedCmd(msg);
          }
        });
  }

  // Get QoS profiles
  const gazebo_ros::QoS& qos = impl_->ros_node_->get_qos();

  // Get number of wheel pairs in the model
  impl_->num_wheel_pairs_ = static_cast<unsigned int>(_sdf->Get<int>("num_wheel_pairs", 1).first);

  if (impl_->num_wheel_pairs_ < 1) {
    impl_->num_wheel_pairs_ = 1;
    RCLCPP_WARN(impl_->ros_node_->get_logger(),
                "Drive requires at least one pair of wheels. Setting [num_wheel_pairs] to 1");
  }

  // Dynamic properties
  impl_->max_wheel_accel_ = _sdf->Get<double>("max_wheel_acceleration", 0.0).first;
  impl_->max_wheel_torque_ = _sdf->Get<double>("max_wheel_torque", 5.0).first;

  // Get joints and Kinematic properties
  std::vector<gazebo::physics::JointPtr> left_joints, right_joints;

  for (auto left_joint_elem = _sdf->GetElement("left_joint"); left_joint_elem != nullptr;
       left_joint_elem = left_joint_elem->GetNextElement("left_joint")) {
    auto left_joint_name = left_joint_elem->Get<std::string>();
    auto left_joint = _model->GetJoint(left_joint_name);
    if (!left_joint) {
      RCLCPP_ERROR(impl_->ros_node_->get_logger(), "Joint [%s] not found, plugin will not work.",
                   left_joint_name.c_str());
      impl_->ros_node_.reset();
      return;
    }
    left_joint->SetParam("fmax", 0, impl_->max_wheel_torque_);
    left_joints.push_back(left_joint);
  }

  for (auto right_joint_elem = _sdf->GetElement("right_joint"); right_joint_elem != nullptr;
       right_joint_elem = right_joint_elem->GetNextElement("right_joint")) {
    auto right_joint_name = right_joint_elem->Get<std::string>();
    auto right_joint = _model->GetJoint(right_joint_name);
    if (!right_joint) {
      RCLCPP_ERROR(impl_->ros_node_->get_logger(), "Joint [%s] not found, plugin will not work.",
                   right_joint_name.c_str());
      impl_->ros_node_.reset();
      return;
    }
    right_joint->SetParam("fmax", 0, impl_->max_wheel_torque_);
    right_joints.push_back(right_joint);
  }

  if (left_joints.size() != right_joints.size() || left_joints.size() != impl_->num_wheel_pairs_) {
    RCLCPP_ERROR(impl_->ros_node_->get_logger(),
                 "Inconsistent number of joints specified. Plugin will not work.");
    impl_->ros_node_.reset();
    return;
  }

  unsigned int index;
  for (index = 0; index < impl_->num_wheel_pairs_; ++index) {
    impl_->joints_.push_back(right_joints[index]);
    impl_->joints_.push_back(left_joints[index]);
  }

  index = 0;
  impl_->wheel_separation_.assign(impl_->num_wheel_pairs_, 0.34);
  for (auto wheel_separation = _sdf->GetElement("wheel_separation"); wheel_separation != nullptr;
       wheel_separation = wheel_separation->GetNextElement("wheel_separation")) {
    if (index >= impl_->num_wheel_pairs_) {
      RCLCPP_WARN(impl_->ros_node_->get_logger(), "Ignoring rest of specified <wheel_separation>");
      break;
    }
    impl_->wheel_separation_[index] = wheel_separation->Get<double>();
    RCLCPP_INFO(impl_->ros_node_->get_logger(), "Wheel pair %i separation set to [%fm]", index + 1,
                impl_->wheel_separation_[index]);
    index++;
  }

  index = 0;
  impl_->wheel_diameter_.assign(impl_->num_wheel_pairs_, 0.15);
  for (auto wheel_diameter = _sdf->GetElement("wheel_diameter"); wheel_diameter != nullptr;
       wheel_diameter = wheel_diameter->GetNextElement("wheel_diameter")) {
    if (index >= impl_->num_wheel_pairs_) {
      RCLCPP_WARN(impl_->ros_node_->get_logger(), "Ignoring rest of specified <wheel_diameter>");
      break;
    }
    impl_->wheel_diameter_[index] = wheel_diameter->Get<double>();
    RCLCPP_INFO(impl_->ros_node_->get_logger(), "Wheel pair %i diameter set to [%fm]", index + 1,
                impl_->wheel_diameter_[index]);
    index++;
  }

  impl_->wheel_speed_instr_.assign(2 * impl_->num_wheel_pairs_, 0);
  impl_->desired_wheel_speed_.assign(2 * impl_->num_wheel_pairs_, 0);

  // Update rate
  auto update_rate = _sdf->Get<double>("update_rate", 100.0).first;
  if (update_rate > 0.0) {
    impl_->update_period_ = 1.0 / update_rate;
  } else {
    impl_->update_period_ = 0.0;
  }
  impl_->last_update_time_ = _model->GetWorld()->SimTime();

  impl_->cmd_vel_sub_ = impl_->ros_node_->create_subscription<geometry_msgs::msg::Twist>(
      "cmd_vel", qos.get_subscription_qos("cmd_vel", rclcpp::QoS(1)),
      [weak](geometry_msgs::msg::Twist::SharedPtr msg) {
        if (auto state = weak.lock()) {
          state->OnCmdVel(msg);
        }
      });

  RCLCPP_INFO(impl_->ros_node_->get_logger(), "Subscribed to [%s]",
              impl_->cmd_vel_sub_->get_topic_name());

  // Odometry
  impl_->odometry_frame_ = _sdf->Get<std::string>("odometry_frame", "odom").first;
  impl_->robot_base_frame_ = _sdf->Get<std::string>("robot_base_frame", "base_footprint").first;
  impl_->odom_source_ =
      static_cast<ScopedDiffDrivePrivate::OdomSource>(_sdf->Get<int>("odometry_source", 1).first);

  // Advertise odometry topic
  impl_->publish_odom_ = _sdf->Get<bool>("publish_odom", false).first;
  if (impl_->publish_odom_) {
    impl_->odometry_pub_ = impl_->ros_node_->create_publisher<nav_msgs::msg::Odometry>(
        "odom", qos.get_publisher_qos("odom", rclcpp::QoS(1)));

    RCLCPP_INFO(impl_->ros_node_->get_logger(), "Advertise odometry on [%s]",
                impl_->odometry_pub_->get_topic_name());
  }

  // Create TF broadcaster if needed
  impl_->publish_wheel_tf_ = _sdf->Get<bool>("publish_wheel_tf", false).first;
  impl_->publish_odom_tf_ = _sdf->Get<bool>("publish_odom_tf", false).first;
  if (impl_->publish_wheel_tf_ || impl_->publish_odom_tf_) {
    impl_->transform_broadcaster_ =
        std::make_shared<tf2_ros::TransformBroadcaster>(impl_->ros_node_);

    if (impl_->publish_odom_tf_) {
      RCLCPP_INFO(impl_->ros_node_->get_logger(),
                  "Publishing odom transforms between [%s] and [%s]",
                  impl_->odometry_frame_.c_str(), impl_->robot_base_frame_.c_str());
    }

    for (index = 0; index < impl_->num_wheel_pairs_; ++index) {
      if (impl_->publish_wheel_tf_) {
        RCLCPP_INFO(impl_->ros_node_->get_logger(),
                    "Publishing wheel transforms between [%s], [%s] and [%s]",
                    impl_->robot_base_frame_.c_str(),
                    impl_->joints_[2 * index + ScopedDiffDrivePrivate::LEFT]->GetName().c_str(),
                    impl_->joints_[2 * index + ScopedDiffDrivePrivate::RIGHT]->GetName().c_str());
      }
    }
  }

  impl_->covariance_[0] = _sdf->Get<double>("covariance_x", 0.00001).first;
  impl_->covariance_[1] = _sdf->Get<double>("covariance_y", 0.00001).first;
  impl_->covariance_[2] = _sdf->Get<double>("covariance_yaw", 0.001).first;

  // Listen to the update event (broadcast every simulation iteration)
  impl_->update_connection_ = gazebo::event::Events::ConnectWorldUpdateBegin(
      [weak](const gazebo::common::UpdateInfo& info) {
        if (auto state = weak.lock()) {
          state->OnUpdate(info);
        }
      });
}

void ScopedDiffDrive::Reset() {
  std::lock_guard<std::mutex> guard(impl_->lock_);
  impl_->last_update_time_ = impl_->joints_[ScopedDiffDrivePrivate::LEFT]->GetWorld()->SimTime();
  for (unsigned int i = 0; i < impl_->num_wheel_pairs_; ++i) {
    if (impl_->joints_[2 * i + ScopedDiffDrivePrivate::LEFT] &&
        impl_->joints_[2 * i + ScopedDiffDrivePrivate::RIGHT]) {
      impl_->joints_[2 * i + ScopedDiffDrivePrivate::LEFT]->SetParam("fmax", 0,
                                                                     impl_->max_wheel_torque_);
      impl_->joints_[2 * i + ScopedDiffDrivePrivate::RIGHT]->SetParam("fmax", 0,
                                                                      impl_->max_wheel_torque_);
    }
  }
  impl_->pose_encoder_.x = 0;
  impl_->pose_encoder_.y = 0;
  impl_->pose_encoder_.theta = 0;
  impl_->target_x_ = 0;
  impl_->target_rot_ = 0;
}

void ScopedDiffDrivePrivate::OnUpdate(const gazebo::common::UpdateInfo& _info) {
#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE("ScopedDiffDrivePrivate::OnUpdate");
#endif
  // Update encoder even if we're going to skip this update
  if (odom_source_ == ENCODER) {
    UpdateOdometryEncoder(_info.simTime);
  }

  double seconds_since_last_update = (_info.simTime - last_update_time_).Double();

  if (seconds_since_last_update < update_period_) {
    return;
  }

#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE_BEGIN("UpdateOdometryWorld");
#endif
  // Update odom message if using ground truth
  if (odom_source_ == WORLD) {
    UpdateOdometryWorld();
  }
#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE_END();
  IGN_PROFILE_BEGIN("PublishOdometryMsg");
#endif
  if (publish_odom_) {
    PublishOdometryMsg(_info.simTime);
  }
#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE_END();
  IGN_PROFILE_BEGIN("PublishWheelsTf");
#endif
  if (publish_wheel_tf_) {
    PublishWheelsTf(_info.simTime);
  }
#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE_END();
  IGN_PROFILE_BEGIN("PublishOdometryTf");
#endif
  if (publish_odom_tf_) {
    PublishOdometryTf(_info.simTime);
  }
#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE_END();
  IGN_PROFILE_BEGIN("UpdateWheelVelocities");
#endif
  // Serialize command consumption through wheel writes with CloseScope and
  // OnCmdVel. A service cannot acknowledge admission closure halfway through an
  // update that still applies a copied pre-seal target.
  std::lock_guard<std::mutex> guard(lock_);
  ++update_sequence_;
  if (sealed_) {
    bool accepted = true;
    for (unsigned int i = 0; i < 2 * num_wheel_pairs_; ++i) {
      const bool wrote_zero = joints_[i]->SetParam("vel", 0, 0.0);
      accepted = wrote_zero && accepted;
      desired_wheel_speed_[i] = 0.0;
      wheel_speed_instr_[i] = 0.0;
    }
    if (accepted && !applied_) {
      applied_ = true;
      if (generation_mode_ && generation_ == 0) {
        Record("drive_boot_zero_applied");
        last_update_time_ = _info.simTime;
        return;
      }
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
      std_msgs::msg::String message;
      message.data = "{\"event\":\"motion_scope_applied\",\"scope_id\":\"" + scope_ +
                     "\",\"sealed\":true,\"wheel_targets_zero\":true,\"update_sequence\":" +
                     std::to_string(update_sequence_) + ",\"sim_ns\":" +
                     std::to_string(_info.simTime.sec * int64_t{1000000000} + _info.simTime.nsec) +
                     ",\"steady_ns\":" + std::to_string(ns) +
                     (generation_mode_ ? ",\"generation\":" + std::to_string(generation_) : "") +
                     "}";
      log_ << message.data << std::endl;
      ack_pub_->publish(message);
    }
    last_update_time_ = _info.simTime;
    return;
  }
  // Normal movement retains the original acceleration-limited control.
  UpdateWheelVelocities();
#ifdef IGN_PROFILER_ENABLE
  IGN_PROFILE_END();
#endif
  // Current speed
  std::vector<double> current_speed(2 * num_wheel_pairs_);
  for (unsigned int i = 0; i < num_wheel_pairs_; ++i) {
    current_speed[2 * i + LEFT] =
        joints_[2 * i + LEFT]->GetVelocity(0) * (wheel_diameter_[i] / 2.0);
    current_speed[2 * i + RIGHT] =
        joints_[2 * i + RIGHT]->GetVelocity(0) * (wheel_diameter_[i] / 2.0);
  }

  // If max_accel == 0, or target speed is reached
  for (unsigned int i = 0; i < num_wheel_pairs_; ++i) {
    if (max_wheel_accel_ == 0 ||
        ((fabs(desired_wheel_speed_[2 * i + LEFT] - current_speed[2 * i + LEFT]) < 0.01) &&
         (fabs(desired_wheel_speed_[2 * i + RIGHT] - current_speed[2 * i + RIGHT]) < 0.01))) {
      joints_[2 * i + LEFT]->SetParam(
          "vel", 0, desired_wheel_speed_[2 * i + LEFT] / (wheel_diameter_[i] / 2.0));
      joints_[2 * i + RIGHT]->SetParam(
          "vel", 0, desired_wheel_speed_[2 * i + RIGHT] / (wheel_diameter_[i] / 2.0));
    } else {
      if (desired_wheel_speed_[2 * i + LEFT] >= current_speed[2 * i + LEFT]) {
        wheel_speed_instr_[2 * i + LEFT] +=
            fmin(desired_wheel_speed_[2 * i + LEFT] - current_speed[2 * i + LEFT],
                 max_wheel_accel_ * seconds_since_last_update);
      } else {
        wheel_speed_instr_[2 * i + LEFT] +=
            fmax(desired_wheel_speed_[2 * i + LEFT] - current_speed[2 * i + LEFT],
                 -max_wheel_accel_ * seconds_since_last_update);
      }

      if (desired_wheel_speed_[2 * i + RIGHT] > current_speed[2 * i + RIGHT]) {
        wheel_speed_instr_[2 * i + RIGHT] +=
            fmin(desired_wheel_speed_[2 * i + RIGHT] - current_speed[2 * i + RIGHT],
                 max_wheel_accel_ * seconds_since_last_update);
      } else {
        wheel_speed_instr_[2 * i + RIGHT] +=
            fmax(desired_wheel_speed_[2 * i + RIGHT] - current_speed[2 * i + RIGHT],
                 -max_wheel_accel_ * seconds_since_last_update);
      }

      joints_[2 * i + LEFT]->SetParam(
          "vel", 0, wheel_speed_instr_[2 * i + LEFT] / (wheel_diameter_[i] / 2.0));
      joints_[2 * i + RIGHT]->SetParam(
          "vel", 0, wheel_speed_instr_[2 * i + RIGHT] / (wheel_diameter_[i] / 2.0));
    }
  }

  last_update_time_ = _info.simTime;
}

void ScopedDiffDrivePrivate::UpdateWheelVelocities() {
  // Called only by OnUpdate while holding lock_.

  double vr = target_x_;
  double va = target_rot_;

  for (unsigned int i = 0; i < num_wheel_pairs_; ++i) {
    desired_wheel_speed_[2 * i + LEFT] = vr - va * wheel_separation_[i] / 2.0;
    desired_wheel_speed_[2 * i + RIGHT] = vr + va * wheel_separation_[i] / 2.0;
  }
}

void ScopedDiffDrivePrivate::OnCmdVel(const geometry_msgs::msg::Twist::SharedPtr _msg) {
  std::lock_guard<std::mutex> scoped_lock(lock_);
  if (generation_mode_) {
    if (_msg->linear.x != 0.0 || _msg->angular.z != 0.0) {
      Record("unscoped_command_rejected");
    }
    return;
  }
  if (sealed_) {
    if (_msg->linear.x != 0.0 || _msg->angular.z != 0.0) {
      Record("late_nonzero_command_rejected");
    }
    return;
  }
  target_x_ = _msg->linear.x;
  target_rot_ = _msg->angular.z;
}

void ScopedDiffDrivePrivate::Record(const std::string& event,
                                    std::optional<uint64_t> command_generation) {
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  log_ << "{\"event\":\"" << event << "\",\"scope_id\":\"" << scope_ << "\",\"steady_ns\":" << ns
       << (generation_mode_ ? ",\"generation\":" + std::to_string(generation_) : "")
       << (command_generation ? ",\"command_generation\":" + std::to_string(*command_generation)
                              : "")
       << "}" << std::endl;
}

void ScopedDiffDrivePrivate::CloseScope(
    const std::shared_ptr<m4_drive_probe::srv::CloseMotionScope::Request> request,
    std::shared_ptr<m4_drive_probe::srv::CloseMotionScope::Response> response) {
  std::lock_guard<std::mutex> guard(lock_);
  if (generation_mode_) {
    response->accepted = false;
    return;
  }
  const auto& scope = request->scope_id;
  if (scope.size() != 32 || scope == std::string(32, '0') ||
      !std::all_of(scope.begin(), scope.end(),
                   [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
      (sealed_ && scope_ != scope)) {
    response->accepted = false;
    return;
  }
  if (!sealed_) {
    scope_ = scope;
    sealed_ = true;
    target_x_ = 0.0;
    target_rot_ = 0.0;
    Record("motion_scope_sealed");
  }
  response->accepted = true;  // Admission only; OnUpdate emits consumed-stop ack.
}

void ScopedDiffDrivePrivate::OpenScope(
    const std::shared_ptr<m4_drive_probe::srv::OpenMotionScope::Request> request,
    std::shared_ptr<m4_drive_probe::srv::OpenMotionScope::Response> response) {
  std::lock_guard<std::mutex> guard(lock_);
  response->accepted = false;
  response->generation = generation_;
  const auto& scope = request->scope_id;
  const auto expected = request->expected_generation;
  if (!generation_mode_ || expected == std::numeric_limits<uint64_t>::max() || scope.size() != 32 ||
      scope == std::string(32, '0') || !std::all_of(scope.begin(), scope.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      })) {
    return;
  }
  // Same request retry never clears a command or opens an already closed scope.
  if (!sealed_ && generation_ == expected + 1 && scope_ == scope) {
    response->accepted = true;
    return;
  }
  // Generation zero is boot: its first physics update must also write zero.
  if (generation_ != expected || !sealed_ || !applied_) {
    return;
  }
  ++generation_;
  scope_ = scope;
  target_x_ = target_rot_ = 0.0;
  applied_ = false;
  sealed_ = false;
  response->accepted = true;
  response->generation = generation_;
  Record("motion_scope_opened");
}

void ScopedDiffDrivePrivate::CloseGeneration(
    const std::shared_ptr<m4_drive_probe::srv::CloseScopedMotion::Request> request,
    std::shared_ptr<m4_drive_probe::srv::CloseScopedMotion::Response> response) {
  std::lock_guard<std::mutex> guard(lock_);
  response->accepted = false;
  if (!generation_mode_ || generation_ == 0 || request->generation != generation_ ||
      request->scope_id != scope_) {
    return;
  }
  if (!sealed_) {
    sealed_ = true;
    applied_ = false;
    target_x_ = target_rot_ = 0.0;
    Record("motion_scope_sealed");
  }
  response->accepted = true;
}

void ScopedDiffDrivePrivate::OnScopedCmd(
    const m4_drive_probe::msg::ScopedTwist::SharedPtr message) {
  std::lock_guard<std::mutex> guard(lock_);
  if (!generation_mode_ || generation_ == 0 || sealed_ || message->generation != generation_ ||
      message->scope_id != scope_ || !std::isfinite(message->twist.linear.x) ||
      !std::isfinite(message->twist.angular.z)) {
    Record("scoped_command_rejected", message->generation);
    return;
  }
  target_x_ = message->twist.linear.x;
  target_rot_ = message->twist.angular.z;
}

void ScopedDiffDrivePrivate::UpdateOdometryEncoder(const gazebo::common::Time& _current_time) {
  double vl = joints_[LEFT]->GetVelocity(0);
  double vr = joints_[RIGHT]->GetVelocity(0);

  double seconds_since_last_update = (_current_time - last_encoder_update_).Double();
  last_encoder_update_ = _current_time;

  double b = wheel_separation_[0];

  // Book: Sigwart 2011 Autonompus Mobile Robots page:337
  double sl = vl * (wheel_diameter_[0] / 2.0) * seconds_since_last_update;
  double sr = vr * (wheel_diameter_[0] / 2.0) * seconds_since_last_update;
  double ssum = sl + sr;

  double sdiff = sr - sl;

  double dx = (ssum) / 2.0 * cos(pose_encoder_.theta + (sdiff) / (2.0 * b));
  double dy = (ssum) / 2.0 * sin(pose_encoder_.theta + (sdiff) / (2.0 * b));
  double dtheta = (sdiff) / b;

  pose_encoder_.x += dx;
  pose_encoder_.y += dy;
  pose_encoder_.theta += dtheta;

  double w = dtheta / seconds_since_last_update;
  double v = sqrt(dx * dx + dy * dy) / seconds_since_last_update;

  tf2::Quaternion qt;
  tf2::Vector3 vt;
  qt.setRPY(0, 0, pose_encoder_.theta);
  vt = tf2::Vector3(pose_encoder_.x, pose_encoder_.y, 0);

  odom_.pose.pose.position.x = vt.x();
  odom_.pose.pose.position.y = vt.y();
  odom_.pose.pose.position.z = vt.z();

  odom_.pose.pose.orientation.x = qt.x();
  odom_.pose.pose.orientation.y = qt.y();
  odom_.pose.pose.orientation.z = qt.z();
  odom_.pose.pose.orientation.w = qt.w();

  odom_.twist.twist.angular.z = w;
  odom_.twist.twist.linear.x = v;
  odom_.twist.twist.linear.y = 0;
}

void ScopedDiffDrivePrivate::UpdateOdometryWorld() {
  auto pose = model_->WorldPose();
  odom_.pose.pose.position = gazebo_ros::Convert<geometry_msgs::msg::Point>(pose.Pos());
  odom_.pose.pose.orientation = gazebo_ros::Convert<geometry_msgs::msg::Quaternion>(pose.Rot());

  // Get velocity in odom frame
  auto linear = model_->WorldLinearVel();
  odom_.twist.twist.angular.z = model_->WorldAngularVel().Z();

  // Convert velocity to child_frame_id(aka base_footprint)
  float yaw = pose.Rot().Yaw();
  odom_.twist.twist.linear.x = cosf(yaw) * linear.X() + sinf(yaw) * linear.Y();
  odom_.twist.twist.linear.y = cosf(yaw) * linear.Y() - sinf(yaw) * linear.X();
}

void ScopedDiffDrivePrivate::PublishOdometryTf(const gazebo::common::Time& _current_time) {
  geometry_msgs::msg::TransformStamped msg;
  msg.header.stamp = gazebo_ros::Convert<builtin_interfaces::msg::Time>(_current_time);
  msg.header.frame_id = odometry_frame_;
  msg.child_frame_id = robot_base_frame_;
  msg.transform.translation =
      gazebo_ros::Convert<geometry_msgs::msg::Vector3>(odom_.pose.pose.position);
  msg.transform.rotation = odom_.pose.pose.orientation;

  transform_broadcaster_->sendTransform(msg);
}

void ScopedDiffDrivePrivate::PublishWheelsTf(const gazebo::common::Time& _current_time) {
  for (unsigned int i = 0; i < 2 * num_wheel_pairs_; ++i) {
    auto pose_wheel = joints_[i]->GetChild()->RelativePose();

    geometry_msgs::msg::TransformStamped msg;
    msg.header.stamp = gazebo_ros::Convert<builtin_interfaces::msg::Time>(_current_time);
    msg.header.frame_id = joints_[i]->GetParent()->GetName();
    msg.child_frame_id = joints_[i]->GetChild()->GetName();
    msg.transform.translation = gazebo_ros::Convert<geometry_msgs::msg::Vector3>(pose_wheel.Pos());
    msg.transform.rotation = gazebo_ros::Convert<geometry_msgs::msg::Quaternion>(pose_wheel.Rot());

    transform_broadcaster_->sendTransform(msg);
  }
}

void ScopedDiffDrivePrivate::PublishOdometryMsg(const gazebo::common::Time& _current_time) {
  // Set covariance
  odom_.pose.covariance[0] = covariance_[0];
  odom_.pose.covariance[7] = covariance_[1];
  odom_.pose.covariance[14] = 1000000000000.0;
  odom_.pose.covariance[21] = 1000000000000.0;
  odom_.pose.covariance[28] = 1000000000000.0;
  odom_.pose.covariance[35] = covariance_[2];

  odom_.twist.covariance[0] = covariance_[0];
  odom_.twist.covariance[7] = covariance_[1];
  odom_.twist.covariance[14] = 1000000000000.0;
  odom_.twist.covariance[21] = 1000000000000.0;
  odom_.twist.covariance[28] = 1000000000000.0;
  odom_.twist.covariance[35] = covariance_[2];

  // Set header
  odom_.header.frame_id = odometry_frame_;
  odom_.child_frame_id = robot_base_frame_;
  odom_.header.stamp = gazebo_ros::Convert<builtin_interfaces::msg::Time>(_current_time);

  // Publish
  odometry_pub_->publish(odom_);
}
GZ_REGISTER_MODEL_PLUGIN(ScopedDiffDrive)
}  // namespace gazebo_plugins
