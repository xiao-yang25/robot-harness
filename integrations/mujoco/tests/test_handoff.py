"""Actual Host/Core and segment backend, with an explicit no-physics test sink."""
import json
from pathlib import Path
import socket
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import numpy as np

from robot_harness._host import Host
from robot_harness._transport import Channel
from robot_harness_mujoco import AlohaHandoffBackend

TRANSFER = 'aloha.transfer_cube_segment'
HOLD = 'aloha.hold_current_target'


class HandoffTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.effects = []
        self.sim_seconds = 0.
        backend = self.backend = AlohaHandoffBackend.__new__(AlohaHandoffBackend)
        backend.root = self.root
        backend.closed = False
        backend.epoch = -1
        backend.trace = backend.video = None
        backend.env = Mock()
        backend.env.unwrapped._env.physics.data.time = 0.
        observation = {'pixels': {'top': np.zeros((480, 640, 3), dtype=np.uint8)},
                       'agent_pos': np.zeros(14)}
        backend.env.reset.return_value = (observation, {})

        def step(action):
            self.effects.append(action.tolist())
            self.sim_seconds += .02
            backend.env.unwrapped._env.physics.data.time = self.sim_seconds
            return SimpleNamespace(reward=4, observation={**observation, 'agent_pos': action.copy()})

        backend.env.unwrapped._env.step.side_effect = step
        backend.env.unwrapped._format_raw_obs.side_effect = lambda value: value
        backend.facts = lambda: {'sim_seconds': self.sim_seconds}
        writer = Mock()
        fake_imageio = SimpleNamespace(v2=SimpleNamespace(get_writer=Mock(return_value=writer)))
        # Later reset/close paths use the same test-only recorder. The lightweight
        # suite must not silently depend on imageio installed on the developer host.
        recording_patch = patch.dict('sys.modules', {'imageio': fake_imageio, 'imageio.v2': fake_imageio.v2})
        recording_patch.start()
        self.addCleanup(recording_patch.stop)
        backend.reset(seed=0)
        self.parent, self.child = socket.socketpair()
        self.worker_log = self.root / 'worker.jsonl'
        with patch('robot_harness_mujoco.AlohaHandoffBackend', return_value=backend):
            self.host = Host(Channel(self.parent, binary=True), 0, mujoco={
                'profile': backend.profile, 'output': str(self.root),
                'worker_script': str(Path(__file__).with_name('handoff_worker.py')),
                'checkpoint': str(getattr(self, 'worker_delay', 0)), 'device': str(self.worker_log)})
        self.drive(lambda: self.host.worker_ready)

    def tearDown(self):
        try:
            self.host.begin_close()
            self.host.run()
        finally:
            self.child.close()
            self.temp.cleanup()

    def drive(self, condition):
        deadline = time.monotonic() + 5
        while not condition():
            for message in self.host.worker_channel.pump():
                self.host.worker_message(message)
            self.host.advance()
            self.assertLess(time.monotonic(), deadline, self.host.status())
            time.sleep(.001)

    def submit(self, reference, skill, *, steps=None, expected=None, **extra):
        observation = self.backend.observe()
        expected = {'epoch': observation['epoch'], 'sequence': observation['sequence']} if expected is None else expected
        return self.host.submit({'request_id': reference, 'skill': skill,
                                 'steps': (400 if skill == TRANSFER else 50) if steps is None else steps,
                                 'expected_observation': expected, **extra})

    def transfer(self):
        result = self.submit('transfer', TRANSFER)
        self.assertEqual(result['state'], 'running')
        self.drive(lambda: self.host.active is None)
        return self.host.snapshot(self.host.records['transfer'])

    def test_two_settled_operations_retain_epoch_progress_and_exact_target(self):
        first = self.transfer()
        self.assertEqual(self.backend.observe()['sequence'], 400)
        self.assertEqual(self.backend.stage, 'hold_ready')
        self.assertEqual(first['result']['native_reason'], 'segment_executed')
        self.assertEqual(first['receipt']['authority']['binding']['provider'], 'mujoco-host')
        held = self.submit('hold', HOLD)
        self.assertNotEqual(held['operation_id'], first['operation_id'])
        self.drive(lambda: self.host.active is None)
        second = self.host.snapshot(self.host.records['hold'])
        self.assertEqual(second['steps'], 50)
        self.assertEqual(second['result']['native_reason'], 'hold_executed')
        self.assertEqual(self.backend.observe()['sequence'], 450)
        self.assertEqual(self.backend.observe()['epoch'], 0)
        self.assertEqual(self.effects[400:], [[400.] * 14] * 50)
        for record in (first, second):
            self.assertEqual(record['receipt']['native_outcome'], 'succeeded')
            self.assertEqual(record['receipt']['settlement'], 'settled')
            self.assertEqual(record['receipt']['authority_disposition'], 'released')
            self.assertEqual(record['receipt']['domain_verdict'], 'unassessed')
        requests = [json.loads(line) for line in self.worker_log.read_text().splitlines()]
        self.assertEqual([row['count'] for row in requests], [100] * 4)
        self.assertEqual(self.host.snapshot(self.host.records['transfer']), first)
        self.assertEqual(self.host.phase, 'needs_reset')
        self.assertEqual(self.submit('another-hold', HOLD)['state'], 'rejected')
        self.assertEqual(self.submit('another-transfer', TRANSFER)['state'], 'rejected')
        self.host.command({'rpc': 1, 'command': 'reset', 'seed': 1})
        self.assertEqual(self.backend.observe()['epoch'], 1)
        self.assertEqual(self.backend.observe()['sequence'], 0)
        self.assertIsNone(self.host.last_action)
        self.assertEqual(self.host.snapshot(self.host.records['transfer']), first)

    def test_skill_budget_and_observation_preconditions_reject_before_effects(self):
        for reference, skill, options in [
                ('early-hold', HOLD, {}), ('short-transfer', TRANSFER, {'steps': 399}),
                ('old-epoch', TRANSFER, {'expected': {'epoch': 1, 'sequence': 0}})]:
            with self.subTest(reference=reference):
                self.assertEqual(self.submit(reference, skill, **options)['state'], 'rejected')
        missing = self.host.submit({'request_id': 'missing', 'skill': TRANSFER, 'steps': 400})
        self.assertEqual(missing['reason'], 'observation_mismatch')
        self.assertEqual(self.effects, [])
        self.transfer()
        old = {'epoch': 0, 'sequence': 0}
        self.assertEqual(self.submit('stale-hold', HOLD, expected=old)['reason'], 'observation_mismatch')
        self.backend.observation_valid = False
        self.assertEqual(self.submit('invalid-hold', HOLD)['state'], 'rejected')
        self.assertEqual(len(self.effects), 400)

    def test_unresolved_prediction_and_cancelled_transfer_never_enable_hold(self):
        self.submit('cancel', TRANSFER)
        self.host.advance()
        self.assertIsNotNone(self.host.prediction)
        self.assertEqual(self.submit('while-pending', HOLD)['state'], 'rejected')
        self.host.command({'rpc': 1, 'command': 'cancel', 'request_id': 'cancel'})
        self.assertEqual(self.host.gate.receipt(self.host.active['operation_id'])['settlement'], 'pending')
        self.assertEqual(self.submit('after-cancel', HOLD)['state'], 'rejected')
        self.drive(lambda: self.host.active is None)
        self.assertEqual(self.effects, [])
        self.assertEqual(self.host.records['cancel']['receipt']['settlement'], 'settled')
        self.assertEqual(self.host.phase, 'needs_reset')
        self.assertEqual(self.submit('late-hold', HOLD)['state'], 'rejected')

    def test_queued_cancellation_retains_last_effect_and_blocks_followup(self):
        self.submit('cancel', TRANSFER)
        self.drive(lambda: len(self.effects) >= 1)
        self.assertGreater(len(self.host.queue), 1)
        self.host.command({'rpc': 1, 'command': 'cancel', 'request_id': 'cancel'})
        stopped = len(self.effects)
        self.drive(lambda: self.host.active is None)
        self.assertEqual(len(self.effects), stopped)
        self.assertEqual(self.submit('hold-after-partial', HOLD)['state'], 'rejected')
        self.assertEqual(self.host.records['cancel']['receipt']['output'], 'not_delivered')

    def test_expiry_inside_final_step_preserves_native_result_but_blocks_hold(self):
        self.submit('expiry', TRANSFER, deadline_ms=5000)
        expired = self.host.gate.receipt(self.host.active['operation_id'])['deadline'] + 1
        original_now = time.monotonic_ns
        with patch('robot_harness._host.now', side_effect=lambda: expired if len(self.effects) == 400 else original_now()):
            self.drive(lambda: self.host.active is None)
            record = self.host.records['expiry']
            self.assertEqual(record['receipt']['native_outcome'], 'succeeded')
            self.assertEqual(record['receipt']['settlement'], 'settled')
            self.assertEqual(record['receipt']['output_non_delivery_reason'], 'authority_revoked')
            self.assertIsNone(record['result'])
            self.assertEqual(self.host.phase, 'needs_reset')
            self.assertEqual(self.submit('hold-after-expiry', HOLD)['state'], 'rejected')

    def test_recording_failure_cannot_publish_hold_readiness(self):
        self.submit('recording', TRANSFER)
        self.drive(lambda: len(self.effects) == 399)
        with patch.object(self.backend, 'finish', side_effect=OSError('recording failed')):
            with self.assertRaisesRegex(OSError, 'recording failed'):
                self.host.advance()
        receipt = self.host.gate.receipt(self.host.active['operation_id'])
        self.assertEqual(receipt['settlement'], 'pending')
        self.assertEqual(self.submit('hold-after-recording-error', HOLD)['state'], 'rejected')
        self.host.fail_worker('recording failed')

    def test_old_prediction_cannot_supply_a_hold_action(self):
        self.transfer()
        self.submit('hold', HOLD)
        with self.assertRaisesRegex(ValueError, 'unsolicited'):
            self.host.worker_message({'kind': 'prediction', 'identity': {'old': True}, 'actions': [[900.] * 14]})
        self.host.fail_worker('unsolicited worker response')
        self.host.advance()
        self.assertEqual(len(self.effects), 400)
        self.assertEqual(self.host.records['hold']['receipt']['native_outcome'], 'failed')
        self.assertEqual(self.host.phase, 'failed')

    def test_expiry_before_hold_dispatch_records_its_own_zero_steps(self):
        first = self.transfer()
        stamp = time.monotonic_ns()
        admission_observed = False

        def clock():
            nonlocal admission_observed
            if not admission_observed:
                admission_observed = True
                return stamp
            return stamp + 1_000_001

        with patch('robot_harness._host.now', side_effect=clock):
            result = self.submit('expired-hold', HOLD, deadline_ms=1)
        self.assertEqual(len(self.effects), 400)
        self.assertEqual(result['state'], 'finished')
        self.assertEqual(result['steps'], 0)
        self.assertEqual(result['execution']['skill'], HOLD)
        self.assertEqual(result['execution']['steps'], 0)
        self.assertEqual(result['execution']['episode_steps'], 400)
        self.assertEqual(result['execution']['operation_id'], result['operation_id'])
        self.assertNotEqual(result['operation_id'], first['operation_id'])
        self.assertEqual(result['receipt']['native_outcome'], 'not_executed')
        self.assertEqual(result['receipt']['settlement'], 'settled')
        self.assertIsNone(result['result'])
        self.assertEqual(self.host.snapshot(self.host.records['transfer']), first)
        self.assertEqual(self.host.phase, 'needs_reset')

    def test_preparation_recording_error_settles_non_submission(self):
        write = self.backend.write

        def fail_preparation_once(event):
            if event['event'] == 'operation_prepared':
                raise OSError('preparation recording failed')
            return write(event)

        with patch.object(self.backend, 'write', side_effect=fail_preparation_once):
            result = self.submit('start-error', TRANSFER)
        self.assertEqual(self.effects, [])
        self.assertEqual(result['state'], 'finished')
        self.assertEqual(result['steps'], 0)
        self.assertEqual(result['execution']['skill'], TRANSFER)
        self.assertEqual(result['execution']['operation_id'], result['operation_id'])
        self.assertEqual(result['receipt']['dispatch'], 'not_submitted')
        self.assertEqual(result['receipt']['native_outcome'], 'not_executed')
        self.assertEqual(result['receipt']['settlement'], 'settled')
        self.assertEqual(result['receipt']['output'], 'not_delivered')
        self.assertEqual(self.host.phase, 'failed')
        self.assertEqual(self.submit('hold-after-start-error', HOLD)['state'], 'rejected')


if __name__ == '__main__':
    unittest.main()
