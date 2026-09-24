"""Focused external movement check; reads Owner verdict, never grants authority."""
import json
import math
import os
from pathlib import Path
import subprocess

mode = os.environ['M4_CONTEXT_CASE']
replacement = mode.startswith('replace-moving')
fault = mode.removeprefix('replace-moving-') if replacement else mode
rows = [json.loads(line) for line in Path('/output/owner.jsonl').read_text().splitlines()
        if line.startswith('{')]
result = subprocess.run(['gz', 'model', '-m', 'turtlebot3_waffle', '-p'],
                        capture_output=True, text=True, timeout=5, check=True)
pose = [float(value) for value in result.stdout.split()]
assert len(pose) == 6 and all(math.isfinite(value) for value in pose)
last = rows[-1]
if mode in ('normal', 'replace-moving'):
    assert last['event'] == 'owner_handoff_result' and last['passed']
    observations = [json.loads(line) for line in Path('/output/context-launcher.log').read_text().splitlines()
                    if line.startswith('{')]
    at_a = next(row['xyz_rpy'] for row in observations if row['phase'] == 'A_closed')
    a_distance = math.hypot(at_a[0] + 2, at_a[1] + .5)
    b_distance = math.hypot(pose[0] - at_a[0], pose[1] - at_a[1])
    if replacement:
        assert .45 < a_distance < 1.5 and b_distance > 1.0
        assert math.hypot(pose[0], pose[1] + .5) < .35
        assert last['native_cancel_count'] == 1 and last['native_send_count'] == 2
        handoff = next(row for row in rows if row['event'] == 'owner_core_handoff')
        assert handoff['a_cancelled'] and not handoff['a_output_delivered']
        a_op, b_op = handoff['a_operation'], handoff['b_operation']
        b_terminal = next(row for row in rows if row['event'] == 'native_result'
                          and row['operation_id'] == b_op)
        assert b_terminal['result_code'] == 4
        scopes = json.loads(Path('/output/producer-context.json').read_text())
        def records(name):
            return [json.loads(line) for line in Path('/output', name).read_text().splitlines()
                    if line.startswith('{')]
        injection = records('old-producer-injection.jsonl')
        assert len(injection) == 40
        start, end = injection[0]['steady_ns'], injection[-1]['steady_ns']
        assert handoff['steady_ms'] * 1000000 < start < end < b_terminal['steady_ms'] * 1000000
        assert all(row['scope_id'] == scopes['a_scope'] and
                   row['input_topic'] == '/m4_motion/s_' + scopes['a_scope'] + '/raw'
                   for row in injection)
        bridge = records('producer-bridge.jsonl')
        def moving(row, scope, generation):
            return (row['event'] == 'producer_command_forwarded' and row['scope_id'] == scope
                    and row['generation'] == generation and start <= row['steady_ns'] <= end
                    and abs(row['linear_x']) > .05)
        assert sum(moving(row, scopes['a_scope'], 1) for row in bridge) >= 10
        assert sum(moving(row, scopes['b_scope'], 2) for row in bridge) >= 10
        drive = records('drive-native.jsonl')
        assert [row['generation'] for row in drive if row['event'] == 'motion_scope_opened'] == [1, 2]
        rejected = [row for row in drive if row['event'] == 'scoped_command_rejected'
                    and row['generation'] == 2 and row['command_generation'] == 1
                    and row['scope_id'] == scopes['b_scope'] and start <= row['steady_ns'] <= end]
        assert len(rejected) >= 10
    else:
        assert a_distance > 2 and b_distance > 1.5
        assert math.hypot(pose[0] + 1.5, pose[1] + .5) < .35
elif mode == 'withhold-feedback':
    assert last['event'] == 'incomplete' and not last['passed']
    assert 'result without pose feedback' in Path('/output/owner.jsonl').read_text()
    assert len([row for row in rows if row['event'] == 'accepted']) == 1
    assert not any(row['event'] == 'owner_core_handoff' for row in rows)
    assert math.hypot(pose[0] - .7, pose[1] + .5) < .35
    a_distance = math.hypot(pose[0] + 2, pose[1] + .5)
    b_distance = None
else:
    assert last['event'] == 'owner_handoff_blocked'
    expected = {'withhold-planner-ack': 'native-closure', 'withhold-drive-ack': 'native-closure',
                'withhold-odometry': 'fresh-motion', 'missing-map': 'next-context',
                'missing-controller': 'next-context'}
    assert last['boundary'] == expected[fault]
    assert not any(row['event'] == 'owner_core_handoff' for row in rows)
    drive_rows = [json.loads(line) for line in Path('/output/drive-native.jsonl').read_text().splitlines()]
    assert [row['generation'] for row in drive_rows if row['event'] == 'motion_scope_opened'] == [1]
    if fault == 'missing-map':
        assert 'navigation context readiness unavailable' in Path('/output/owner.jsonl').read_text()
    if fault == 'missing-controller':
        launch = Path('/output/b/launch.log').read_text()
        assert 'native_worker_planner' in launch and 'native_worker_controller' not in launch
    assert last['native_send_count'] == 1 and not last['a_settled'] and not last['b_admitted']
    a_distance = math.hypot(pose[0] + 2, pose[1] + .5)
    if replacement:
        assert .45 < a_distance < 1.5
        assert len([row for row in rows if row['event'] == 'accepted']) == 1
    else:
        assert math.hypot(pose[0] - .7, pose[1] + .5) < .35
    b_distance = None
if replacement:
    requested = [row for row in rows if row['event'] == 'owner_cancel_requested']
    responses = [row for row in rows if row['event'] == 'owner_cancel_response']
    assert len(requested) == len(responses) == 1 and responses[0]['acknowledged']
    a_terminal = next(row for row in rows if row['event'] == 'native_result'
                      and row['operation_id'] == requested[0]['operation_id'])
    assert a_terminal['goal_uuid'] == requested[0]['goal_uuid'] == responses[0]['goal_uuid']
    assert a_terminal['result_code'] == 5
    assert requested[0]['steady_ms'] <= a_terminal['steady_ms']
print(json.dumps(dict(passed=True, case=mode, gazebo_final=pose,
                      a_displacement_m=a_distance, b_displacement_m=b_distance,
                      old_a_commands_rejected_while_b_moving=len(rejected) if mode == 'replace-moving' else None)))
