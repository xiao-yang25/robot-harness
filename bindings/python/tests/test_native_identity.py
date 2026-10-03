"""Actual Core checks for the private bridge; no ROS or physical-stop claims."""
import threading
import unittest

from robot_harness import _core


def dispatched_operation():
    gate = _core.Gate('provider', 'domain', 'cap', 'profile', 10, 0)
    for kind in ('idle', 'worker', 'sink'):
        if gate.startup(kind, True, 1) != 'accepted':
            raise AssertionError('startup evidence refused')
    if gate.capabilities(True, 2, 100) != 'accepted':
        raise AssertionError('capability evidence refused')
    operation = gate.admit('first', 1, None, 3)['operation_id']
    if operation is None or not gate.dispatch(operation, 4):
        raise AssertionError('operation not dispatched')
    return gate, operation


class NativeIdentityTests(unittest.TestCase):
    def test_legacy_episode_identity_and_completion_are_unchanged(self):
        gate, operation = dispatched_operation()
        self.addCleanup(gate.close)
        self.assertEqual(gate.native(operation, 'accepted', 5), 'accepted')
        self.assertEqual(gate.receipt(operation)['native_identity'], f'episode-{operation}')
        self.assertEqual(gate.native(operation, 'started', 6), 'accepted')
        self.assertEqual(gate.native(operation, 'succeeded', 7), 'accepted')
        self.assertTrue(gate.can_deliver(operation))

    def test_explicit_identity_is_retained_and_mismatched_terminal_is_refused(self):
        gate, operation = dispatched_operation()
        self.addCleanup(gate.close)
        target = '37b08d7fcd164f05973e9067b367d792'
        self.assertEqual(gate.native(operation, 'accepted', 5, target), 'accepted')
        self.assertEqual(gate.receipt(operation)['native_identity'], target)
        self.assertEqual(gate.native(operation, 'started', 6, target), 'accepted')
        self.assertEqual(gate.native(operation, 'succeeded', 7, 'other-goal'), 'rejected')
        self.assertEqual(gate.receipt(operation)['native_outcome'], 'pending')
        self.assertFalse(gate.can_deliver(operation))
        # Omitting identity cannot re-label an explicit native lane as an episode.
        self.assertEqual(gate.native(operation, 'succeeded', 8), 'rejected')
        self.assertEqual(gate.native(operation, 'succeeded', 9, target), 'accepted')
        self.assertTrue(gate.can_deliver(operation))

    def test_late_success_after_cancel_cannot_be_delivered_or_settle(self):
        gate, operation = dispatched_operation()
        self.addCleanup(gate.close)
        gate.cancel(operation, 5)
        self.assertEqual(gate.native(operation, 'accepted', 6, 'late-success'), 'accepted')
        self.assertEqual(gate.native(operation, 'succeeded', 7, 'late-success'), 'accepted')
        receipt = gate.receipt(operation)
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertEqual(receipt['settlement'], 'pending')
        self.assertFalse(gate.can_deliver(operation))
        self.assertEqual(gate.output(operation, 'accepted', 'first', 8), 'rejected')

    def test_late_acceptance_after_cancel_does_not_restore_authority_or_settle(self):
        gate, operation = dispatched_operation()
        self.addCleanup(gate.close)
        pending = gate.receipt(operation)
        self.assertEqual(pending['native_acceptance'], 'pending')
        self.assertEqual(pending['native_identity'], '')
        gate.cancel(operation, 5)
        self.assertEqual(gate.native(operation, 'accepted', 6, 'late-goal'), 'accepted')
        receipt = gate.receipt(operation)
        self.assertEqual(receipt['native_identity'], 'late-goal')
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertEqual(receipt['settlement'], 'pending')
        self.assertFalse(gate.can_deliver(operation))
        self.assertEqual(gate.native(operation, 'cancelled', 7, 'other-goal'), 'rejected')
        self.assertEqual(gate.native(operation, 'cancelled', 8, 'late-goal'), 'accepted')
        self.assertEqual(gate.receipt(operation)['settlement'], 'pending')
        self.assertEqual(gate.output(operation, 'no_output', '', 9), 'accepted')
        self.assertEqual(gate.settle(operation, 10), 'accepted')
        self.assertEqual(gate.receipt(operation)['settlement'], 'settled')
        self.assertFalse(gate.can_deliver(operation))

    def test_old_operation_and_identity_cannot_complete_next_operation(self):
        gate, old = dispatched_operation()
        self.addCleanup(gate.close)
        self.assertEqual(gate.native(old, 'accepted', 5, 'old-goal'), 'accepted')
        self.assertEqual(gate.native(old, 'succeeded', 6, 'old-goal'), 'accepted')
        self.assertEqual(gate.output(old, 'accepted', 'first', 7), 'accepted')
        self.assertEqual(gate.settle(old, 8), 'accepted')
        current = gate.admit('second', 1, None, 9)['operation_id']
        self.assertIsNotNone(current)
        self.assertNotEqual(old, current)
        self.assertTrue(gate.dispatch(current, 10))
        self.assertEqual(gate.native(current, 'accepted', 11, 'new-goal'), 'accepted')
        with self.assertRaises(ValueError):
            gate.native(old, 'cancelled', 12, 'old-goal')
        self.assertEqual(gate.native(current, 'succeeded', 13, 'old-goal'), 'rejected')
        self.assertEqual(gate.receipt(current)['native_outcome'], 'pending')
        self.assertEqual(gate.native(current, 'succeeded', 14, 'new-goal'), 'accepted')

    def test_invalid_identity_does_not_establish_native_acceptance(self):
        gate, operation = dispatched_operation()
        self.addCleanup(gate.close)
        original = gate.receipt(operation)
        for value, error in [(None, TypeError), (True, TypeError), (1, TypeError),
                             ('', ValueError), ('goal\x00suffix', ValueError),
                             ('é' * 513, ValueError)]:
            with self.subTest(identity=value):
                with self.assertRaises(error):
                    gate.native(operation, 'accepted', 5, value)
                self.assertEqual(gate.receipt(operation), original)

    def test_explicit_identity_does_not_bypass_owner_thread(self):
        gate, operation = dispatched_operation()
        self.addCleanup(gate.close)
        failures = []
        def report_from_other_thread():
            try:
                gate.native(operation, 'accepted', 5, 'foreign-thread')
            except Exception as error:
                failures.append(error)
        thread = threading.Thread(target=report_from_other_thread)
        thread.start()
        thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(failures), 1)
        self.assertIsInstance(failures[0], RuntimeError)
        self.assertIn('owner thread', str(failures[0]))
        self.assertEqual(gate.receipt(operation)['native_acceptance'], 'pending')


if __name__ == '__main__':
    unittest.main()
