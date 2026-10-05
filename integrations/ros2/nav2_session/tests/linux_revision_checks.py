"""Real Core and endpoint; controlled ROS edges for opt-in cancellation reuse."""
import socket
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from robot_harness import _core
from robot_harness._execution import ExecutionCoordinator, accepted
from robot_harness._transport import Channel
from robot_harness.navigation import NavigationRequests
from robot_harness_nav2._native_lane import ObservedHandle, ObservedClient, ObservedFuture
from robot_harness_nav2._requests import RequestEndpoint
from robot_harness_nav2 import _owner as driver
from robot_harness_nav2 import _revision as revision
from robot_harness_nav2._profile import REVISION_PROFILE_ID


class RevisionChecks(unittest.TestCase):
    owner_type = revision.RevisionOwner
    profile_id = REVISION_PROFILE_ID
    def setUp(self):
        self.make_owner()

    def make_owner(self, native=True):
        now = time.monotonic_ns()
        gate = self.gate = _core.Gate('p', 'd', 'cap', self.profile_id, 1, now)
        self.addCleanup(gate.close)
        for kind in ('idle', 'worker', 'sink'):
            accepted(gate.startup(kind, True, time.monotonic_ns()))
        accepted(gate.capabilities(True, time.monotonic_ns(), time.monotonic_ns()+60_000_000_000))
        owner = self.owner = self.owner_type.__new__(self.owner_type)
        owner.gate = gate
        owner.execution = ExecutionCoordinator(gate, time.monotonic_ns)
        record = self.a = owner.execution.reserve('A', ('A',), lambda: dict(stage='A'))[0]
        self.assertEqual(owner.execution.admit(record, 1, now+147_000_000_000,
                                              time.monotonic_ns())['status'], 'admitted')
        self.assertTrue(owner.execution.dispatch(record))
        self.goal = record['goal_id'] = 'a'*32
        self.cancel_calls = []
        self.result_available = True
        self.status = 5
        self.handle = SimpleNamespace(accepted=True, goal_id=SimpleNamespace(uuid=bytes.fromhex(self.goal)),
            cancel_goal_async=lambda: self.cancel_calls.append(self.goal),
            get_result_async=lambda: SimpleNamespace(done=lambda: self.result_available,
                                                    result=lambda: SimpleNamespace(status=self.status)))
        self.pending = dict(record=record, goal_id=self.goal, terminal=False)
        if native:
            accepted(owner.execution.native(record, 'accepted', self.goal))
            self.pending.update(handle=self.handle,
                wrapped=ObservedHandle(self.handle, self.pending, owner.execution))
        owner.pending = [self.pending]
        owner.intent_record = owner.close_until = None
        owner.closing_visit = False
        owner.stage_index = 0
        owner.task_until = owner.deadline = time.monotonic()+240
        owner.stop_requested = owner.completed = owner.aborting = False
        owner.capability_at = time.monotonic_ns()+30_000_000_000
        owner.valid_observation = lambda: True
        owner.observation = lambda: dict(clock=1)
        owner.context_process = SimpleNamespace(poll=lambda: None)
        owner.b_client = SimpleNamespace(server_is_ready=lambda: True)
        owner.contexts = {'A': dict(scope_id='c'*32, generation=1), 'B': dict(namespace='/b')}
        owner.current = owner.contexts['A']
        owner.active = self.handle
        owner.endpoint = None
        owner.requests = NavigationRequests(owner.execution, sites={'A': (1, 2, 0), 'B': (0, 0, 0)},
            profile=self.profile_id, map_id='m',
            observation=lambda: dict(epoch=0, map_id='m', frame='map', valid=True),
            ready=lambda: True, context=lambda: {}, request_stop=owner.request_stop)
        self.edges = []
        owner.service = self.service
        owner.wait = self.wait
        self.lane_close = Mock(side_effect=lambda *a, **k: self.edges.append('lane-and-quiet'))
        for target in (patch.object(driver.ScopedDriver, 'tick'),
                       patch.object(driver.ScopedDriver, 'close_visit', self.lane_close),
                       patch.object(driver, 'emit'), patch.object(revision, 'emit'),
                       patch.object(driver, 'diagnose')):
            target.start()
            self.addCleanup(target.stop)

    def service(self, kind, name, request, budget=5, **kwargs):
        self.assertGreater(budget, 0)
        self.assertLessEqual(budget, 5)
        self.edges.append(name)
        if name == '/close_scoped_motion':
            self.assertEqual(self.cancel_calls, [self.goal])
            self.assertEqual((request.scope_id, request.generation), ('c'*32, 1))
        response = SimpleNamespace(accepted=True, success=True, current_state=SimpleNamespace(id=3))
        return SimpleNamespace(done=lambda: True, result=lambda: response)

    def wait(self, predicate, budget, **kwargs):
        self.owner.tick()
        if not predicate():
            raise TimeoutError('controlled unresolved native edge')

    def request(self):
        response = self.owner.requests.cancel('A')
        self.assertEqual(response['closure'], 'unknown')
        with self.assertRaises(revision.CancelledVisit):
            self.owner.tick()

    def connect(self):
        endpoint = self.owner.endpoint = RequestEndpoint(self.owner.requests)
        self.addCleanup(endpoint.dispose)
        parent, peer = socket.socketpair()
        endpoint.listener.close()
        endpoint.listener = None
        endpoint.peer = Channel(parent)
        caller = Channel(peer)
        self.addCleanup(caller.close)
        return caller

    def assert_pending(self):
        receipt = self.owner.execution.snapshot(self.a)['receipt']
        self.assertEqual(receipt['settlement'], 'pending')
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertIs(self.owner.execution.active, self.a)
        b = self.owner.execution.reserve('B', ('B',), lambda: dict(stage='B'))[0]
        with self.assertRaisesRegex(ValueError, 'released before admission'):
            self.owner.execution.admit(b, 1, time.monotonic_ns()+100_000_000_000, time.monotonic_ns())

    def test_endpoint_cancel_created_in_same_tick_then_real_core_release_and_b(self):
        caller = self.connect()
        caller.queue(dict(rpc=1, command='cancel', request_id='A'))
        caller.pump()
        with self.assertRaises(revision.CancelledVisit):
            self.owner.tick()
        self.assertFalse(self.owner.aborting)
        self.assertEqual(caller.pump()[0]['result']['closure'], 'unknown')
        self.owner.finish_cancel()
        receipt = self.owner.execution.snapshot(self.a)['receipt']
        self.assertEqual((receipt['native_outcome'], receipt['output_non_delivery_reason'],
                          receipt['settlement'], receipt['authority_disposition']),
                         ('cancelled', 'no_output', 'settled', 'released'))
        self.assertEqual(self.owner.stage_index, 1)
        self.assertEqual(self.edges, ['/close_scoped_motion', 'lane-and-quiet',
            '/b/bt_navigator/get_state', '/b/planner_server/get_state',
            '/b/controller_server/get_state', '/b/velocity_smoother/get_state',
            '/b/controller_transform_ready', '/b/planner_map_ready'])
        b = self.owner.execution.reserve('B', ('B',), lambda: dict(stage='B'))[0]
        self.assertEqual(self.owner.execution.admit(b, 1, time.monotonic_ns()+100_000_000_000,
                                                  time.monotonic_ns())['status'], 'admitted')
        self.assertTrue(self.owner.execution.dispatch(b))
        self.assertFalse(self.owner.execution.cancel(self.a))
        self.assertEqual(self.owner.execution.tick()['authority_disposition'], 'current')

    def test_native_success_racing_cancel_stays_succeeded_without_false_arrival(self):
        self.request()
        self.status = 4
        self.owner.finish_cancel()
        receipt = self.owner.execution.snapshot(self.a)['receipt']
        self.assertEqual(receipt['native_outcome'], 'succeeded')
        self.assertEqual(receipt['output_non_delivery_reason'], 'authority_revoked')
        self.assertIsNone(self.a['result'])
        self.assertEqual(receipt['authority_disposition'], 'released')

    def test_global_close_and_eof_override_in_progress_closure(self):
        for command in ('close', 'eof'):
            with self.subTest(command=command):
                if command == 'eof':
                    self.make_owner()
                caller = self.connect()
                self.request()
                if command == 'eof':
                    caller.close()
                else:
                    caller.queue(dict(rpc=1, command='close'))
                    caller.pump()
                with self.assertRaises(driver.ClientClosed):
                    self.owner.finish_cancel()
                self.assert_pending()
                self.assertEqual(self.edges, [])

    def test_missing_terminal_or_outlet_or_lane_quiet_blocks_release(self):
        for missing in ('terminal', 'outlet', 'lane-quiet'):
            with self.subTest(missing=missing):
                if missing != 'terminal':
                    self.make_owner()
                self.request()
                if missing == 'terminal':
                    self.result_available = False
                elif missing == 'outlet':
                    self.owner.service = Mock(side_effect=RuntimeError('outlet unavailable'))
                else:
                    self.lane_close.side_effect = TimeoutError('missing lane or quiet')
                with self.assertRaises((RuntimeError, TimeoutError)):
                    self.owner.finish_cancel()
                self.assert_pending()
                self.assertEqual(self.owner.execution.snapshot(self.a)['receipt']['output'], 'pending')

    def test_actual_readiness_checks_and_post_log_freshness_block_settlement(self):
        for fault in ('state', 'trigger', 'post-log-input', 'post-log-close'):
            with self.subTest(fault=fault):
                if fault != 'state':
                    self.make_owner()
                self.request()
                original = self.service
                def service(kind, name, request, budget=5, **kwargs):
                    future = original(kind, name, request, budget, **kwargs)
                    if fault == 'state' and name.endswith('bt_navigator/get_state'):
                        future.result().current_state.id = 2
                    if fault == 'trigger' and name.endswith('planner_map_ready'):
                        future.result().success = False
                    return future
                self.owner.service = service
                def logging(event, **kwargs):
                    if event == 'core_next_ready':
                        if fault == 'post-log-input':
                            self.owner.valid_observation = lambda: False
                        if fault == 'post-log-close':
                            self.owner.requests.close()
                with patch.object(driver, 'emit', side_effect=logging):
                    with self.assertRaises(RuntimeError):
                        self.owner.finish_cancel()
                self.assert_pending()

    def test_deadline_is_shared_clipped_and_not_extended_by_repeat_cancel(self):
        self.owner.task_until = time.monotonic()+.5
        self.request()
        until = self.owner.close_until
        self.assertLessEqual(until, self.owner.task_until)
        self.owner.requests.cancel('A')
        self.assertEqual(self.owner.close_until, until)
        self.owner.close_until = time.monotonic()-1
        with self.assertRaises(RuntimeError):
            self.owner.finish_cancel()
        self.assert_pending()
        self.assertEqual(self.edges, [])

    def test_unassociated_revocation_keeps_default_fault_path(self):
        self.owner.execution.cancel(self.a)
        with self.assertRaisesRegex(RuntimeError, 'authority'):
            self.owner.tick()
        self.assertTrue(self.owner.aborting)
        self.assertIsNone(self.owner.intent_record)
        self.assert_pending()

    def test_original_core_deadline_expiry_blocks_closure(self):
        self.request()
        deadline = self.owner.execution.snapshot(self.a)['receipt']['deadline']
        self.owner.execution.clock = lambda: deadline+1
        with self.assertRaises(RuntimeError):
            self.owner.finish_cancel()
        self.assert_pending()
        self.assertEqual(self.owner.execution.snapshot(self.a)['receipt']['expiry_observed_at'], deadline+1)
        self.assertEqual(self.edges, [])

    def test_eof_during_native_readiness_wait_blocks_output_and_release(self):
        caller = self.connect()
        self.request()
        original = self.service
        def service(kind, name, request, budget=5, **kwargs):
            future = original(kind, name, request, budget, **kwargs)
            if name.endswith('bt_navigator/get_state'):
                caller.close()
            return future
        self.owner.service = service
        with self.assertRaises(driver.ClientClosed):
            self.owner.finish_cancel()
        self.assert_pending()
        self.assertEqual(self.owner.execution.snapshot(self.a)['receipt']['output'], 'pending')

    def test_terminal_already_accepted_result_uses_abort_and_preserves_result(self):
        accepted(self.owner.execution.native(self.a, 'succeeded', self.goal))
        candidate = {'stage': 'A', 'goal_id': self.goal}
        self.owner.execution.dispose_result(self.a, candidate, lambda: {})
        self.owner.requests.cancel('A')
        with self.assertRaises(driver.ClientClosed):
            self.owner.tick()
        self.assertIsNone(self.owner.intent_record)
        self.assertEqual(self.a['result'], candidate)
        self.assertEqual(self.owner.execution.snapshot(self.a)['receipt']['output'], 'accepted')
        self.assert_pending()

    def test_late_acceptance_is_retained_and_cancelled_without_reuse(self):
        self.make_owner(native=False)
        available = [False]
        client = ObservedClient(None, self.owner)
        raw = SimpleNamespace(done=lambda: available[0], result=lambda: self.handle)
        self.pending['future'] = ObservedFuture(raw, lambda h: client.acceptance(self.pending, h))
        self.owner.requests.cancel('A')
        with self.assertRaises(driver.ClientClosed):
            self.owner.tick()
        self.owner.scoped_clients = {'/close_scoped_motion': SimpleNamespace(
            service_is_ready=lambda: True, call_async=lambda r: SimpleNamespace(done=lambda: False))}
        with patch.object(driver.time, 'monotonic', side_effect=[0, 0, 3]), \
                patch.object(driver.rclpy, 'spin_once', side_effect=lambda *a, **k: available.__setitem__(0, True)):
            self.owner.abort()
        self.assertIs(self.pending['handle'], self.handle)
        self.assertEqual(self.cancel_calls, [self.goal])
        self.assertIsNone(self.owner.intent_record)
        self.assert_pending()


if __name__ == '__main__':
    unittest.main()
