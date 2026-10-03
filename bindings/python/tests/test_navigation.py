"""Real-Core request boundary checks; no ROS or physical stop assertions."""
import unittest
import socket
import tempfile
import threading
import time
from pathlib import Path

from robot_harness import _core
from robot_harness._execution import ExecutionCoordinator, accepted
from robot_harness.navigation import NavigationRequests, SKILL
from robot_harness import NavigationSession, SessionError
from robot_harness._transport import Channel


class NavigationTests(unittest.TestCase):
    def setUp(self):
        self.stamp = 1_000_000
        self.valid, self.epoch, self.sequence = True, 0, 0
        self.available = True
        self.stops = []
        self.gate = _core.Gate('native', 'drive', 'navigation', 'test', 1, self.stamp,
                              settlement_scope='native-outlet-quiet-and-next-context')
        for kind in ('idle', 'worker', 'sink'):
            accepted(self.gate.startup(kind, True, self.stamp))
        accepted(self.gate.capabilities(True, self.stamp, self.stamp+60_000_000_000))
        self.execution = ExecutionCoordinator(self.gate, lambda: self.stamp)
        self.requests = NavigationRequests(self.execution, sites={'A': (1, 2, 0), 'B': (2, 1, 0)},
            profile='test', map_id='map-v1', observation=self.observation,
            ready=lambda: self.available,
            context=lambda: dict(stage='A', scope_id='a'*32, generation=1),
            request_stop=self.stops.append)

    def tearDown(self):
        self.gate.close()

    def observation(self):
        return dict(epoch=self.epoch, map_id='map-v1', frame='map', valid=self.valid,
                    pose=[0, 0], sequence=self.sequence)

    def message(self, reference='one', site='A'):
        return dict(request_id=reference, skill=SKILL, site=site, deadline_ms=1000,
                    expected_observation=self.requests.observe()['reference'])

    def finish(self, reference):
        record = self.execution.records[reference]
        self.assertTrue(self.execution.dispatch(record))
        accepted(self.execution.native(record, 'accepted', 'a'*32))
        accepted(self.execution.native(record, 'succeeded', 'a'*32))
        self.execution.dispose_result(record, {'visit': 'done'}, lambda: {})
        self.execution.settle(record)
        self.execution.release(record)

    def test_advancing_sequence_replay_conflict_and_old_cancel(self):
        message = self.message()
        self.sequence += 1
        first = self.requests.submit(message)
        self.assertEqual(first['state'], 'running')
        self.assertEqual(self.requests.submit(message)['operation_id'], first['operation_id'])
        with self.assertRaisesRegex(ValueError, 'different arguments'):
            self.requests.submit(dict(message, site='B'))
        self.finish('one')
        second = self.requests.submit(self.message('two', 'B'))
        self.assertNotEqual(first['operation_id'], second['operation_id'])
        self.assertFalse(self.requests.cancel('one')['affected_current'])
        self.assertEqual(self.requests.status('two'), second)
        self.assertEqual(self.stops, [])

    def test_new_expired_reference_refused_replay_retained_after_eviction(self):
        message = self.message()
        admitted = self.requests.submit(message)
        self.stamp += 1_000_000_001
        self.execution.tick()
        for _ in range(65):
            self.requests.observe()
        replay = self.requests.submit(message)
        self.assertEqual(replay['operation_id'], admitted['operation_id'])
        self.assertEqual(replay['receipt']['authority_disposition'], 'revoked')
        refused = self.requests.submit(dict(message, request_id='new'))
        self.assertIsNone(refused['operation_id'])
        self.assertEqual(refused['reason'], 'stale_or_foreign_observation')
        self.assertEqual(len(self.requests.observations), 64)

    def test_foreign_epoch_invalid_and_delayed_context_refuse_admission(self):
        original = self.message()
        forged = dict(original['expected_observation'], session_id='foreign')
        self.assertIsNone(self.requests.submit(dict(original, expected_observation=forged))['operation_id'])
        fresh = self.message('epoch')
        self.epoch = 1
        self.assertIsNone(self.requests.submit(fresh)['operation_id'])
        fresh = self.message('invalid')
        self.valid = False
        self.assertIsNone(self.requests.submit(fresh)['operation_id'])
        self.valid = True
        fresh = self.message('delayed')
        def delayed_context():
            self.stamp += 1_000_000_001
            return dict(stage='A', scope_id='a'*32, generation=1)
        self.requests.context = delayed_context
        self.assertIsNone(self.requests.submit(fresh)['operation_id'])

    def test_pending_busy_capacity_and_shutdown_never_settle(self):
        self.requests.submit(self.message())
        for count in range(63):
            rejected = self.requests.submit(self.message('busy-'+str(count)))
            self.assertEqual(rejected['reason'], 'busy')
        with self.assertRaisesRegex(ValueError, 'capacity'):
            self.requests.submit(self.message('overflow'))
        first = self.requests.cancel('one')
        self.assertTrue(first['affected_current'])
        self.requests.cancel('one')
        closed = self.requests.close()
        self.assertEqual(len(self.stops), 1)
        self.assertEqual(closed['closure'], 'unknown')
        self.assertFalse(closed['owner_reaped'])
        self.assertEqual(self.requests.status('one')['receipt']['settlement'], 'pending')
        self.assertEqual(self.requests.status('one')['receipt']['authority_disposition'], 'revoked')
        self.assertFalse(self.requests.capabilities()['available'])

    def test_closed_not_ready_and_parameter_validation(self):
        self.available = False
        self.assertEqual(self.requests.submit(self.message())['reason'], 'not_ready')
        for updates in ({'site': 'unknown'}, {'deadline_ms': True}, {'skill': 'fixture'},
                        {'expected_observation': {}}, {'request_id': ''}):
            with self.assertRaises(ValueError):
                self.requests.submit(dict(self.message('bad'), **updates))
        self.requests.close()
        self.assertEqual(self.requests.submit(self.message('closed'))['reason'], 'closing')
        with self.assertRaises(ValueError):
            NavigationRequests(self.execution, sites={'x': (float('nan'), 0, 0)}, profile='p',
                map_id='m', observation=self.observation, ready=lambda: True,
                context=lambda: {}, request_stop=lambda row: None)

    def test_invalid_issued_observation_cannot_become_valid_by_recovery(self):
        self.valid = False
        message = self.message()
        self.valid = True
        row = self.requests.submit(message)
        self.assertEqual(row['reason'], 'stale_or_foreign_observation')
        self.assertIsNone(row['operation_id'])

    def test_failed_stop_scheduling_remains_revoked_and_can_be_requested_again(self):
        self.requests.submit(self.message())
        def fail(record):
            raise RuntimeError('stop scheduler failed')
        self.requests.request_stop = fail
        with self.assertRaisesRegex(RuntimeError, 'scheduler'):
            self.requests.cancel('one')
        self.assertEqual(self.requests.status('one')['receipt']['authority_disposition'], 'revoked')
        self.assertNotIn('stop_requested', self.execution.active)
        self.requests.request_stop = self.stops.append
        self.requests.close()
        self.assertEqual(len(self.stops), 1)


