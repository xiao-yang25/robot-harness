"""Actual driver methods with real Core, synthetic ROS edges; isolated Linux only."""
import importlib.util
import json
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
    def test_b_startup_endpoint_precedes_lifecycle_queries_within_context_budget(self):
        owner = scoped.ScopedDriver.__new__(scoped.ScopedDriver)
        owner.motion_started = False
        owner.b_client = None
        owner.contexts = {'B': dict(scope_id='b'*32, namespace='/b')}
        owner.observation = lambda: {}
        owner.valid_observation = lambda: True
        calls = []
        process = SimpleNamespace(poll=lambda: None)
        endpoint = SimpleNamespace(server_is_ready=lambda: True)
        owner.wait = lambda predicate, budget: calls.append(('wait', budget, predicate()))
        def service(kind, name, request, until):
            calls.append(('service', name, until))
            return SimpleNamespace(current_state=SimpleNamespace(id=3), success=True)
        owner.prepare_service = service
        with tempfile.TemporaryDirectory() as directory, patch.object(scoped, 'OUT', Path(directory)), \
                patch.object(scoped, 'emit'), patch.object(scoped.subprocess, 'Popen', return_value=process), \
                patch.object(scoped, 'ActionClient', return_value=endpoint), \
                patch.object(scoped.time, 'monotonic', return_value=10):
            try:
                owner.prepare_b_initial()
            finally:
                owner.context_log.close()
        self.assertEqual(calls[0], ('wait', 70, True))
        self.assertTrue(calls[1][1].endswith('/bt_navigator/get_state'))
        self.assertTrue(all(c[2] == 80 for c in calls if c[0] == 'service'))

    def test_b_startup_process_exit_prevents_lifecycle_query_and_ready_event(self):
        owner = scoped.ScopedDriver.__new__(scoped.ScopedDriver)
        owner.motion_started = False
        owner.b_client = None
        owner.contexts = {'B': dict(scope_id='b'*32, namespace='/b')}
        owner.observation = lambda: {}
        owner.wait = lambda predicate, budget: self.assertTrue(predicate())
        owner.prepare_service = unittest.mock.Mock()
        with tempfile.TemporaryDirectory() as directory, patch.object(scoped, 'OUT', Path(directory)), \
                patch.object(scoped, 'emit') as events, \
                patch.object(scoped.subprocess, 'Popen', return_value=SimpleNamespace(poll=lambda: 17)), \
                patch.object(scoped, 'ActionClient'):
            try:
                with self.assertRaisesRegex(RuntimeError, 'exited before startup'):
                    owner.prepare_b_initial()
            finally:
                owner.context_log.close()
        owner.prepare_service.assert_not_called()
        self.assertEqual([c.args[0] for c in events.call_args_list], ['context_prepare_requested'])

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
            partial.wait = lambda *a, **k: None
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


