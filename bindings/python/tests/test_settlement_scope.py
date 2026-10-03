"""Private owner-supplied scope configuration; not physical closure evidence."""
import unittest

from robot_harness import _core


class SettlementScopeTests(unittest.TestCase):
    def test_explicit_scope_is_used_for_actual_core_settlement(self):
        gate = _core.Gate('provider', 'domain', 'cap', 'profile', 1, 0,
                          settlement_scope='scoped-nav2-native-outlet-and-next-context')
        self.addCleanup(gate.close)
        for kind in ('idle', 'worker', 'sink'):
            self.assertEqual(gate.startup(kind, True, 1), 'accepted')
        self.assertEqual(gate.capabilities(True, 2, 100), 'accepted')
        operation = gate.admit('first', 1, None, 3)['operation_id']
        self.assertTrue(gate.dispatch(operation, 4))
        self.assertEqual(gate.native(operation, 'accepted', 5, 'nav-goal'), 'accepted')
        self.assertEqual(gate.native(operation, 'succeeded', 6, 'nav-goal'), 'accepted')
        self.assertEqual(gate.output(operation, 'accepted', 'first', 7), 'accepted')
        self.assertEqual(gate.receipt(operation)['settlement'], 'pending')
        self.assertEqual(gate.settle(operation, 8), 'accepted')
        self.assertEqual(gate.receipt(operation)['settlement'], 'settled')
        self.assertEqual(gate.admit('second', 1, None, 9)['status'], 'admitted')

    def test_scope_name_cannot_waive_unresolved_native_work(self):
        gate = _core.Gate('provider', 'domain', 'cap', 'profile', 1, 0,
                          settlement_scope='scoped-nav2-native-outlet-and-next-context')
        self.addCleanup(gate.close)
        for kind in ('idle', 'worker', 'sink'):
            gate.startup(kind, True, 1)
        gate.capabilities(True, 2, 100)
        operation = gate.admit('first', 1, None, 3)['operation_id']
        self.assertTrue(gate.dispatch(operation, 4))
        self.assertEqual(gate.settle(operation, 5), 'rejected')
        self.assertEqual(gate.receipt(operation)['settlement'], 'pending')

    def test_invalid_scope_is_refused_at_configuration(self):
        for scope in ('', 'x'*1025, 'wrong\0scope', None, True):
            with self.subTest(scope_type=type(scope).__name__), self.assertRaises((TypeError, ValueError)):
                _core.Gate('provider', 'domain', 'cap', 'profile', 1, 0,
                           settlement_scope=scope)


if __name__ == '__main__':
    unittest.main()
