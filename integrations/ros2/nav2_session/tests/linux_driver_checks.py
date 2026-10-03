"""Actual driver methods with real Core, synthetic ROS edges; isolated Linux only."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import socket
from types import SimpleNamespace, MethodType
import unittest
from unittest.mock import patch

from robot_harness import _core
from robot_harness._execution import ExecutionCoordinator
from robot_harness._transport import Channel
from robot_harness.navigation import NavigationRequests
from robot_harness_nav2._requests import RequestEndpoint
from robot_harness_nav2._native_lane import ObservedClient

from robot_harness_nav2 import _owner as driver
from robot_harness_nav2 import _scoped as scoped


class DriverChecks(unittest.TestCase):
    def test_preparation_response_timeout_retains_service_phase_and_original_budget(self):
        owner = scoped.ScopedDriver.__new__(scoped.ScopedDriver)
        owner.context_process = SimpleNamespace(poll=lambda: None)
        future = SimpleNamespace(done=lambda: False)
        failure = TimeoutError('native wait budget expired')
        owner.service = unittest.mock.Mock(return_value=future)
        owner.wait = unittest.mock.Mock(side_effect=failure)
        with patch.object(scoped.time, 'monotonic', return_value=10), patch.object(scoped, 'diagnose') as diagnostic:
            with self.assertRaises(TimeoutError) as caught:
                owner.prepare_service('GetState', '/b/controller_server/get_state', 'request', 12)
        self.assertIs(caught.exception, failure)
        owner.service.assert_called_once_with('GetState', '/b/controller_server/get_state', 'request', 2, fresh=False)
        owner.wait.assert_called_once_with(future.done, 2)
        fields = diagnostic.call_args.kwargs
        self.assertEqual(fields['service'], '/b/controller_server/get_state')
        self.assertEqual(fields['phase'], 'response')
        self.assertEqual(fields['budget_seconds'], 2)
        self.assertEqual(fields['error'], 'TimeoutError: native wait budget expired')
        self.assertIsNone(fields['context_returncode'])

    def test_preparation_discovery_failure_does_not_wait_or_mask_original_error(self):
        owner = scoped.ScopedDriver.__new__(scoped.ScopedDriver)
        owner.context_process = SimpleNamespace(poll=lambda: 7)
        failure = TimeoutError('native wait budget expired')
        owner.service = unittest.mock.Mock(side_effect=failure)
        owner.wait = unittest.mock.Mock()
        with patch.object(scoped.time, 'monotonic', return_value=10), patch.object(scoped, 'diagnose') as diagnostic:
            with self.assertRaises(TimeoutError) as caught:
                owner.prepare_service('GetState', '/b/bt_navigator/get_state', 'request', 80)
        self.assertIs(caught.exception, failure)
        owner.service.assert_called_once_with('GetState', '/b/bt_navigator/get_state', 'request', 5, fresh=False)
        owner.wait.assert_not_called()
        self.assertEqual(diagnostic.call_args.kwargs['phase'], 'discovery')
        self.assertEqual(diagnostic.call_args.kwargs['context_returncode'], 7)

    def setUp(self):
        self.stamp = 3
        self.gate = _core.Gate('p', 'd', 'cap', 'nav', 1, 0)
        self.addCleanup(self.gate.close)
        for kind in ('idle', 'worker', 'sink'):
            self.gate.startup(kind, True, 1)
        self.gate.capabilities(True, 2, 100)
        self.execution = ExecutionCoordinator(self.gate, lambda: self.stamp)
        record = self.execution.reserve('A', ('A',), lambda: {'stage': 'A'})[0]
        self.execution.admit(record, 1, 50, 3)
        self.execution.dispatch(record)
        self.owner = driver.NavigationOwner.__new__(driver.NavigationOwner)
        self.owner.gate = self.gate
        self.owner.execution = self.execution
        self.owner.pending = []
        self.owner.aborting = False
        self.owner.valid_observation = lambda: True
        self.goal = 'a'*32
        self.uuid = SimpleNamespace(uuid=list(bytes.fromhex(self.goal)))

    def test_expiry_during_actual_driver_reservation_blocks_native_send(self):
        def delay_record(*args, **kwargs):
            self.stamp = 60
        transmitted = []
        client = ObservedClient(SimpleNamespace(send_goal_async=lambda *a, **k: transmitted.append(True)), self.owner)
        with patch.object(driver, 'emit', side_effect=delay_record), patch.object(
                driver.time, 'monotonic_ns', side_effect=lambda: self.stamp):
            with self.assertRaisesRegex(RuntimeError, 'authority'):
                client.send_goal_async(None, feedback_callback=lambda _: None, goal_uuid=self.uuid)
        self.assertEqual(transmitted, [])
        self.assertEqual(self.gate.receipt(1)['expiry_observed_at'], 60)
        self.assertEqual(len(self.owner.pending), 1)

    def test_cancel_transport_error_does_not_block_actual_outlet_request(self):
        calls = []
        def broken_cancel():
            calls.append('cancel')
            raise RuntimeError('native cancel transport failed')
        def close(request):
            calls.append('close')
            return SimpleNamespace(done=lambda: False)
        self.owner.pending = [dict(record=self.execution.active, goal_id=self.goal, terminal=False,
            handle=SimpleNamespace(accepted=True, goal_id=self.uuid, cancel_goal_async=broken_cancel))]
        self.owner.current = dict(scope_id='b'*32, generation=1)
        self.owner.scoped_clients = {'/close_scoped_motion': SimpleNamespace(
            service_is_ready=lambda: True, call_async=close)}
        with patch.object(driver, 'diagnose'), patch.object(driver.time, 'monotonic', side_effect=[0, 3]):
            self.owner.abort()
        self.assertEqual(calls, ['cancel', 'close'])
        self.assertEqual(self.gate.receipt(1)['authority_disposition'], 'revoked')
        self.assertEqual(self.gate.receipt(1)['settlement'], 'pending')

    def test_input_expiry_during_readiness_record_blocks_actual_settlement(self):
        self.execution.active['goal_id'] = self.goal
        self.execution.native(self.execution.active, 'accepted', self.goal)
        self.execution.native(self.execution.active, 'succeeded', self.goal)
        fresh = [True]
        self.owner.valid_observation = lambda: fresh[0]
        self.owner.observation = lambda: {'clock': 1}
        self.owner.contexts = {'B': {'namespace': '/b'}}
        self.owner.context_process = SimpleNamespace(poll=lambda: None)
        self.owner.b_client = SimpleNamespace(server_is_ready=lambda: True)
        response = SimpleNamespace(current_state=SimpleNamespace(id=3), success=True)
        self.owner.service = lambda *a, **k: SimpleNamespace(done=lambda: True, result=lambda: response)
        self.owner.wait = lambda predicate, *a, **k: self.assertTrue(predicate())
        def delayed_record(event, **kwargs):
            if event == 'core_next_ready':
                fresh[0] = False
        with patch.object(driver.ScopedDriver, 'close_visit'), patch.object(driver, 'emit', side_effect=delayed_record):
            with self.assertRaisesRegex(RuntimeError, 'before settlement'):
                self.owner.close_visit('A')
        receipt = self.gate.receipt(1)
        self.assertEqual(receipt['output'], 'pending')
        self.assertEqual(receipt['settlement'], 'pending')
        self.assertIsNotNone(self.execution.active)



    def test_outlet_error_still_cancels_late_actual_acceptance(self):
        available = [False]
        calls = []
        actual_handle = SimpleNamespace(accepted=True, goal_id=self.uuid,
            cancel_goal_async=lambda: calls.append(self.goal))
        from robot_harness_nav2._native_lane import ObservedFuture
        underlying = SimpleNamespace(done=lambda: available[0], result=lambda: actual_handle)
        pending = dict(record=self.execution.active, goal_id=self.goal, terminal=False)
        client = ObservedClient(None, self.owner)
        pending['future'] = ObservedFuture(underlying, lambda handle: client.acceptance(pending, handle))
        self.owner.pending = [pending]
        self.owner.current = dict(scope_id='b'*32, generation=1)
        def failed_outlet(request):
            raise RuntimeError('outlet unavailable')
        self.owner.scoped_clients = {'/close_scoped_motion': SimpleNamespace(
            service_is_ready=lambda: True, call_async=failed_outlet)}
        with patch.object(driver, 'diagnose'), patch.object(driver.time, 'monotonic',
                side_effect=[0, 0, 3]), patch.object(driver.rclpy, 'spin_once',
                side_effect=lambda *a, **k: available.__setitem__(0, True)):
            self.owner.abort()
        self.assertIs(pending['handle'], actual_handle)
        self.assertEqual(calls, [self.goal])
        receipt = self.gate.receipt(1)
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertEqual(receipt['native_identity'], self.goal)
        self.assertEqual(receipt['settlement'], 'pending')

    def test_close_without_active_task_interrupts_before_wait_timeout(self):
        # Same path covers before A and after settled/released A, before B.
        for stage in (0, 1):
            self.owner.stage_index = stage
            self.owner.requests = SimpleNamespace(closing=True)
            self.owner.endpoint = None
            self.owner.stop_requested = False
            self.owner.completed = False
            self.owner.execution = None
            with patch.object(driver.ScopedDriver, 'tick'):
                with self.assertRaises(driver.ClientClosed):
                    self.owner.tick()

    def request_connection(self):
        self.owner.stop_requested = self.owner.completed = False
        self.owner.requests = NavigationRequests(self.execution, sites={'A': (1, 2, 0)},
            profile='nav', map_id='m', observation=lambda: dict(epoch=0, map_id='m', frame='map', valid=True),
            ready=lambda: True, context=lambda: {}, request_stop=self.owner.request_stop)
        self.owner.endpoint = RequestEndpoint(self.owner.requests)
        self.addCleanup(self.owner.endpoint.dispose)
        parent, peer = socket.socketpair()
        self.owner.endpoint.listener.close()
        self.owner.endpoint.listener = None
        self.owner.endpoint.peer = Channel(parent)
        caller = Channel(peer)
        self.addCleanup(caller.close)
        return caller

    def assert_requested_cleanup(self):
        calls = []
        self.owner.pending = [dict(record=self.execution.active, goal_id=self.goal, terminal=False,
            handle=SimpleNamespace(accepted=True, goal_id=self.uuid,
                cancel_goal_async=lambda: calls.append(self.goal)))]
        self.owner.current = dict(scope_id='b'*32, generation=1)
        def outlet(request):
            calls.append((request.scope_id, request.generation))
            return SimpleNamespace(done=lambda: False)
        self.owner.scoped_clients = {'/close_scoped_motion': SimpleNamespace(
            service_is_ready=lambda: True, call_async=outlet)}
        self.assertTrue(self.owner.requests.closing)
        self.assertTrue(self.owner.stop_requested)
        self.assertEqual(self.gate.receipt(1)['authority_disposition'], 'revoked')
        with patch.object(driver, 'diagnose'), patch.object(driver.time, 'monotonic', side_effect=[0, 3]):
            self.owner.abort()
        self.assertEqual(calls, [self.goal, ('b'*32, 1)])
        self.assertEqual(self.gate.receipt(1)['settlement'], 'pending')

    def test_actual_endpoint_eof_revokes_core_and_schedules_exact_cleanup(self):
        caller = self.request_connection()
        caller.close()
        self.owner.endpoint.pump()
        self.assert_requested_cleanup()

    def test_actual_endpoint_close_reply_is_unknown_then_exact_cleanup(self):
        caller = self.request_connection()
        caller.queue(dict(rpc=1, command='close'))
        caller.pump()
        self.owner.endpoint.pump()
        replies = caller.pump()
        self.assertEqual(len(replies), 1)
        self.assertEqual(replies[0]['result']['closure'], 'unknown')
        self.assertFalse(replies[0]['result']['owner_reaped'])
        self.assert_requested_cleanup()

    def completed_connection(self):
        caller = self.request_connection()
        record = self.execution.active
        record['goal_id'] = self.goal
        self.execution.native(record, 'accepted', self.goal)
        self.execution.native(record, 'succeeded', self.goal)
        self.execution.dispose_result(record, {'goal_id': self.goal, 'stage': 'A'}, lambda: {})
        # A generic terminal record represents the end-of-sequence condition;
        # the actual two-site profile reaches this condition after native B.
        self.owner.completed = True
        return caller

    def test_completed_native_cancel_uses_client_shutdown_and_preserves_result(self):
        caller = self.completed_connection()
        caller.queue(dict(rpc=1, command='cancel', request_id='A'))
        caller.pump()
        with patch.object(driver.ScopedDriver, 'tick'):
            with self.assertRaises(driver.ClientClosed):
                self.owner.tick()
        response = caller.pump()[0]['result']
        self.assertTrue(response['affected_current'])
        self.assertFalse(self.owner.requests.closing)
        receipt = response['record']['receipt']
        self.assertEqual(receipt['authority_disposition'], 'revoked')
        self.assertEqual(receipt['native_outcome'], 'succeeded')
        self.assertEqual(receipt['output'], 'accepted')
        self.assertEqual(receipt['settlement'], 'pending')
        self.assertEqual(response['record']['result']['goal_id'], self.goal)

    def test_completed_native_close_keeps_the_normal_terminal_wait_path(self):
        caller = self.completed_connection()
        caller.queue(dict(rpc=1, command='close'))
        caller.pump()
        with patch.object(driver.ScopedDriver, 'tick'):
            self.owner.tick()
        response = caller.pump()[0]['result']
        self.assertEqual(response['closure'], 'unknown')
        self.assertTrue(self.owner.requests.closing)
        self.assertEqual(self.gate.receipt(1)['authority_disposition'], 'revoked')
        self.assertEqual(self.gate.receipt(1)['output'], 'accepted')
        self.assertEqual(self.gate.receipt(1)['settlement'], 'pending')

    def test_partial_owner_disposal_reaps_process_even_if_endpoint_fails(self):
        from robot_harness_nav2.__main__ import dispose
        calls = []
        def failed_endpoint():
            calls.append('endpoint')
            raise OSError('endpoint cleanup')
        process = SimpleNamespace(poll=lambda: None,
            terminate=lambda: calls.append('terminate'),
            wait=lambda **kw: calls.append('wait'))
        owner = SimpleNamespace(endpoint=SimpleNamespace(dispose=failed_endpoint),
            context_process=process, context_log=SimpleNamespace(close=lambda: calls.append('log')),
            gate=SimpleNamespace(close=lambda: calls.append('gate')))
        self.assertEqual(dispose(owner), ['endpoint cleanup'])
        self.assertEqual(calls, ['endpoint', 'terminate', 'wait', 'log', 'gate'])

    def test_b_preparation_failure_reaps_created_os_process(self):
        from robot_harness_nav2 import _scoped
        from robot_harness_nav2.__main__ import dispose
        with tempfile.TemporaryDirectory() as directory:
            child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
            self.addCleanup(lambda: child.poll() is None and child.kill())
            partial = SimpleNamespace(motion_started=False, b_client=None,
                contexts={'B': dict(scope_id='b'*32, namespace='/b')},
                observation=lambda: {}, context_process=None, context_log=None)
            def failed_service(*a, **k):
                raise RuntimeError('B readiness service failed')
            partial.service = failed_service
            partial.prepare_service = MethodType(_scoped.ScopedDriver.prepare_service, partial)
            with patch.object(_scoped, 'OUT', Path(directory)), patch.object(_scoped, 'emit'), patch.object(
                    _scoped.subprocess, 'Popen', return_value=child), patch.object(_scoped, 'ActionClient'):
                with self.assertRaisesRegex(RuntimeError, 'B readiness'):
                    _scoped.ScopedDriver.prepare_b_initial(partial)
            self.assertIsNone(child.poll())
            self.assertEqual(dispose(partial), [])
            self.assertIsNotNone(child.poll())
            self.assertTrue(partial.context_log.closed)

    def test_logging_failure_after_submission_does_not_skip_abort(self):
        from robot_harness_nav2 import __main__ as entry
        from robot_harness_nav2 import _observations
        calls = []
        class FailedOwner:
            def ready(self):
                raise OSError('caller log unavailable after send')
            def abort(self):
                calls.append('abort')
        # Same exception handler as a real send/log failure; no ROS node fake
        # is treated as evidence of actual physical cancellation.
        with patch.object(entry, 'require_isolation'), patch.object(driver, 'NavigationOwner', FailedOwner), patch.object(
                _observations, 'emit', side_effect=OSError('log failed')), patch.object(
                driver.rclpy, 'init'), patch.object(driver.rclpy, 'shutdown'), patch.object(entry, 'dispose', return_value=[]), patch(
                'sys.argv', ['owner']):
            self.assertEqual(entry.main(), 1)
        self.assertEqual(calls, ['abort'])


if __name__ == '__main__':
    unittest.main()
