"""Check resumed motion and the new post-wait admission boundary."""
import json
import math
import os
from check_obstacle_wait import rows, verify_trigger
from gazebo_pose import query_pose


def one(records, event):
    result, = [r for r in records if r['event'] == event]
    return result


def verify_resume(owner, fixture, mode):
    _, terminal_ms = verify_trigger(owner, fixture, 'obstacle-wait')
    waiting = one(owner, 'obstacle_waiting')
    removed = one(fixture, 'obstacle_removed')
    removal = one(fixture, 'obstacle_remove_requested')
    observation = one(owner, 'obstacle_resume_observation')
    verdict = owner[-1]
    assert terminal_ms <= waiting['steady_ms'] < removal['steady_ns'] / 1000000
    assert removal['steady_ns'] <= removed['steady_ns']
    accepted = [r for r in owner if r['event'] == 'accepted']
    cancel = one(owner, 'owner_cancel_requested')
    assert accepted[0]['goal_uuid'] == cancel['goal_uuid']
    assert accepted[0]['operation_id'] == cancel['operation_id']
    if mode == 'obstacle-resume-frozen-scan':
        assert not observation['ready'] and not observation['scan_fresh']
        assert observation['scan_stamp_ns'] == removal['frozen_scan_stamp_ns']
        assert observation['steady_ms'] > removed['steady_ns'] / 1000000
    else:
        assert observation['ready']
        assert observation['steady_ms'] >= removal['steady_ns'] / 1000000
    if mode in ('obstacle-resume', 'obstacle-resume-dispatch-scan-loss', 'obstacle-resume-runtime-loss'):
        checks = [one(owner, event) for event in ('obstacle_resume_observation',
                  'obstacle_resume_settlement_check', 'obstacle_resume_dispatch_check')]
        for check in checks[:2] if mode.endswith('dispatch-scan-loss') else checks:
            assert check['ready'] and check['scan_fresh'] and check['costmap_fresh']
            assert check['scan_stamp_ns'] > waiting['after_ns']
            assert check['costmap_stamp_ns'] > waiting['after_ns']
            assert not check['costmap_occupied'] and check['clearance_m'] > 1.3
        handoff = one(owner, 'owner_core_handoff')
        assert checks[0]['steady_ms'] <= checks[1]['steady_ms'] <= handoff['steady_ms'] <= checks[2]['steady_ms']
        assert handoff['a_cancelled'] and not handoff['a_output_delivered']
        assert handoff['a_settled'] and handoff['b_admitted']
        assert handoff['a_operation'] == cancel['operation_id']
        assert handoff['b_operation'] != handoff['a_operation']
        if mode.endswith('dispatch-scan-loss'):
            assert not checks[2]['ready'] and not checks[2]['scan_fresh']
            assert checks[2]['costmap_fresh']
            assert len(accepted) == 1
            assert verdict['event'] == 'owner_resume_dispatch_refused' and verdict['passed']
            assert verdict['b_operation'] == handoff['b_operation'] and not verdict['b_submitted']
            assert verdict['b_revoked'] and not verdict['b_settled']
            assert verdict['drive_isolated'] and verdict['motion_observed']
            assert verdict['native_send_count'] == 1
            return False
        assert len(accepted) == 2
        assert handoff['b_operation'] == accepted[1]['operation_id'] != accepted[0]['operation_id']
        assert accepted[0]['goal_uuid'] != accepted[1]['goal_uuid']
        b_terminal, = [r for r in owner if r['event'] == 'native_result'
                       and r['operation_id'] == accepted[1]['operation_id']]
        assert b_terminal['goal_uuid'] == accepted[1]['goal_uuid']
        if mode.endswith('runtime-loss'):
            loss = one(owner, 'owner_runtime_observation_lost')
            assert loss['operation_id'] == handoff['b_operation']
            assert loss['observation'] == 'odometry' and loss['binding_withdrawn']
            assert not loss['output_authority'] and b_terminal['result_code'] == 5
            response, = [r for r in owner if r['event'] == 'owner_cancel_response'
                          and r['operation_id'] == handoff['b_operation']]
            assert response['goal_uuid'] == b_terminal['goal_uuid'] and response['acknowledged']
            assert verdict['event'] == 'owner_runtime_loss_result' and verdict['passed']
            assert verdict['binding_withdrawn'] and verdict['second_admission_blocked']
            assert not verdict['settled'] and not verdict['output_delivered']
            assert verdict['native_closed'] and verdict['drive_isolated'] and verdict['motion_observed']
            assert verdict['native_send_count'] == verdict['native_cancel_count'] == 2
            return True
        assert b_terminal['result_code'] == 4
        assert verdict['event'] == 'owner_handoff_result' and verdict['passed']
        assert verdict['native_send_count'] == 2 and verdict['native_cancel_count'] == 1
        assert verdict['b_native_closed'] and not verdict['b_settled'] and verdict['third_admission_blocked']
        return True
    assert verdict['event'] == 'owner_handoff_blocked'
    assert verdict['boundary'] == ('clear-corridor' if mode.endswith('frozen-scan') else 'next-context')
    assert not verdict['a_settled'] and not verdict['b_admitted'] and verdict['native_send_count'] == 1
    assert len(accepted) == 1 and not any(r['event'] == 'owner_core_handoff' for r in owner)
    return False


def main():
    mode = os.environ['M4_CONTEXT_CASE']
    owner, fixture = rows('owner.jsonl'), rows('obstacle.jsonl')
    resumed = verify_resume(owner, fixture, mode)
    stopped = one(fixture, 'obstacle_waiting_pose')
    at_a = stopped['xyz_rpy']
    assert math.dist(stopped['box_pose'][:3], [-.5, -.5, .5]) < .01
    a_distance = math.hypot(at_a[0] + 2, at_a[1] + .5)
    assert .15 < a_distance < 1 and at_a[0] < -.95
    pose = query_pose('obstacle_resume_final')
    b_distance = math.hypot(pose[0] - at_a[0], pose[1] - at_a[1])
    drive = rows('drive-native.jsonl')
    assert [r['generation'] for r in drive if r['event'] == 'motion_scope_opened'] == ([1, 2] if resumed or mode.endswith('dispatch-scan-loss') else [1])
    if mode.endswith('runtime-loss'):
        fault = one(rows('runtime-relay.jsonl'), 'observation_fault_started')
        restored = one(rows('runtime-relay.jsonl'), 'observation_flow_restored')
        lost = one(owner, 'owner_runtime_observation_lost')
        assert fault['steady_ns'] < lost['steady_ms'] * 1000000 < restored['steady_ns']
        assert owner[-1]['steady_ms'] * 1000000 > restored['steady_ns']
        assert .2 < b_distance < 1.5 and pose[0] < .3
    elif resumed:
        assert b_distance > 1.5 and math.hypot(pose[0] - .7, pose[1] + .5) < .35
    else:
        assert b_distance < .03, 'blocked recovery must remain stopped'
    if mode.endswith('missing-controller'):
        from pathlib import Path
        launch = Path('/output/b/launch.log').read_text()
        assert 'native_worker_planner' in launch and 'native_worker_controller' not in launch
    print(json.dumps(dict(passed=True, case=mode, resumed=resumed, gazebo_final=pose,
                          a_displacement_m=a_distance, b_displacement_m=b_distance)))


if __name__ == '__main__':
    main()
