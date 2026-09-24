"""External simulation check: moving cancellation and closed-drive late commands.

The product Owner alone evaluates closure/admission. This checker verifies actual
Gazebo displacement and one focused negative drive input after its verdict.
"""
import json
import math
import os
from pathlib import Path
import time
from gazebo_pose import query_pose


def rows(path):
    return [json.loads(line) for line in Path(path).read_text().splitlines()
            if line.startswith('{')]


def position():
    return query_pose('cancellation_check')


mode = os.environ['M4_CONTEXT_CASE']
observations = rows('/output/owner.jsonl')
verdict = observations[-1]
assert verdict['event'] == 'owner_cancellation_result' and verdict['passed']
assert verdict['case'] == mode and not verdict['settled'] and not verdict['output_delivered']
assert verdict['second_admission_blocked'] and verdict['native_send_count'] == 1
assert verdict['native_cancel_count'] == 1
requested, = [r for r in observations if r['event'] == 'owner_cancel_requested']
response, = [r for r in observations if r['event'] == 'owner_cancel_response']
terminal, = [r for r in observations if r['event'] == 'native_result']
assert requested['goal_uuid'] == response['goal_uuid'] == terminal['goal_uuid']
assert requested['steady_ms'] <= min(response['steady_ms'], terminal['steady_ms'])
assert requested['speed_m_s'] >= .05 and requested['displacement_m'] >= .5
assert response['acknowledged'] and terminal['result_code'] == 5  # Humble ResultCode::CANCELED / GoalStatus::STATUS_CANCELED
assert not any(r['event'] == 'owner_core_handoff' for r in observations)
pose = position()
distance = math.hypot(pose[0] + 2, pose[1] + .5)
assert .45 < distance < 1.5, 'must move, then stop before the original target'
assert math.hypot(pose[0] - .7, pose[1] + .5) > .7
expected_closed = mode in ('cancel-moving', 'cancel-moving-withhold-odometry')
assert verdict['native_closed'] == expected_closed
assert verdict['motion_observed'] == (mode == 'cancel-moving')
late_rejections = None
if mode == 'cancel-moving':
    import rclpy
    from m4_drive_probe.msg import ScopedTwist

    rclpy.init()
    node = rclpy.create_node('cancel_late_command_check')
    publisher = node.create_publisher(ScopedTwist, '/scoped_cmd_vel', 10)
    try:
        end = time.monotonic() + 5
        while not publisher.get_subscription_count() and time.monotonic() < end:
            rclpy.spin_once(node, timeout_sec=.05)
        assert publisher.get_subscription_count(), 'drive subscriber absent'
        contexts = json.loads(Path('/output/producer-context.json').read_text())
        command = ScopedTwist()
        command.generation = 1
        command.scope_id = contexts['a_scope']
        command.twist.linear.x = .2
        injection_ns = time.monotonic_ns()
        end = time.monotonic() + 1
        while time.monotonic() < end:
            publisher.publish(command)
            rclpy.spin_once(node, timeout_sec=.1)
        after = position()
        assert math.hypot(after[0] - pose[0], after[1] - pose[1]) < .01
        late_rejections = len([r for r in rows('/output/drive-native.jsonl')
                               if r['event'] == 'scoped_command_rejected'
                               and r['scope_id'] == command.scope_id
                               and r['generation'] == r['command_generation'] == 1
                               and r['steady_ns'] >= injection_ns])
        assert late_rejections > 0, 'no native rejection of injected late command'
    finally:
        node.destroy_node()
        rclpy.shutdown()
quiet = [r for r in observations if r['event'] == 'owner_quiet_observation' and r['observed']]
print(json.dumps(dict(passed=True, case=mode, gazebo_final=pose,
                      displacement_m=distance, late_command_rejections=late_rejections,
                      response_after_cancel_ms=response['steady_ms'] - requested['steady_ms'],
                      terminal_after_cancel_ms=terminal['steady_ms'] - requested['steady_ms'],
                      quiet_observed_after_cancel_ms=(quiet[-1]['steady_ms'] - requested['steady_ms']) if quiet else None)))
