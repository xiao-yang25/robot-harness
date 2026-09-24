"""Focused external check of the injected channel loss and actual robot outcome."""
import json
import math
import os
from pathlib import Path
from gazebo_pose import query_pose


def rows(name):
    return [json.loads(line) for line in Path('/output', name).read_text().splitlines()
            if line.startswith('{')]


mode = os.environ['M4_CONTEXT_CASE']
owner = rows('owner.jsonl')
faults = rows('runtime-relay.jsonl')
started = [row for row in faults if row['event'] == 'observation_fault_started']
assert len(started) == 1 and started[0]['displacement_m'] >= .5 and started[0]['speed_m_s'] >= .05
lost = [row for row in owner if row['event'] == 'owner_runtime_observation_lost']
assert len(lost) == 1 and lost[0]['binding_withdrawn'] and not lost[0]['output_authority']
assert lost[0]['observation'] == ('clock' if mode == 'runtime-clock-loss' else 'odometry')
detection_delay_ms = lost[0]['steady_ms'] - started[0]['steady_ns'] / 10**6
# One second of fixture scheduling margin; this check is not a stop-time SLA.
assert 1500 <= detection_delay_ms <= 3000
assert not any(row['event'] in ('owner_core_handoff', 'owner_cancel_requested') for row in owner)
accepted = [row for row in owner if row['event'] == 'accepted']
assert len(accepted) == 1
terminal = [row for row in owner if row['event'] == 'native_result']
response = [row for row in owner if row['event'] == 'owner_cancel_response']
unreachable = mode.startswith('runtime-stop-unreachable')
last = owner[-1]
assert last['event'] == 'owner_runtime_loss_result' and last['passed'] and last['case'] == mode
assert last['native_cancel_count'] == last['native_send_count'] == 1
assert last['binding_withdrawn'] and last['second_admission_blocked'] and not last['settled']
assert not last['output_delivered']
assert accepted[0]['operation_id'] == lost[0]['operation_id']
drive = rows('drive-native.jsonl')
assert [row['generation'] for row in drive if row['event'] == 'motion_scope_opened'] == [1]
applied = [row for row in drive if row['event'] == 'motion_scope_applied' and row['generation'] == 1]
assert len(applied) == 1
missing_ack = mode == 'runtime-stop-unreachable-withhold-drive-ack'
assert last['drive_isolated'] == (not missing_ack)
if unreachable:
    assert not terminal and not response
    assert last['native_pending'] and last['output_pending']
    assert not last['native_closed'] and not last['native_cancelled'] and not last['stop_acknowledged']
    assert last['motion_observed'] == (not missing_ack)
    frozen = [row for row in faults if row['event'] == 'native_navigator_stop_requested']
    assert len(frozen) == 1
    state = Path('/proc', str(frozen[0]['pid']), 'status').read_text()
    assert any(line.startswith('State:') and 'T (stopped)' in line for line in state.splitlines())
    assert frozen[0]['steady_ns'] < lost[0]['steady_ms'] * 10**6
    requests = [row for row in owner if row['event'] == 'owner_native_observation'
                and row['source'] == 'drive_close_requested']
    assert len(requests) == 1
    request = json.loads(requests[0]['wire'])
    assert request['scope_id'] == applied[0]['scope_id'] and request['generation'] == 1
    restored = [row for row in faults if row['event'] == 'observation_flow_restored']
    assert len(restored) == 1
    assert lost[0]['steady_ms'] * 10**6 <= request['steady_ns'] <= applied[0]['steady_ns'] < restored[0]['steady_ns']
    # Real producers continue after the consumer closes; no authored command injection.
    commands = [row for row in rows('producer-bridge.jsonl')
                if row['event'] == 'producer_command_forwarded' and row['generation'] == 1
                and row['steady_ns'] > applied[0]['steady_ns']
                and (abs(row['linear_x']) > .01 or abs(row['angular_z']) > .01)]
    rejected = [row for row in drive if row['event'] == 'scoped_command_rejected'
                and row.get('command_generation') == 1 and row['steady_ns'] > applied[0]['steady_ns']]
    assert len(commands) >= 10 and len(rejected) >= 10
    assert all(row['scope_id'] == applied[0]['scope_id'] for row in commands)
    seen_ack = [row for row in owner if row['event'] == 'owner_native_observation' and row['source'] == 'drive_ack']
    assert bool(seen_ack) == (not missing_ack)
else:
    assert len(terminal) == len(response) == 1
    assert accepted[0]['goal_uuid'] == terminal[0]['goal_uuid'] == response[0]['goal_uuid']
    assert accepted[0]['operation_id'] == terminal[0]['operation_id'] == response[0]['operation_id']
    assert terminal[0]['result_code'] == 5
    assert response[0]['acknowledged'] and terminal[0]['steady_ms'] >= lost[0]['steady_ms']
    assert last['native_closed'] and last['native_cancelled'] and last['stop_acknowledged']
    assert not last['output_pending'] and not last['native_pending']
    assert last['motion_observed'] == (mode == 'runtime-odometry-resume')
if mode == 'runtime-odometry-replay':
    replayed = [row for row in faults if row['event'] == 'old_odometry_replayed'
                and row['steady_ns'] < lost[0]['steady_ms'] * 10**6]
    assert len(replayed) >= 20
    assert all(row['stamp_ns'] == started[0]['last_forwarded_odom_ns'] for row in replayed)
    assert lost[0]['odometry_message_age_ms'] < 500
if mode == 'runtime-odometry-resume':
    restored = [row for row in faults if row['event'] == 'observation_flow_restored']
    assert len(restored) == 1
    assert lost[0]['steady_ms'] * 10**6 < restored[0]['steady_ns'] < last['steady_ms'] * 10**6
values = query_pose('runtime_final')
distance = math.hypot(values[0] + 2, values[1] + .5)
assert .5 < distance < 2 and math.hypot(values[0] - .7, values[1] + .5) > .5
print(json.dumps(dict(passed=True, case=mode, gazebo_final=values, displacement_m=distance,
                      binding_withdrawn=True, motion_observed=last['motion_observed'],
                      fault_to_detection_ms=detection_delay_ms,
                      native_pending=last['native_pending'], drive_isolated=last['drive_isolated'],
                      continued_nonzero_commands=len(commands) if unreachable else None,
                      rejected_commands=len(rejected) if unreachable else None)))
