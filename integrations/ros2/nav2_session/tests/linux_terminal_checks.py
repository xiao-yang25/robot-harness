"""Actual Owner abort/query methods, Core/socket; synthetic ROS edges only."""
from pathlib import Path
import socket
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'bindings/python/tests'))
import test_navigation as navigation_fixture
from robot_harness._transport import Channel
from robot_harness_nav2 import _owner as driver
from robot_harness_nav2 import _scoped as scoped
from robot_harness_nav2._requests import RequestEndpoint
from robot_harness_nav2._terminal import TerminalWindow


class TerminalOwnerChecks(unittest.TestCase):
    def setUp(self):
        self.fixture = navigation_fixture.NavigationTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.now = 10.
        self.events = []
        self.owner = driver.NavigationOwner.__new__(driver.NavigationOwner)
        owner = self.owner
        owner.execution = self.fixture.execution
        owner.requests = self.fixture.requests
        owner.requests.request_stop = owner.request_stop
        owner.terminal_window = TerminalWindow(8, lambda: self.now)
        owner.requests.submit(self.fixture.message('original'))
        owner.execution.dispatch(owner.execution.active)
        owner.stop_requested = owner.completed = owner.aborting = False
        owner.current = dict(scope_id='b'*32, generation=1)
        owner.scoped_clients = {'/close_scoped_motion': SimpleNamespace(
            service_is_ready=lambda: True, call_async=self.outlet)}
        handle = SimpleNamespace(accepted=True,
            goal_id=SimpleNamespace(uuid=list(bytes.fromhex('a'*32))),
            cancel_goal_async=lambda: self.events.append('native_cancel'))
        owner.pending = [dict(record=owner.execution.active, goal_id='a'*32,
                              terminal=False, handle=handle)]
        owner.endpoint = RequestEndpoint(owner.requests, terminal=owner.terminal_window)
        self.addCleanup(owner.endpoint.dispose)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(owner.endpoint.path)
        self.client = Channel(sock)
        self.addCleanup(self.client.close)
        owner.endpoint.pump()

    def outlet(self, request):
        self.events.append('outlet_close')
        return SimpleNamespace(done=lambda: False)

    def spin(self, *args, **kwargs):
        self.events.append('progress')
        self.now += .1

    def cancel(self):
        self.client.queue(dict(rpc=1, command='cancel', request_id='original'))
        self.client.pump()
        with patch.object(scoped.ScopedDriver, 'tick'), self.assertRaises(driver.ClientClosed):
            self.owner.tick()
        self.assertTrue(self.client.pump()[0]['result']['affected_current'])
        self.assertEqual(self.owner.execution.snapshot(self.owner.execution.active)
                         ['receipt']['authority_disposition'], 'revoked')

    def cleanup(self):
        with patch.object(driver.time, 'monotonic', side_effect=lambda: self.now), \
                patch.object(driver.rclpy, 'spin_once', side_effect=self.spin), \
                patch.object(driver, 'diagnose'):
            self.owner.abort()
            self.owner.finish_terminal_queries()

    def test_abort_services_status_and_close_while_native_cleanup_progresses(self):
        self.cancel()
        for rpc, command in ((2, 'status'), (3, 'close')):
            self.client.queue(dict(rpc=rpc, command=command, request_id='original'))
        self.client.pump()
        self.cleanup()
        replies = self.client.pump()
        self.assertEqual([r['rpc'] for r in replies], [2, 3])
        self.assertEqual(replies[0]['result']['request_id'], 'original')
        self.assertEqual(replies[0]['result']['receipt']['authority_disposition'], 'revoked')
        self.assertEqual(replies[0]['result']['receipt']['settlement'], 'pending')
        self.assertEqual(replies[1]['result'],
                         dict(intent_received=True, closure='unknown', owner_reaped=False))
        self.assertEqual(self.events.count('native_cancel'), 1)
        self.assertEqual(self.events.count('outlet_close'), 1)
        self.assertGreater(self.events.count('progress'), 0)
        self.assertEqual(self.owner.terminal_window.deadline, 18.)

    def test_no_close_expires_without_extending_original_window(self):
        self.cancel()
        self.cleanup()
        self.assertTrue(self.owner.endpoint.closed)
        self.assertFalse(Path(self.owner.endpoint.path).exists())
        self.assertTrue(self.owner.requests.closing)
        self.assertEqual(self.owner.terminal_window.deadline, 18.)
        self.assertLess(self.now, 18.2)
        self.assertEqual(self.events.count('native_cancel'), 1)
        self.assertEqual(list(self.owner.execution.records), ['original'])

    def test_close_reply_backpressure_is_served_after_native_abort(self):
        self.cancel()
        native = self.owner.endpoint.peer.sock
        class BlockedSend:
            def send(self, data): raise BlockingIOError()
            def __getattr__(self, name): return getattr(native, name)
        self.owner.endpoint.peer.sock = BlockedSend()
        self.client.queue(dict(rpc=2, command='close'))
        self.client.pump()
        original = self.spin
        def spin(*args, **kwargs):
            original(*args, **kwargs)
            if self.now >= 12.3:
                self.owner.endpoint.peer.sock = native
        self.spin = spin
        self.cleanup()
        self.assertGreaterEqual(self.now, 12.3)
        self.assertLess(self.now, 18.)
        self.assertTrue(self.owner.endpoint.replies_drained)
        self.assertEqual(self.client.pump()[0]['result']['closure'], 'unknown')

    def test_partial_preparation_preserves_original_abort_progress(self):
        self.owner.execution = self.owner.requests = self.owner.endpoint = None
        self.owner.current = None
        self.owner.pending = []
        self.cleanup()
        self.assertGreater(self.events.count('progress'), 0)
        self.assertFalse(self.owner.terminal_window.started)
        self.assertEqual(self.events.count('native_cancel'), 0)
        self.assertEqual(self.events.count('outlet_close'), 0)

    def test_disabled_window_keeps_legacy_abort_without_query_service(self):
        self.owner.terminal_window = self.owner.endpoint.terminal = None
        self.cancel()
        self.client.queue(dict(rpc=2, command='status', request_id='original'))
        self.client.pump()
        self.cleanup()
        self.assertEqual(self.client.pump(), [])
        self.assertFalse(self.owner.endpoint.closed)
        self.assertEqual(self.events.count('native_cancel'), 1)
        self.assertEqual(self.events.count('outlet_close'), 1)


if __name__ == '__main__':
    unittest.main()
