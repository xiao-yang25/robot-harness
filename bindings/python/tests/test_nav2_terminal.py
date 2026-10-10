"""Real Core and Unix endpoint, controlled time; no ROS or physical stop."""
from pathlib import Path
import socket
import unittest

from robot_harness._transport import Channel
from robot_harness_nav2._requests import RequestEndpoint
from robot_harness_nav2._terminal import TerminalWindow
import test_navigation as navigation_fixture


class WindowTests(unittest.TestCase):
    def test_fixed_deadline_is_not_renewed(self):
        now = [10.]
        window = TerminalWindow(8, lambda: now[0])
        self.assertFalse(window.started)
        self.assertFalse(window.expired)
        window.begin()
        self.assertEqual(window.deadline, 18.)
        now[0] = 17.
        window.begin()
        self.assertEqual(window.deadline, 18.)
        self.assertFalse(window.expired)
        now[0] = 18.
        self.assertTrue(window.expired)

    def test_invalid_values(self):
        for value in (True, None, 0, -1, 11, float('nan'), float('inf')):
            with self.subTest(value=value), self.assertRaises(ValueError):
                TerminalWindow(value)


class EndpointWindowTests(unittest.TestCase):
    def setUp(self):
        self.fixture = navigation_fixture.NavigationTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.now = 10.
        self.window = TerminalWindow(8, lambda: self.now)
        self.requests = self.fixture.requests
        def stop(record):
            self.fixture.stops.append(record)
            self.window.begin()
        self.requests.request_stop = stop
        self.message = dict(self.fixture.message('original'), command='submit')
        self.requests.command(self.message)
        self.endpoint = RequestEndpoint(self.requests, terminal=self.window)
        self.addCleanup(self.endpoint.dispose)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(self.endpoint.path)
        self.client = Channel(sock)
        self.addCleanup(self.client.close)
        self.endpoint.pump()

    def exchange(self, *messages):
        for rpc, message in enumerate(messages, 1):
            self.client.queue(dict(message, rpc=rpc))
        self.client.pump()
        self.endpoint.pump()
        return self.client.pump()

    def test_batched_cancel_queries_replay_and_new_requests(self):
        replies = self.exchange(
            dict(command='cancel', request_id='original'),
            dict(command='status', request_id='original'),
            self.message, dict(self.message, site='B'),
            dict(self.message, request_id='new'), dict(command='observe'),
            dict(command='capabilities'), dict(command='cancel', request_id='original'))
        self.assertEqual(len(replies), 8)
        self.assertTrue(replies[0]['result']['affected_current'])
        self.assertEqual(replies[1]['result']['receipt']['authority_disposition'], 'revoked')
        self.assertEqual(replies[1]['result']['operation_id'], replies[2]['result']['operation_id'])
        self.assertIn('different arguments', replies[3]['error'])
        self.assertIn('new requests', replies[4]['error'])
        self.assertIn('new observations', replies[5]['error'])
        self.assertFalse(replies[6]['result']['available'])
        self.assertEqual(len(self.fixture.stops), 1)
        self.assertEqual(list(self.requests.execution.records), ['original'])
        self.assertEqual(self.window.deadline, 18.)

    def test_expiry_disposes_endpoint_without_admitting_queued_work(self):
        self.exchange(dict(command='cancel', request_id='original'))
        self.now = 18.
        self.client.queue(dict(self.message, rpc=9, request_id='new'))
        self.client.pump()
        self.endpoint.pump()
        self.assertTrue(self.requests.closing)
        self.assertTrue(self.endpoint.closed)
        self.assertFalse(Path(self.endpoint.path).exists())
        self.assertEqual(list(self.requests.execution.records), ['original'])

    def test_eof_revokes_before_local_endpoint_disposal(self):
        self.client.close()
        self.endpoint.pump()
        self.assertEqual(self.requests.status('original')['receipt']['authority_disposition'], 'revoked')
        self.assertTrue(self.endpoint.closed)
        self.assertFalse(Path(self.endpoint.path).exists())

    def test_close_reply_stays_pending_under_send_backpressure(self):
        self.exchange(dict(command='cancel', request_id='original'))
        native = self.endpoint.peer.sock
        class BlockedSend:
            def send(self, data): raise BlockingIOError()
            def __getattr__(self, name): return getattr(native, name)
        self.endpoint.peer.sock = BlockedSend()
        self.assertEqual(self.exchange(dict(command='close')), [])
        self.assertTrue(self.requests.closing)
        self.assertFalse(self.endpoint.replies_drained)
        self.endpoint.peer.sock = native
        self.endpoint.pump()
        replies = self.client.pump()
        self.assertEqual(replies[0]['result'],
                         dict(intent_received=True, closure='unknown', owner_reaped=False))
        self.assertTrue(self.endpoint.replies_drained)

    def test_invalid_submit_identity_is_a_reply_error(self):
        self.exchange(dict(command='cancel', request_id='original'))
        replies = self.exchange(dict(self.message, request_id=[]))
        self.assertIn('invalid request_id', replies[0]['error'])
        self.assertFalse(self.endpoint.closed)
        self.assertEqual(list(self.requests.execution.records), ['original'])


if __name__ == '__main__':
    unittest.main()