class PermissionOwnerChecks(unittest.TestCase):
    def setUp(self):
        from robot_harness_nav2 import _permission
        self.permission = _permission
        self.owner = _permission.PermissionOwner.__new__(_permission.PermissionOwner)
        self.context = dict(scope_id='a'*32, generation=1, operation_id=7,
                            initial_permission_deadline_ns=1000, last_permission_deadline_ns=1200)
        self.owner.contexts = {'A': self.context}
        self.owner.permission_fault = None
        self.fact = dict(event='motion_scope_applied', scope_id='a'*32, generation=1,
                         operation_id=7, sealed=True, wheel_targets_zero=True,
                         update_sequence=5, sim_ns=200, steady_ns=1300,
                         permission_deadline_ns=1200, closure_reason='permission_expired')

    def ack(self, **changes):
        with patch.object(self.permission, 'now_ns', return_value=1400), patch.object(self.permission, 'emit'):
            self.owner.drive_ack(SimpleNamespace(data=json.dumps(dict(self.fact, **changes))))

    def test_unsolicited_ack_enters_existing_abort_and_keeps_settlement_pending(self):
        # Actual live Owner methods / real Core; only ROS spin and edge clients
        # are synthetic. No close request is required for the autonomous fact.
        self.assertNotIn('close_requested_ns', self.context)
        self.ack()
        gate = _core.Gate('p', 'd', 'cap', self.owner.profile, 1, 0)
        self.addCleanup(gate.close)
        for kind in ('idle', 'worker', 'sink'):
            gate.startup(kind, True, 1)
        gate.capabilities(True, 2, 100)
        execution = ExecutionCoordinator(gate, lambda: 3)
        record = execution.reserve('A', ('A',), lambda: {'stage': 'A'})[0]
        execution.admit(record, 1, 50, 3)
        execution.dispatch(record)
        self.owner.execution = execution
        self.owner.gate = gate
        self.owner.endpoint = self.owner.requests = None
        self.owner.stop_requested = self.owner.completed = self.owner.aborting = False
        self.owner.context_process = SimpleNamespace(poll=lambda: None)
        self.owner.valid_observation = lambda: True
        self.owner.capability_at = 100
        self.owner.pending = []
        self.owner.current = self.context
        close = unittest.mock.Mock(return_value=SimpleNamespace(done=lambda: False))
        self.owner.scoped_clients = {'/close_scoped_motion': SimpleNamespace(
            service_is_ready=lambda: True, call_async=close)}
        with patch.object(driver.ScopedDriver, 'tick'), patch.object(driver.time, 'monotonic_ns', return_value=3):
            with self.assertRaisesRegex(RuntimeError, 'permission fault'):
                self.owner.tick()
        with patch.object(driver, 'diagnose'), patch.object(driver.time, 'monotonic', side_effect=[0, 3]):
            self.owner.abort()
        close.assert_called_once()
        self.assertEqual(gate.receipt(1)['authority_disposition'], 'revoked')
        self.assertEqual(gate.receipt(1)['settlement'], 'pending')
        self.assertEqual(gate.receipt(1)['output'], 'pending')
        self.assertNotIn('drive_ack', self.context)

    def test_fault_fact_rejects_wrong_identity_premature_time_and_bool_integer(self):
        for changes in (dict(operation_id=8), dict(generation=True),
                        dict(steady_ns=1199), dict(sim_ns=0), dict(wheel_targets_zero=False)):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.ack(**changes)
        self.assertIsNone(self.owner.permission_fault)
        self.ack()
        self.ack()  # Exact duplicate is harmless.
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.ack(update_sequence=6)

    def test_normal_ack_still_requires_owner_close_request(self):
        with self.assertRaisesRegex(ValueError, 'unexpected drive'):
            self.ack(closure_reason='normal_close')
        self.assertIsNone(self.owner.permission_fault)

    def test_unresolved_old_renewal_blocks_b_open(self):
        owner = self.owner
        record = dict(stage='B', scope_id='b'*32, generation=2, operation_id=8)
        owner.execution = SimpleNamespace(active=record, dispatch=lambda record: True,
                                          snapshot=lambda record: record,
                                          tick=lambda: {'authority_disposition': 'current'})
        owner.aborting = False
        owner.valid_observation = lambda: True
        owner.context_process = SimpleNamespace(poll=lambda: None)
        owner.contexts = {'B': dict(scope_id='b'*32, generation=2)}
        owner.b_observed_client = None
        owner.permission_context = None
        old = SimpleNamespace(done=lambda: False)
        owner.permission_future = old
        owner.permission_issued = 100
        response = SimpleNamespace(accepted=True, generation=2, deadline_ns=100+self.permission.LIFETIME_NS)
        opening = unittest.mock.Mock(return_value=SimpleNamespace(done=lambda: True, result=lambda: response))
        owner.permit_open = SimpleNamespace(service_is_ready=lambda: True, call_async=opening)
        def wait(predicate, *args, **kwargs):
            if not predicate():
                raise TimeoutError('old renewal unresolved')
        owner.wait = wait
        with tempfile.TemporaryDirectory() as directory, patch.object(self.permission, 'OUT', Path(directory)), \
                patch.object(self.permission, 'emit'), patch.object(self.permission, 'now_ns', return_value=100):
            with self.assertRaisesRegex(TimeoutError, 'old renewal unresolved'):
                owner.open_scope('B')
        self.assertIs(owner.permission_future, old)
        opening.assert_not_called()
        # A response rejected after normal closure is harmless, but must be
        # observed before B can acquire the one outstanding renewal slot.
        old.done = lambda: True
        old.result = unittest.mock.Mock(return_value=SimpleNamespace(accepted=False, deadline_ns=1200))
        with tempfile.TemporaryDirectory() as directory, patch.object(self.permission, 'OUT', Path(directory)), \
                patch.object(self.permission, 'emit'), patch.object(self.permission, 'now_ns', return_value=100):
            owner.open_scope('B')
        old.result.assert_called_once()
        opening.assert_called_once()
        self.assertIsNone(owner.permission_future)

    def test_missing_permission_service_prevents_public_endpoint_preparation(self):
        self.owner.scoped_clients = {}
        self.owner.create_client = lambda *a: SimpleNamespace(service_is_ready=lambda: False)
        self.owner.wait = unittest.mock.Mock(side_effect=TimeoutError('permission service missing'))
        with patch.object(driver.NavigationOwner, 'prepare_b_initial') as public_prepare:
            with self.assertRaisesRegex(TimeoutError, 'permission service missing'):
                self.owner.prepare_b_initial()
        public_prepare.assert_not_called()


class SceneOwnerChecks(unittest.TestCase):
    def owner(self):
        from robot_harness_nav2._failure import FailureOwner
        owner=FailureOwner.__new__(FailureOwner)
        owner.requests=None;owner.motion_started=False;owner.subscriptions_keep=[]
        owner.create_subscription=unittest.mock.Mock(return_value='map-subscription')
        return owner

    def test_occupied_map_is_required_before_native_successor_or_public_endpoint(self):
        owner=self.owner();owner.configure_scene('occupied-a')
        self.assertEqual(owner.map_id,'turtlebot3-occupied-a-probe-v1')
        self.assertEqual(owner.subscriptions_keep,['map-subscription'])
        with patch.object(scoped.ScopedDriver,'prepare_b_initial') as prepare:
            with self.assertRaisesRegex(RuntimeError,'map has not been checked'):owner.prepare_b_initial()
            prepare.assert_not_called()
        self.assertIsNone(owner.requests)

    def test_configured_identity_reaches_public_observation_and_late_configuration_is_refused(self):
        owner=self.owner();owner.configure_scene('occupied-a')
        owner.observation=lambda:dict(pose=(-2.,-.5),pose_stamp=1.,clock_age=0.,streams=[])
        owner.valid_observation=lambda:True;owner.localization_ok=True
        self.assertEqual(owner.public_observation()['map_id'],owner.map_id)
        owner.requests=object()
        with self.assertRaisesRegex(RuntimeError,'precede public startup'):owner.configure_scene('occupied-a')

    def test_wrong_profile_fails_before_ros_initialization(self):
        from robot_harness_nav2 import __main__ as entry
        with patch('sys.argv',['owner','--scene','occupied-a']),patch.object(entry,'require_isolation') as guard,patch.object(driver.rclpy,'init') as init:
            with self.assertRaises(SystemExit) as caught:entry.main()
            self.assertEqual(caught.exception.code,2);guard.assert_not_called();init.assert_not_called()


if __name__ == '__main__':
    unittest.main()
