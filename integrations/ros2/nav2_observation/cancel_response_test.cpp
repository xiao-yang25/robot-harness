#include "cancel_response.hpp"

#include <stdexcept>

int main() {
  using robot_harness::StopAcknowledgement;
  using robot_harness::nav2_example::cancel_acknowledgement;
  using Response = action_msgs::srv::CancelGoal::Response;
  unique_identifier_msgs::msg::UUID::_uuid_type wanted{};
  wanted[0] = 1;
  Response response;
  auto expect = [&](StopAcknowledgement value) {
    if (cancel_acknowledgement(response, wanted) != value)
      throw std::runtime_error("incorrect targeted cancellation acknowledgement");
  };
  response.return_code = Response::ERROR_NONE;
  expect(StopAcknowledgement::kUnavailable);  // Empty success is not our goal.
  response.goals_canceling.resize(1);
  response.goals_canceling[0].goal_id.uuid[0] = 2;
  expect(StopAcknowledgement::kUnavailable);  // Another goal cannot acknowledge ours.
  response.goals_canceling[0].goal_id.uuid = wanted;
  expect(StopAcknowledgement::kAcknowledged);
  response.goals_canceling.push_back(response.goals_canceling.front());
  expect(StopAcknowledgement::kUnavailable);  // Not the targeted one-goal response.
  response.goals_canceling.resize(1);
  response.return_code = Response::ERROR_REJECTED;
  expect(StopAcknowledgement::kRefused);
  response.return_code = Response::ERROR_UNKNOWN_GOAL_ID;
  expect(StopAcknowledgement::kUnavailable);
  response.return_code = Response::ERROR_GOAL_TERMINATED;
  expect(StopAcknowledgement::kUnavailable);  // Already terminal is not a stop ACK.
  response.return_code = 99;
  expect(StopAcknowledgement::kUnavailable);
}
