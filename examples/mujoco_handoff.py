"""Deterministic caller for the opt-in continuous ALOHA profile; not an Agent."""
import argparse
import json
import os
from pathlib import Path
import time

from robot_harness import Session

TRANSFER = 'aloha.transfer_cube_segment'
HOLD = 'aloha.hold_current_target'


def reference(observation):
    return {key: observation[key] for key in ('epoch', 'sequence')}


def wait(session, request_id):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        result = session.status(request_id)
        if result['state'] == 'finished':
            return result
        time.sleep(.01)
    session.cancel(request_id)
    raise TimeoutError(f'operation not finished: {request_id}')


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--worker-script', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--device', default='mps')
    parser.add_argument('--seed', type=int, default=0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    plan = {'profile': 'mujoco-stepped-handoff-v1', 'caller': 'deterministic',
            'seed': args.seed, 'operations': [{'skill': TRANSFER, 'steps': 400},
                                            {'skill': HOLD, 'steps': 50}],
            'startup_budget_seconds': 90, 'operation_budget_seconds': 60,
            'task_verdict': 'unassessed', 'explicit_reset_after_hold': True}
    (args.output / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    report = {'status': 'running', 'operations': [], 'observations': []}
    session = None
    try:
        session = Session(startup_timeout=90, mujoco={
            'profile': plan['profile'], 'output': str((args.output / 'episodes').resolve()),
            'seed': args.seed, 'worker_script': str(args.worker_script.resolve()),
            'checkpoint': str(args.checkpoint.resolve()), 'device': args.device})
        report.update(host_pid=session.host_pid, worker_pid=session.worker_pid)
        previous = None
        first = None
        for index, skill in enumerate((TRANSFER, HOLD)):
            observation = session.observe()
            rgb = observation.pop('rgb')
            require(len(rgb) == 480 * 640 * 3 and observation['valid'], 'invalid camera observation')
            report['observations'].append(observation)
            if previous is not None:
                require(observation['epoch'] == previous['epoch'] and observation['sequence'] == 400,
                        'transfer lost continuous episode')
                stale = session.submit('stale-hold', skill=HOLD, expected_observation=reference(previous))
                report['stale_hold'] = stale
                require(stale['state'] == 'rejected' and stale['reason'] == 'observation_mismatch',
                        'stale observation admitted')
            request_id = f'segment-{index}'
            submission = session.submit(request_id, skill=skill, deadline_ms=60000,
                                        expected_observation=reference(observation))
            require(submission['state'] == 'running', f'segment rejected: {submission}')
            result = wait(session, request_id)
            report['operations'].append(result)
            require(result['receipt']['settlement'] == 'settled' and result['result'] is not None,
                    'operation did not deliver and settle')
            require(result['receipt']['domain_verdict'] == 'unassessed', 'Core invented task success')
            if first is None:
                first = result
            previous = observation
        final = session.observe()
        final.pop('rgb')
        report['observations'].append(final)
        require(final['epoch'] == previous['epoch'] and final['sequence'] == 450,
                'hold lost continuous episode')
        require(report['operations'][1]['steps'] == 50, 'hold reused episode step count')
        require(session.status('segment-0') == first, 'old receipt changed')
        report['after_hold'] = session.status()
        require(report['after_hold']['phase'] == 'needs_reset', 'episode reused without reset')
        session.reset(seed=args.seed + 1)
        reset = session.observe()
        reset.pop('rgb')
        report['reset_observation'] = reset
        require(reset['epoch'] == final['epoch'] + 1 and reset['sequence'] == 0, 'reset identity failed')
        require(session.status('segment-0') == first, 'reset changed old receipt')
        report['status'] = 'completed'
    except BaseException as error:
        report.update(status='error', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        try:
            if session is not None:
                session.close()
                for pid in (session.host_pid, session.worker_pid):
                    try:
                        os.kill(pid, 0)
                    except ProcessLookupError:
                        continue
                    raise RuntimeError(f'process still alive: {pid}')
                report['processes_reaped'] = True
        except BaseException as error:
            report.update(status='error', cleanup_error=str(error))
            raise
        finally:
            (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
