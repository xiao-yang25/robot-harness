#ifndef ROBOT_HARNESS_NAV2_CANCEL_RESPONSE_HPP_
#define ROBOT_HARNESS_NAV2_CANCEL_RESPONSE_HPP_

#include "robot_harness/authority_gate.hpp"
#include <action_msgs/srv/cancel_goal.hpp>
#include <unique_identifier_msgs/msg/uuid.hpp>

namespace robot_harness::nav2_example {

// A targeted cancel response is only an acknowledgement for this exact UUID.
// Neither a zero return code nor another goal's cancellation establishes it.
inline StopAcknowledgement
cancel_acknowledgement(const action_msgs::srv::CancelGoal::Response& response,
                       const unique_identifier_msgs::msg::UUID::_uuid_type& expected) {
  using Response = action_msgs::srv::CancelGoal::Response;
  if (response.return_code == Response::ERROR_REJECTED)
    return StopAcknowledgement::kRefused;
  if (response.return_code != Response::ERROR_NONE || response.goals_canceling.size() != 1 ||
      response.goals_canceling.front().goal_id.uuid != expected)
    return StopAcknowledgement::kUnavailable;
  return StopAcknowledgement::kAcknowledged;
}

}  // namespace robot_harness::nav2_example
#endif
