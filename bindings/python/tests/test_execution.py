"""Real Core coordination checks; synthetic native facts, no physical-stop claim."""

import unittest

from robot_harness import _core
from robot_harness._execution import ExecutionCoordinator, MAX_RECORDS


class ExecutionTests(unittest.TestCase):
    def setUp(self):
        self.stamp = 3
        self.gate = _core.Gate('provider', 'domain', 'cap', 'profile', 10, 0)
        self.addCleanup(self.gate.close)
        for kind in ('idle', 'worker', 'sink'):
            self.assertEqual(self.gate.startup(kind, True, 1), 'accepted')
        self.assertEqual(self.gate.capabilities(True, 2, 1000), 'accepted')
        self.execution = ExecutionCoordinator(self.gate, lambda: self.stamp)

    def reserve(self, reference):
        return self.execution.reserve(reference, ('skill', 1), lambda: {'steps': 0})[0]

    def start(self, reference='first', deadline=None):
        record = self.reserve(reference)
        self.assertEqual(self.execution.admit(record, 1, deadline, self.stamp)['status'], 'admitted')
        self.assertTrue(self.execution.dispatch(record))
        return record

    def native_success(self, record, identity='goal-one'):
        self.assertEqual(self.execution.native(record, 'accepted', identity), 'accepted')
        self.assertEqual(self.execution.native(record, 'succeeded', identity), 'accepted')

    def test_replay_conflict_and_capacity_precede_driver_readiness(self):
        def unavailable():
            raise AssertionError('duplicate or full request must not read the driver')
        rejected = self.reserve('refused')
        rejected['reason'] = 'not_ready'
        replay, is_new = self.execution.reserve('refused', ('skill', 1), unavailable)
        self.assertIs(replay, rejected)
        self.assertFalse(is_new)
        with self.assertRaisesRegex(ValueError, 'different arguments'):
            self.execution.reserve('refused', ('skill', 2), unavailable)
        for index in range(MAX_RECORDS - 1):
            self.reserve(f'rejected-{index}')
        with self.assertRaisesRegex(ValueError, 'capacity'):
            self.execution.reserve('overflow', ('skill', 1), unavailable)
        self.assertIs(self.execution.reserve('refused', ('skill', 1), unavailable)[0], rejected)
        self.assertEqual(rejected['reason'], 'not_ready')

    def test_native_result_settlement_and_driver_release_are_separate(self):
        record = self.start()
        self.assertEqual(self.execution.snapshot(record)['receipt']['native_acceptance'], 'pending')
        with self.assertRaisesRegex(ValueError, 'unsettled'):
            self.execution.release(record)
        self.native_success(record)
        self.assertEqual(self.execution.snapshot(record)['receipt']['settlement'], 'pending')
        self.execution.dispose_result(record, {'value': 1}, lambda: None)
        self.assertEqual(record['result'], {'value': 1})
        self.assertEqual(self.execution.snapshot(record)['receipt']['settlement'], 'pending')
        self.execution.settle(record)
        self.assertIs(self.execution.active, record)
        next_record = self.reserve('next')
        with self.assertRaisesRegex(ValueError, 'released'):
            self.execution.admit(next_record, 1, None, self.stamp)
        self.execution.release(record)
        frozen = self.execution.snapshot(record)
        self.assertEqual(self.execution.admit(next_record, 1, None, self.stamp)['status'], 'admitted')
        self.assertEqual(self.execution.snapshot(record), frozen)

    def test_old_cancel_and_late_native_cannot_select_current_operation(self):
        old = self.start()
        self.native_success(old)
        self.execution.dispose_result(old, {'value': 1}, lambda: None)
        self.execution.settle(old)
        self.execution.release(old)
        current = self.start('second')
        before = self.execution.snapshot(current)
        self.assertFalse(self.execution.cancel(old))
        with self.assertRaisesRegex(ValueError, 'not active'):
            self.execution.native(old, 'cancelled', 'goal-one')
        self.assertEqual(self.execution.snapshot(current), before)
        self.assertEqual(before['receipt']['authority_disposition'], 'current')

    def test_late_acceptance_after_cancel_retains_fact_without_fresh_authority(self):
        record = self.start()
        self.assertTrue(self.execution.cancel(record))
        self.native_success(record, 'late-goal')
        receipt = self.execution.snapshot(record)['receipt']
        self.assertEqual(receipt['native_identity'], 'late-goal')
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertEqual(receipt['settlement'], 'pending')
        self.execution.dispose_result(record, {'value': 1}, lambda: {'late': True})
        self.assertIsNone(record['result'])
        self.assertEqual(record['diagnostic'], {'late': True})
        self.assertEqual(self.execution.snapshot(record)['receipt']['output'], 'not_delivered')
        self.assertIs(self.execution.active, record)

    def test_expiry_during_publication_retracts_staged_result(self):
        record = self.start(deadline=10)
        self.native_success(record)
        self.assertTrue(self.gate.can_deliver(record['operation_id']))
        self.stamp = 11
        self.execution.dispose_result(record, {'value': 1}, lambda: {'expired': True})
        self.assertIsNone(record['result'])
        self.assertEqual(record['diagnostic'], {'expired': True})
        receipt = self.execution.snapshot(record)['receipt']
        self.assertEqual(receipt['output'], 'not_delivered')
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertEqual(receipt['expiry_observed_at'], 11)
        self.assertEqual(receipt['settlement'], 'pending')
        self.execution.settle(record)
        self.assertEqual(record['receipt']['settlement'], 'settled')

    def test_native_success_does_not_bypass_required_result_disposition(self):
        record = self.start()
        self.native_success(record)
        # Current Core requires delivery for a successful, authorized operation.
        # An unknown business verdict does not waive that execution contract.
        with self.assertRaisesRegex(RuntimeError, 'rejected'):
            self.execution.dispose_result(record, None, lambda: {'verdict': 'unknown'})
        self.assertEqual(self.execution.snapshot(record)['receipt']['settlement'], 'pending')
        self.execution.cancel(record)
        self.execution.dispose_result(record, {'value': 1}, lambda: {'verdict': 'unknown'})
        self.execution.settle(record)
        self.assertIsNone(record['result'])
        self.assertEqual(record['diagnostic']['verdict'], 'unknown')
        self.assertEqual(record['receipt']['native_outcome'], 'succeeded')
        self.assertEqual(record['receipt']['output'], 'not_delivered')
        self.assertEqual(record['receipt']['settlement'], 'settled')
        self.assertEqual(record['receipt']['domain_verdict'], 'unassessed')

    def test_unresolved_native_work_cannot_settle_or_release(self):
        record = self.start()
        self.execution.cancel(record)
        with self.assertRaisesRegex(RuntimeError, 'rejected'):
            self.execution.settle(record)
        with self.assertRaisesRegex(ValueError, 'unsettled'):
            self.execution.release(record)
        self.assertIs(self.execution.active, record)
        self.assertEqual(record['state'], 'running')
        self.assertEqual(self.execution.snapshot(record)['receipt']['settlement'], 'pending')


if __name__ == '__main__':
    unittest.main()