class NavigationClientTests(unittest.TestCase):
    def start_peer(self, handler):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        endpoint = Path(directory.name)/'navigation.sock'
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(str(endpoint))
        listener.listen(1)
        listener.settimeout(2)
        def serve():
            channel = None
            try:
                peer, _ = listener.accept()
                channel = Channel(peer)
                deadline = time.monotonic()+2
                while time.monotonic() < deadline:
                    for message in channel.pump():
                        if not handler(channel, message):
                            return
                    time.sleep(.001)
            except EOFError:
                pass
            finally:
                if channel is not None:
                    channel.close()
                listener.close()
        thread = threading.Thread(target=serve)
        thread.start()
        def reap():
            thread.join(timeout=3)
            self.assertFalse(thread.is_alive(), 'test peer was not reaped')
        self.addCleanup(reap)
        return endpoint

    def test_timeout_late_reply_status_and_close_are_distinct(self):
        def handler(channel, message):
            if message['command'] == 'submit':
                time.sleep(.08)
                result = {'late_admission': True}
            elif message['command'] == 'close':
                result = {'intent_received': True, 'closure': 'unknown', 'owner_reaped': False}
            else:
                result = {'request_id': message['request_id'], 'state': 'running'}
            channel.queue(dict(rpc=message['rpc'], result=result))
            return True
        session = NavigationSession(self.start_peer(handler), timeout=.04)
        with self.assertRaisesRegex(SessionError, 'query its request ID'):
            session.submit('one', site='A', expected_observation={})
        session._timeout = .5
        self.assertEqual(session.status('one')['state'], 'running')
        closed = session.close()
        self.assertEqual(closed['closure'], 'unknown')
        self.assertFalse(closed['owner_reaped'])
        session.close()
        with self.assertRaisesRegex(SessionError, 'closed'):
            session.observe()

    def test_eof_is_unknown_and_cleanup_still_closes_the_client(self):
        session = NavigationSession(self.start_peer(lambda channel, message: False))
        with self.assertRaisesRegex(SessionError, 'outcome may be unknown'):
            session.status()
        with self.assertRaises(SessionError):
            session.close()
        self.assertTrue(session._closed)
        for timeout in (True, float('nan'), 0, 121):
            with self.assertRaises(ValueError):
                NavigationSession('/unused', timeout=timeout)


if __name__ == '__main__':
    unittest.main()
