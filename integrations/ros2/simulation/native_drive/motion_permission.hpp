#ifndef ROBOT_HARNESS_SIMULATION_MOTION_PERMISSION_HPP_
#define ROBOT_HARNESS_SIMULATION_MOTION_PERMISSION_HPP_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace robot_harness::simulation {

struct MotionPermitIdentity {
  std::string scope_id;
  std::uint64_t generation;
  std::uint64_t operation_id;
};

inline bool same_identity(const MotionPermitIdentity& left, const MotionPermitIdentity& right) {
  return left.scope_id == right.scope_id && left.generation == right.generation &&
         left.operation_id == right.operation_id;
}

// Private simulation policy. The caller must serialize all access with wheel writes,
// clear cached targets on closure and independently report actual application.
class MotionPermission {
public:
  enum class State { kBootClosed, kActive, kNormalClosed, kFaultClosed };

  explicit MotionPermission(std::uint64_t lifetime_ns) : lifetime_ns_(lifetime_ns) {
    if (lifetime_ns == 0) {
      throw std::invalid_argument("zero permission lifetime");
    }
  }

  bool open(const MotionPermitIdentity& identity, std::uint64_t expected_generation,
            std::uint64_t issued_ns, std::uint64_t now_ns, bool prior_zero_applied) {
    observe_time(now_ns);
    if (state_ == State::kFaultClosed || !valid_identity(identity) ||
        !valid_stamp(issued_ns, now_ns) ||
        expected_generation == std::numeric_limits<std::uint64_t>::max() ||
        identity.generation != expected_generation + 1) {
      return false;
    }
    if (state_ == State::kActive) {
      // A retry cannot renew or discard an already accepted target.
      return same_identity(identity_, identity) && issued_ns == opened_ns_;
    }
    if (!prior_zero_applied || identity_.generation != expected_generation ||
        identity.scope_id == identity_.scope_id ||
        identity.operation_id == identity_.operation_id) {
      return false;
    }
    identity_ = identity;
    opened_ns_ = issued_ns_ = issued_ns;
    deadline_ns_ = issued_ns + lifetime_ns_;
    state_ = State::kActive;
    return true;
  }

  bool renew(const MotionPermitIdentity& identity, std::uint64_t issued_ns, std::uint64_t now_ns) {
    // Check the previous deadline first: a late fresh renewal cannot reopen it.
    observe_time(now_ns);
    if (state_ != State::kActive || !same_identity(identity_, identity) ||
        issued_ns <= issued_ns_ || !valid_stamp(issued_ns, now_ns)) {
      return false;
    }
    issued_ns_ = issued_ns;
    deadline_ns_ = issued_ns + lifetime_ns_;
    return true;
  }

  bool can_consume(const MotionPermitIdentity& identity, std::uint64_t now_ns) {
    observe_time(now_ns);
    return state_ == State::kActive && same_identity(identity_, identity);
  }

  bool close(const MotionPermitIdentity& identity, std::uint64_t now_ns) {
    observe_time(now_ns);
    if (!same_identity(identity_, identity) || identity_.generation == 0) {
      return false;
    }
    if (state_ == State::kActive) {
      state_ = State::kNormalClosed;
    }
    // Matching close is idempotent, including fault closure; it never clears
    // the fault latch or establishes that zero has actually been applied.
    return true;
  }

  State state() const {
    return state_;
  }
  std::uint64_t deadline_ns() const {
    return deadline_ns_;
  }

private:
  static bool valid_identity(const MotionPermitIdentity& identity) {
    return identity.generation > 0 && identity.operation_id > 0 && identity.scope_id.size() == 32 &&
           identity.scope_id != std::string(32, '0') &&
           std::all_of(identity.scope_id.begin(), identity.scope_id.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  }

  bool valid_stamp(std::uint64_t issued_ns, std::uint64_t now_ns) const {
    return issued_ns > 0 && issued_ns <= now_ns &&
           issued_ns <= std::numeric_limits<std::uint64_t>::max() - lifetime_ns_ &&
           now_ns < issued_ns + lifetime_ns_;
  }

  void observe_time(std::uint64_t now_ns) {
    if (now_ns < observed_ns_ || (state_ == State::kActive && now_ns >= deadline_ns_)) {
      state_ = State::kFaultClosed;
    }
    observed_ns_ = now_ns;
  }

  const std::uint64_t lifetime_ns_;
  MotionPermitIdentity identity_{"", 0, 0};
  State state_{State::kBootClosed};
  std::uint64_t opened_ns_{0};
  std::uint64_t issued_ns_{0};
  std::uint64_t deadline_ns_{0};
  std::uint64_t observed_ns_{0};
};

// Called under the drive mutex, after calculations and immediately before the
// native write call. This is call admission, not a hard bound inside that API.
template <class Clock, class ZeroWriter, class WheelWriter>
bool write_current(MotionPermission& permission, const MotionPermitIdentity& identity, Clock clock,
                   ZeroWriter apply_zero, WheelWriter write) {
  const auto now = clock();
  if (!permission.can_consume(identity, now)) {
    apply_zero(now);
    return false;
  }
  write();
  return true;
}

}  // namespace robot_harness::simulation

#endif
