"""Bounded caller think-time compatibility; real CLI control flow, no physics."""
import sys
import unittest
from types import ModuleType, SimpleNamespace
from unittest.mock import patch
from robot_harness_nav2 import __main__ as entry


class ClientClosed(Exception):
    pass


class BudgetTests(unittest.TestCase):
    def exercise(self, options=(), final_delay=0):
        calls = []
        class Owner:
            def __init__(self):
                self.execution = SimpleNamespace(active=True)
                self.requests = SimpleNamespace(closing=True)
            def ready(self): pass
            def prepare_b_initial(self): pass
            def open_scope(self, stage): pass
            def visit(self, stage): pass
            def close_visit(self, stage): pass
            def visit_and_close(self, stage):
                self.visit(stage)
                self.close_visit(stage)
            def abort(self): calls.append('abort')
            def finish_terminal_queries(self):
                calls.append(('terminal', self.terminal_window.seconds))
            def wait(self, predicate, seconds, **kwargs):
                calls.append((seconds, kwargs.get('require_fresh', False)))
                if len([c for c in calls if isinstance(c, tuple)]) == 3 and final_delay > seconds:
                    raise TimeoutError('native wait budget expired')
        ros = ModuleType('rclpy')
        ros.init = lambda: None
        ros.shutdown = lambda: None
        owner = ModuleType('robot_harness_nav2._owner')
        owner.NavigationOwner, owner.ClientClosed = Owner, ClientClosed
        revision = ModuleType('robot_harness_nav2._revision')
        class RevisionOwner(Owner):
            def __init__(self):
                super().__init__()
                calls.append('revision-profile')
        revision.RevisionOwner = RevisionOwner
        failure = ModuleType('robot_harness_nav2._failure')
        class FailureOwner(Owner):
            def __init__(self):
                super().__init__()
                calls.append('failure-profile')
        failure.FailureOwner = FailureOwner
        observations = ModuleType('robot_harness_nav2._observations')
        observations.emit = lambda *a, **kw: None
        observations.diagnose = lambda *a, **kw: None
        with patch.dict(sys.modules, {'rclpy': ros, owner.__name__: owner,
                                      revision.__name__: revision,
                                      failure.__name__: failure,
                                      observations.__name__: observations}), \
                patch.object(sys, 'argv', ['owner', *options]), \
                patch.object(entry, 'require_isolation'):
            code = entry.main()
        return code, calls

    def test_defaults_keep_15_second_admission_and_10_second_close(self):
        code, calls = self.exercise()
        self.assertEqual(code, 0)
        self.assertEqual(calls, [(15, True), (15, True), (10, False), 'abort'])

    def test_revision_profile_is_explicit_and_keeps_idle_budget(self):
        code, calls = self.exercise(['--profile', 'scoped-two-context-nav2-revision-v1',
                                     '--caller-wait-seconds', '45'])
        self.assertEqual(code, 0)
        self.assertEqual(calls, ['revision-profile', (45, True), (45, True), (45, False), 'abort'])

    def test_failure_profile_is_explicit_and_keeps_idle_budget(self):
        code, calls = self.exercise(['--profile', 'scoped-two-context-nav2-failure-recovery-v1',
                                     '--caller-wait-seconds', '45'])
        self.assertEqual(code, 0)
        self.assertEqual(calls, ['failure-profile', (45, True), (45, True), (45, False), 'abort'])

    def test_final_proposal_beyond_old_wait_requires_explicit_budget(self):
        code, calls = self.exercise(final_delay=12)
        self.assertEqual(code, 1)
        self.assertEqual(calls[-1], 'abort')
        code, calls = self.exercise(['--caller-wait-seconds', '45'], final_delay=12)
        self.assertEqual(code, 0)
        self.assertEqual(calls, [(45, True), (45, True), (45, False), 'abort'])

    def test_idle_budget_still_expires_and_aborts(self):
        code, calls = self.exercise(['--caller-wait-seconds', '45'], final_delay=46)
        self.assertEqual(code, 1)
        self.assertEqual(calls[-1], 'abort')

    def test_terminal_window_is_explicit_and_keeps_existing_idle_waits(self):
        code, calls = self.exercise(['--terminal-query-seconds', '8'])
        self.assertEqual(code, 0)
        self.assertEqual(calls, [(15, True), (15, True), (10, False), 'abort', ('terminal', 8)])

    def test_invalid_terminal_settings_rejected_before_isolation(self):
        for options in (['--terminal-query-seconds', value] for value in
                        ('0', '-1', '11', 'nan', 'inf', 'bad')):
            with self.subTest(options=options), patch.object(sys, 'argv', ['owner', *options]), \
                    patch.object(entry, 'require_isolation') as isolation, patch('sys.stderr'):
                with self.assertRaises(SystemExit):
                    entry.main()
                isolation.assert_not_called()
        for profile in ('scoped-two-context-nav2-revision-v1',
                        'scoped-two-context-nav2-failure-recovery-v1',
                        'scoped-two-context-nav2-owner-permit-v1'):
            with self.subTest(profile=profile), patch.object(sys, 'argv',
                    ['owner', '--profile', profile, '--terminal-query-seconds', '8']), \
                    patch.object(entry, 'require_isolation') as isolation, patch('sys.stderr'):
                with self.assertRaises(SystemExit):
                    entry.main()
                isolation.assert_not_called()

    def test_invalid_budget_rejected_before_ros_or_isolation(self):
        for value in ('0', '-1', '61', 'nan', 'inf', 'not-a-number'):
            with self.subTest(value=value), patch.object(sys, 'argv', ['owner', '--caller-wait-seconds', value]), \
                    patch.object(entry, 'require_isolation') as isolation, \
                    patch('sys.stderr'):
                with self.assertRaises(SystemExit) as error:
                    entry.main()
                self.assertEqual(error.exception.code, 2)
                isolation.assert_not_called()
