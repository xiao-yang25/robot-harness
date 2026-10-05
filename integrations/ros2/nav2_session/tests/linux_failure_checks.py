"""Real Core/endpoint, controlled ROS edges for explicit failed-A closure."""
import copy
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import linux_revision_checks as revision_checks
from robot_harness_nav2 import _failure as failure
from robot_harness_nav2 import _owner as driver
from robot_harness_nav2._profile import FAILURE_PROFILE_ID
from robot_harness_nav2._scoped import ScopedDriver


class FailureChecks(unittest.TestCase):
    def setUp(self):
        fixture = self.fixture = revision_checks.RevisionChecks()
        fixture.owner_type = failure.FailureOwner
        fixture.profile_id = FAILURE_PROFILE_ID
        fixture.make_owner()
        self.addCleanup(fixture.doCleanups)
        self.owner = fixture.owner
        self.owner.failure_closing = False
        self.owner.close_until = None
        fixture.status = 6
        fixture.pending['wrapped'].get_result_async().result()
        self.addCleanup(patch.stopall)
        patch.object(failure, 'emit').start()

    def assert_unreleased(self):
        f = self.fixture
        r = f.owner.execution.snapshot(f.a)['receipt']
        self.assertEqual(r['settlement'], 'pending')
        self.assertNotEqual(r['authority_disposition'], 'released')
        b = f.owner.execution.reserve('B', ('B',), lambda: dict(stage='B'))[0]
        with self.assertRaisesRegex(ValueError, 'released before admission'):
            f.owner.execution.admit(b, 1, time.monotonic_ns()+100_000_000_000, time.monotonic_ns())

    def test_complete_failed_no_output_release_allows_b_without_sending_it(self):
        self.owner.finish_failure()
        f = self.fixture
        r = self.owner.execution.snapshot(f.a)['receipt']
        self.assertEqual((r['native_outcome'], r['output_non_delivery_reason'], r['settlement'], r['authority_disposition']),
                         ('failed', 'no_output', 'settled', 'released'))
        self.assertIsNone(f.a['result'])
        self.assertEqual(self.owner.stage_index, 1)
        self.assertEqual(f.cancel_calls, [])
        self.assertEqual(f.edges[0], 'lane-and-quiet')
        self.assertEqual(len(f.edges), 7)
        b = self.owner.execution.reserve('B', ('B',), lambda: dict(stage='B'))[0]
        self.assertEqual(self.owner.execution.admit(b, 1, time.monotonic_ns()+100_000_000_000,
                                                  time.monotonic_ns())['status'], 'admitted')

    def test_only_a_status6_selects_failure_branch(self):
        with self.assertRaises(failure.FailedVisit): self.owner.validate_result('A', SimpleNamespace(status=6))
        for stage, status in (('B', 6), ('A', 5), ('A', 0)):
            with self.assertRaises(RuntimeError) as caught: self.owner.validate_result(stage, SimpleNamespace(status=status))
            self.assertNotIsInstance(caught.exception, failure.FailedVisit)
        self.owner.validate_result('A', SimpleNamespace(status=4))

    def test_unconfirmed_or_foreign_terminal_cannot_close(self):
        self.fixture.pending['terminal'] = False
        with self.assertRaisesRegex(RuntimeError, 'associated A'): self.owner.finish_failure()
        self.assert_unreleased()
        self.assertEqual(self.fixture.edges, [])

    def test_missing_lane_proof_or_quiet_blocks_core_disposal(self):
        self.fixture.lane_close.side_effect = TimeoutError('missing lane proof/quiet')
        with self.assertRaises(TimeoutError): self.owner.finish_failure()
        self.assert_unreleased()
        self.assertEqual(self.owner.execution.snapshot(self.fixture.a)['receipt']['output'], 'pending')
        until = self.owner.close_until
        with self.assertRaisesRegex(RuntimeError, 'already attempted'): self.owner.finish_failure()
        self.assertEqual(self.owner.close_until, until)

    def test_global_close_eof_and_cancel_keep_existing_stop_path(self):
        caller = self.fixture.connect()
        caller.close()
        with self.assertRaises(driver.ClientClosed): self.owner.finish_failure()
        self.assert_unreleased()
        self.assertEqual(self.fixture.edges, [])

    def test_explicit_close_wins_before_recovery(self):
        self.owner.requests.close()
        with self.assertRaises(driver.ClientClosed): self.owner.finish_failure()
        self.assert_unreleased()

    def test_cancelled_authority_does_not_become_failure_reuse(self):
        self.owner.requests.cancel('A')
        with self.assertRaises(driver.ClientClosed): self.owner.finish_failure()
        self.assert_unreleased()
        self.assertEqual(self.fixture.edges, [])

    def test_post_readiness_logging_loss_blocks_settlement(self):
        def log(event, **kwargs):
            if event == 'core_next_ready': self.owner.valid_observation = lambda: False
        with patch.object(driver, 'emit', side_effect=log):
            with self.assertRaises(RuntimeError): self.owner.finish_failure()
        self.assert_unreleased()

    def test_original_operation_and_task_budget_are_not_refreshed(self):
        self.owner.task_until = time.monotonic()+.5
        self.owner.finish_failure()
        self.assertLessEqual(self.owner.close_until, self.owner.task_until)

    def test_original_expiry_prevents_failure_closure(self):
        deadline = self.owner.execution.snapshot(self.fixture.a)['receipt']['deadline']
        self.owner.execution.clock = lambda: deadline+1
        with self.assertRaises(RuntimeError): self.owner.finish_failure()
        self.assert_unreleased()
        self.assertEqual(self.fixture.edges, [])

    def test_actual_failure_close_lane_rejects_unknown_or_unsealed_controller(self):
        context = self.owner.contexts['A']
        context['namespace'] = ''
        context['children'] = {'compute_path_to_pose': dict(link=dict(child_uuid='b'*32),ack=dict(goal_uuid='b'*32)),
                               'follow_path': {}}
        self.owner.failure_closing = True
        self.owner.close_until = time.monotonic()+15
        fact = dict(event='navigation_bt_closed',scope_id=context['scope_id'],bt_worker_joined=True,
            endpoint_sealed=True,tree_idle=True,leaf_proof_complete=True,
            children=[dict(action='compute_path_to_pose',scope_id=context['scope_id'],kind='terminal',child_uuid='b'*32,terminal='failed'),
                      dict(action='follow_path',scope_id=context['scope_id'],kind='not_submitted',child_uuid='',terminal='')])
        calls = []
        fault = [None]
        def service(kind, name, request, budget=5, **kwargs):
            calls.append(name)
            row = copy.deepcopy(fact)
            row['steady_ns'] = time.monotonic_ns()
            if name == '/close_scoped_motion': context['drive_ack'] = {}
            if name == '/seal_unsubmitted_controller':
                row = dict(event='controller_unsubmitted_sealed',scope_id=context['scope_id'],generation=1,
                           worker_callbacks=0,server_idle=True,endpoint_sealed=fault[0]!='seal',steady_ns=time.monotonic_ns())
            elif fault[0] == 'unknown': row['children'][1]['kind']='unknown'
            import json
            return SimpleNamespace(done=lambda: True,result=lambda: SimpleNamespace(accepted=True,success=True,
                                   message=json.dumps(row),current_state=SimpleNamespace(id=2)))
        self.owner.service = service
        self.owner.close_lane('A',goal_id=self.fixture.goal)
        self.assertEqual(calls, ['/close_scoped_motion','/close_navigation_scope','/seal_unsubmitted_controller',
                                '/lifecycle_manager_navigation/manage_nodes','/velocity_smoother/get_state'])
        for value in ('unknown', 'seal'):
            fault[0] = value
            with self.assertRaises(ValueError): self.owner.close_lane('A',goal_id=self.fixture.goal)
        self.assert_unreleased()


if __name__ == '__main__': unittest.main()
