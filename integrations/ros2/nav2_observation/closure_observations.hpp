#ifndef ROBOT_HARNESS_NAV2_CLOSURE_OBSERVATIONS_HPP_
#define ROBOT_HARNESS_NAV2_CLOSURE_OBSERVATIONS_HPP_

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace robot_harness::nav2_example {

// Private evidence matcher for one trusted, fresh, exclusive fixed-tree session.
// No authority, policy decision, transport or motion verdict lives here.
class ClosureObservations {
public:
  explicit ClosureObservations(std::string scope, std::uint64_t generation = 0)
      : scope_(std::move(scope)), generation_(generation) {
    check(uuid(scope_), "invalid request scope");
  }
  const std::string& scope() const {
    return scope_;
  }
  bool rejected() const {
    return rejected_;
  }
  std::int64_t drive_stamp_ns() const {
    return drive_stamp_ns_;
  }

  void bind_parent(const std::string& parent) {
    check(uuid(parent) && (parent_.empty() || parent_ == parent), "parent identity mismatch");
    parent_ = parent;
  }

  void observe_child(unsigned index, bool acknowledgement, const std::string& wire) try {
    check(index < children_.size(), "invalid child action");
    const auto event = parse(wire);
    auto& child = children_[index];
    const std::string action = index == 0 ? "compute_path_to_pose" : "follow_path";
    if (acknowledgement) {
      check(event.at("event") == action + "_worker_ack" &&
                event.at("worker_future_ready") == true && event.at("endpoint_sealed") == true,
            "invalid worker acknowledgement");
      const auto id = event.at("goal_uuid").get<std::string>();
      check(uuid(id) && (!child.ack || *child.ack == id), "worker identity changed");
      child.ack = id;
    } else {
      check(event.at("event") == "navigation_child_link" && event.at("scope_id") == scope_ &&
                event.at("action") == action,
            "child scope mismatch");
      const auto id = event.at("child_uuid").get<std::string>();
      check(uuid(id) && (!child.link || *child.link == id), "child identity changed");
      child.link = id;
    }
    check(!child.link || !child.ack || child.link == child.ack, "child/worker mismatch");
  } catch (...) {
    rejected_ = true;
    throw;
  }

  bool workers_closed() const {
    return !rejected_ && !parent_.empty() &&
           std::all_of(children_.begin(), children_.end(), [](const Child& child) {
             return child.link && child.ack && child.link == child.ack;
           });
  }

  void observe_bt(const std::string& wire, std::int64_t requested_ns,
                  std::int64_t received_ns) try {
    const auto event = parse(wire);
    check(workers_closed() && event.at("event") == "navigation_bt_closed" &&
              event.at("scope_id") == scope_ && event.at("bt_worker_joined") == true &&
              event.at("endpoint_sealed") == true && event.at("tree_idle") == true,
          "BT closure mismatch");
    ordered(event, requested_ns, received_ns);
    bt_closed_ = true;
  } catch (...) {
    rejected_ = true;
    throw;
  }

  void observe_drive(const std::string& wire, std::int64_t requested_ns,
                     std::int64_t received_ns) try {
    const auto event = parse(wire);
    // The shared drive topic retains the previous task's acknowledgement. It
    // cannot satisfy this generation; validate its generation field before ignoring it.
    if (generation_ != 0) {
      check(event.at("generation").is_number_unsigned(), "invalid drive generation");
      const auto generation = event.at("generation").get<std::uint64_t>();
      if (generation < generation_)
        return;
      check(generation == generation_, "unexpected drive generation");
    }
    check(bt_closed_ && event.at("event") == "motion_scope_applied" &&
              event.at("scope_id") == scope_ && event.at("sealed") == true &&
              event.at("wheel_targets_zero") == true,
          "drive closure mismatch");
    ordered(event, requested_ns, received_ns);
    check(event.at("update_sequence").is_number_unsigned() &&
              event.at("update_sequence").get<std::uint64_t>() > 0 &&
              event.at("sim_ns").is_number_integer() &&
              event.at("sim_ns") <= std::numeric_limits<std::int64_t>::max(),
          "invalid drive update");
    const auto stamp = event.at("sim_ns").get<std::int64_t>();
    check(stamp > 0 && (drive_stamp_ns_ == 0 || drive_stamp_ns_ == stamp), "drive update changed");
    drive_stamp_ns_ = stamp;
  } catch (...) {
    rejected_ = true;
    throw;
  }

  bool closed() const {
    return workers_closed() && bt_closed_ && drive_stamp_ns_ > 0;
  }

  // Transport/parser exceptions also invalidate this one observation permanently.
  void reject() {
    rejected_ = true;
  }

private:
  struct Child {
    std::optional<std::string> link, ack;
  };
  static bool uuid(const std::string& text) {
    return text.size() == 32 && text != std::string(32, '0') &&
           std::all_of(text.begin(), text.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  }
  nlohmann::json parse(const std::string& wire) {
    check(wire.size() <= 4096, "oversized native observation");
    try {
      auto value = nlohmann::json::parse(wire);
      check(value.is_object(), "native observation is not an object");
      return value;
    } catch (...) {
      rejected_ = true;
      throw;
    }
  }
  void check(bool condition, const char* error) {
    if (!condition) {
      rejected_ = true;
      throw std::runtime_error(error);
    }
  }
  void ordered(const nlohmann::json& event, std::int64_t requested, std::int64_t received) {
    check(event.at("steady_ns").is_number_integer() &&
              event.at("steady_ns") <= std::numeric_limits<std::int64_t>::max(),
          "invalid native timestamp");
    const auto stamp = event.at("steady_ns").get<std::int64_t>();
    check(requested > 0 && requested <= stamp && stamp <= received, "native closure ordering");
  }
  std::string scope_, parent_;
  std::uint64_t generation_;
  std::array<Child, 2> children_;
  bool bt_closed_ = false, rejected_ = false;
  std::int64_t drive_stamp_ns_ = 0;
};
}  // namespace robot_harness::nav2_example
#endif
