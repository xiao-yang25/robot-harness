"""Check the physical scene and the Owner's bounded wait verdict once."""
import json
import math
import os
from pathlib import Path
import subprocess
from gazebo_pose import query_pose


def rows(name):
    return [json.loads(line) for line in (Path('/output') / name).read_text().splitlines()
            if line.startswith('{')]


def verify_observations(owner, fixture, mode):
    verdict = owner[-1]
    assert verdict['event'] == 'owner_cancellation_result' and verdict['passed']
    assert verdict['case'] == mode and verdict['native_closed'] and verdict['motion_observed']
    assert not verdict['settled'] and not verdict['output_delivered']
    assert verdict['second_admission_blocked'] and verdict['native_send_count'] == 1
    assert verdict['native_cancel_count'] == 1
    baseline, = [r for r in owner if r['event'] == 'obstacle_baseline_clear']
    decision, = [r for r in owner if r['event'] == 'obstacle_wait_requested']
    requested, = [r for r in owner if r['event'] == 'owner_cancel_requested']
    response, = [r for r in owner if r['event'] == 'owner_cancel_response']
    terminal, = [r for r in owner if r['event'] == 'native_result']
    assert requested['goal_uuid'] == response['goal_uuid'] == terminal['goal_uuid']
    assert requested['operation_id'] == response['operation_id'] == terminal['operation_id']
    assert decision['steady_ms'] <= requested['steady_ms'] <= min(response['steady_ms'], terminal['steady_ms'])
    assert response['acknowledged'] and terminal['result_code'] == 5
    assert max(response['steady_ms'], terminal['steady_ms']) <= verdict['steady_ms']
    spawn, = [r for r in fixture if r['event'] == 'obstacle_spawned']
    injection, = [r for r in fixture if r['event'] == 'obstacle_spawn_requested']
    assert spawn['steady_ns'] // 1000000 <= decision['steady_ms']
    assert decision['reason'] == ('blocked' if mode == 'obstacle-wait' else 'unknown')
    assert decision['costmap_fresh']
    if mode == 'obstacle-wait':
        assert not injection['frozen_scan'] and decision['scan_fresh']
        assert decision['costmap_occupied'] and decision['clearance_m'] <= 1.05
        assert decision['scan_stamp_ns'] > baseline['scan_stamp_ns']
        assert decision['costmap_stamp_ns'] > baseline['costmap_stamp_ns']
    else:
        assert injection['frozen_scan'] and not decision['scan_fresh']
        assert decision['scan_stamp_ns'] == injection['frozen_scan_stamp_ns']
        assert (decision['scan_progress_age_ms'] >= 1500 or
                decision['sim_ns'] - decision['scan_stamp_ns'] >= 1500000000)
    assert not any(r['event'] == 'owner_core_handoff' for r in owner)
    return decision['reason']


def main():
    mode = os.environ['M4_CONTEXT_CASE']
    reason = verify_observations(rows('owner.jsonl'), rows('obstacle.jsonl'), mode)
    # Query actual Gazebo geometry independently of the spawn request and odometry.
    box = subprocess.run(['gz', 'model', '-m', 'm4_corridor_obstacle', '-p'],
                         check=True, text=True, capture_output=True, timeout=5)
    box_pose = [float(v) for v in box.stdout.split()]
    assert len(box_pose) == 6 and all(math.isfinite(v) for v in box_pose)
    assert math.dist(box_pose[:3], [-.5, -.5, .5]) < .01
    pose = query_pose('obstacle_wait')
    distance = math.hypot(pose[0] + 2, pose[1] + .5)
    assert .15 < distance < 1.0 and pose[0] < -.95, 'must move then stop before box'
    print(json.dumps(dict(passed=True, case=mode, decision=reason,
                          gazebo_final=pose, gazebo_obstacle=box_pose, displacement_m=distance,
                          waiting=True, settled=False, second_admission_blocked=True)))


if __name__ == '__main__':
    main()
