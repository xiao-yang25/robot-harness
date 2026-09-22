#include "native_closure.hpp"

#include <lifecycle_msgs/srv/get_state.hpp>
#include <m4_drive_probe/srv/close_scoped_motion.hpp>
#include <m4_drive_probe/srv/open_motion_scope.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <fstream>
#include <iostream>
#include <thread>

namespace robot_harness::nav2_example {
namespace {
using namespace std::chrono_literals;
std::int64_t steady_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

NativeClosure::NativeClosure(rclcpp::Node::SharedPtr node,
                             rclcpp::executors::SingleThreadedExecutor& executor, std::string mode,
                             std::string scope, std::uint64_t generation, std::string context)
    : node_(std::move(node)), executor_(executor), mode_(std::move(mode)),
      context_(std::move(context)), generation_(generation), facts_(std::move(scope), generation) {
  const auto qos = rclcpp::QoS(1).reliable().transient_local();
  const std::array<std::string, 2> actions{"compute_path_to_pose", "follow_path"};
  const std::array<std::string, 2> links{"navigation_planner_link", "navigation_child_link"};
  for (unsigned index = 0; index < actions.size(); ++index) {
    subscriptions_.push_back(node_->create_subscription<std_msgs::msg::String>(
        context_ + "/" + links[index], qos,
        [this, index](std_msgs::msg::String::ConstSharedPtr msg) {
          record("child_link", msg->data);
          facts_.observe_child(index, false, msg->data);
        }));
    if (index == 0 && mode_ == "withhold-planner-ack") {
      continue;
    }
    subscriptions_.push_back(node_->create_subscription<std_msgs::msg::String>(
        context_ + "/" + actions[index] + "_worker_ack", qos,
        [this, index](std_msgs::msg::String::ConstSharedPtr msg) {
          record("worker_ack", msg->data);
          facts_.observe_child(index, true, msg->data);
        }));
  }
  if (mode_ != "withhold-drive-ack") {
    subscriptions_.push_back(node_->create_subscription<std_msgs::msg::String>(
        "/motion_scope_ack", qos, [this](std_msgs::msg::String::ConstSharedPtr msg) {
          record("drive_ack", msg->data);
          facts_.observe_drive(msg->data, drive_requested_ns_, steady_ns());
        }));
  }
}

void NativeClosure::record(const std::string& source, const std::string& wire) {
  std::cout << nlohmann::json{{"event", "owner_native_observation"},
                              {"source", source},
                              {"scope_id", scope()},
                              {"received_ns", steady_ns()},
                              {"wire", wire}}
                   .dump()
            << std::endl;
}

void NativeClosure::prepare() {
  for (const auto* name : {"planner_server", "controller_server", "velocity_smoother"}) {
    auto service =
        node_->create_client<lifecycle_msgs::srv::GetState>(context_ + "/" + name + "/get_state");
    wait([&] { return service->service_is_ready(); }, 10s, "context lifecycle absent");
    bool active = false;
    const auto until = std::chrono::steady_clock::now() + 15s;
    while (!active && std::chrono::steady_clock::now() < until) {
      auto pending =
          service->async_send_request(std::make_shared<lifecycle_msgs::srv::GetState::Request>());
      wait([&] { return pending.wait_for(0s) == std::future_status::ready; }, 2s,
           "context lifecycle response absent");
      active = pending.get()->current_state.id == 3;
    }
    if (!active)
      throw std::runtime_error("context lifecycle inactive");
  }
  for (const auto* endpoint : {"controller_transform_ready", "planner_map_ready"}) {
    auto service = node_->create_client<std_srvs::srv::Trigger>(context_ + "/" + endpoint);
    wait([&] { return service->service_is_ready(); }, 5s, "controller readiness service absent");
    const auto until = std::chrono::steady_clock::now() + 15s;
    bool ready = false;
    while (!ready && std::chrono::steady_clock::now() < until) {
      auto pending =
          service->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
      wait([&] { return pending.wait_for(0s) == std::future_status::ready; }, 2s,
           "controller readiness response absent");
      ready = pending.get()->success;
    }
    std::cout << nlohmann::json{{"event", "owner_context_readiness"},
                                {"scope_id", scope()},
                                {"endpoint", endpoint},
                                {"ready", ready}}
                     .dump()
              << std::endl;
    if (!ready)
      throw std::runtime_error("navigation context readiness unavailable");
  }
  // Each producer is immutable for this task. A separate bridge owns the
  // generation-stamped drive input; these are trusted exclusive launcher nodes.
  const auto prefix = "/m4_motion/s_" + scope();
  const auto expected_namespace = context_.empty() ? "/" : context_;
  for (const auto& stage : std::array<std::pair<std::string, std::string>, 2>{
           std::pair<std::string, std::string>{"raw", "controller_server"},
           {"smoothed", "velocity_smoother"}}) {
    const auto topic = prefix + "/" + stage.first;
    wait([&] { return !node_->get_publishers_info_by_topic(topic).empty(); }, 5s,
         "task command producer unavailable");
    const auto publishers = node_->get_publishers_info_by_topic(topic);
    if (publishers.size() != 1 || publishers.front().node_namespace() != expected_namespace ||
        publishers.front().node_name() != stage.second) {
      throw std::runtime_error("task command producer identity mismatch");
    }
  }
  const auto bridges = node_->get_publishers_info_by_topic("/scoped_cmd_vel");
  if (bridges.size() != 2 ||
      !std::all_of(bridges.begin(), bridges.end(),
                   [](const auto& publisher) {
                     return publisher.node_namespace() == "/" &&
                            (publisher.node_name() == "producer_bridge_1" ||
                             publisher.node_name() == "producer_bridge_2");
                   }) ||
      bridges[0].node_name() == bridges[1].node_name()) {
    throw std::runtime_error("fixed drive bridges unavailable");
  }
}

void NativeClosure::open_drive() {
  using Open = m4_drive_probe::srv::OpenMotionScope;
  auto service = node_->create_client<Open>("/open_motion_scope");
  wait([&] { return service->service_is_ready(); }, 5s, "drive open service absent");
  auto request = std::make_shared<Open::Request>();
  request->expected_generation = generation_ - 1;
  request->scope_id = scope();
  auto pending = service->async_send_request(request);
  wait([&] { return pending.wait_for(0s) == std::future_status::ready; }, 5s,
       "drive open response absent");
  const auto response = pending.get();
  if (!response->accepted || response->generation != generation_) {
    throw std::runtime_error("drive generation open refused");
  }
  std::cout << nlohmann::json{{"event", "owner_drive_opened"},
                              {"generation", generation_},
                              {"scope_id", scope()}}
                   .dump()
            << std::endl;
}

std::string NativeClosure::goal_tree() {
  const auto path = "/output/request-" + scope() + ".xml";
  std::ofstream tree(path);
  tree << "<root main_tree_to_execute=\"MainTree\"><BehaviorTree ID=\"MainTree\"><Sequence>"
       << "<ScopedComputePathToPose goal=\"{goal}\" path=\"{path}\" planner_id=\"GridBased\" "
          "scope_id=\""
       << scope()
       << "\"/><ScopedFollowPath path=\"{path}\" controller_id=\"FollowPath\" scope_id=\""
       << scope() << "\"/></Sequence></BehaviorTree></root>";
  tree.close();
  if (!tree) {
    throw std::runtime_error("request tree write failed");
  }
  return path;
}

void NativeClosure::bind_parent(const std::string& uuid) {
  facts_.bind_parent(uuid);
  std::cout << nlohmann::json{{"event", "owner_request_scope"},
                              {"scope_id", scope()},
                              {"parent_uuid", uuid}}
                   .dump()
            << std::endl;
}

bool NativeClosure::close() {
  try {
    wait([&] { return facts_.workers_closed(); }, 8s, "native worker identity/closure incomplete");
    auto bt = node_->create_client<std_srvs::srv::Trigger>(context_ + "/close_navigation_scope");
    wait([&] { return bt->service_is_ready(); }, 5s, "BT closure service absent");
    const auto bt_requested = steady_ns();
    auto bt_pending = bt->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
    wait([&] { return bt_pending.wait_for(0s) == std::future_status::ready; }, 8s,
         "BT closure response absent");
    const auto response = bt_pending.get();
    if (!response->success) {
      throw std::runtime_error("BT closure refused");
    }
    record("bt_close", response->message);
    facts_.observe_bt(response->message, bt_requested, steady_ns());

    using Close = m4_drive_probe::srv::CloseScopedMotion;
    auto drive = node_->create_client<Close>("/close_scoped_motion");
    wait([&] { return drive->service_is_ready(); }, 5s, "drive closure service absent");
    auto request = std::make_shared<Close::Request>();
    request->scope_id = scope();
    request->generation = generation_;
    drive_requested_ns_ = steady_ns();
    auto pending = drive->async_send_request(request);
    wait([&] { return pending.wait_for(0s) == std::future_status::ready; }, 5s,
         "drive seal response absent");
    if (!pending.get()->accepted) {
      throw std::runtime_error("drive seal refused");
    }
    wait([&] { return facts_.closed(); }, 5s, "drive applying-update acknowledgement absent");
    return true;
  } catch (const std::exception& error) {
    facts_.reject();
    std::cout << nlohmann::json{{"event", "owner_native_closure_incomplete"},
                                {"scope_id", scope()},
                                {"reason", error.what()}}
                     .dump()
              << std::endl;
    return false;
  }
}
}  // namespace robot_harness::nav2_example
