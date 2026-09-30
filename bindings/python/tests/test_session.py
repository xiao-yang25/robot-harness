import itertools
import os
import signal
import socket
import struct
import subprocess
import time
import unittest
from unittest.mock import Mock, patch

from robot_harness import Session, SessionError, _core
from robot_harness._host import Host
from robot_harness._transport import Channel, MAX_FRAME, MAX_PAYLOAD


def finished(session, reference, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = session.status(reference)
        if result['state'] == 'finished':
            return result
        time.sleep(0.01)
    raise AssertionError(session.status(reference))


class SessionTests(unittest.TestCase):
    def test_two_operations_reuse_host_and_worker_and_retain_old_receipt(self):
        with Session() as session:
            host, worker = session.host_pid, session.worker_pid
            self.assertTrue(session.capabilities()['available'])
            first = session.submit('one', steps=7)
            done = finished(session, 'one')
            self.assertEqual(done['result'], {'steps': 7, 'value': 7})
            self.assertEqual(done['receipt']['settlement'], 'settled')
            self.assertEqual(done['receipt']['output'], 'accepted')
            self.assertEqual(done['receipt']['authority_disposition'], 'released')
            self.assertFalse(session.capabilities()['available'])
            self.assertEqual(session.submit('one', steps=7), done)
            with self.assertRaises(SessionError):
                session.submit('one', steps=8)
            session.reset()
            second = session.submit('two', steps=4)
            self.assertNotEqual(first['operation_id'], second['operation_id'])
            self.assertEqual(finished(session, 'two')['result']['value'], 4)
            self.assertEqual(session.status('one'), done)
            self.assertEqual((session.status()['host_pid'], session.status()['worker_pid']), (host, worker))
        for pid in (host, worker):
            with self.assertRaises(ProcessLookupError):
                os.kill(pid, 0)
        session.close()

    def test_caller_delay_does_not_stop_host_progress(self):
        with Session() as session:
            session.submit('unpolled', steps=10)
            time.sleep(0.2)
            self.assertEqual(session.status('unpolled')['state'], 'finished')

    def test_cancel_nonempty_queue_prevents_later_steps(self):
        with Session() as session:
            session.submit('long', steps=100)
            deadline = time.monotonic() + 3
            while True:
                status = session.status('long')
                if status.get('queue_remaining', 0) > 10 and status['steps'] > 1:
                    break
                self.assertLess(time.monotonic(), deadline)
            cancelled = session.cancel('long')
            done = finished(session, 'long')
            self.assertEqual(done['steps'], cancelled['revoked_at_step'])
            self.assertIsNone(done['result'])
            self.assertEqual(done['receipt']['native_outcome'], 'cancelled')
            self.assertEqual(done['receipt']['output'], 'not_delivered')
            self.assertEqual(done['receipt']['settlement'], 'settled')

    def test_deadline_progress_during_slow_prediction(self):
        with Session(worker_delay_ms=600) as session:
            session.submit('slow', steps=8, deadline_ms=40)
            time.sleep(0.08)
            status = session.status('slow')
            self.assertEqual(status['steps'], 0)
            self.assertNotEqual(status['receipt']['authority_disposition'], 'current')
            self.assertEqual(status['receipt']['settlement'], 'pending')
            done = finished(session, 'slow')
            self.assertEqual(done['steps'], 0)
            self.assertEqual(done['receipt']['settlement'], 'settled')

    def test_worker_exit_preserves_failure_and_cleanup(self):
        with Session(worker_delay_ms=600) as session:
            session.submit('lost', steps=10)
            os.kill(session.worker_pid, signal.SIGKILL)
            result = finished(session, 'lost')
            self.assertEqual(result['reason'], 'worker_error')
            self.assertIsNone(result['result'])
            self.assertEqual(result['receipt']['native_outcome'], 'failed')
            self.assertEqual(session.status()['phase'], 'failed')

    def test_active_close_reaps_worker(self):
        session = Session(worker_delay_ms=1000)
        worker = session.worker_pid
        session.submit('closing', steps=100)
        session.close()
        with self.assertRaises(ProcessLookupError):
            os.kill(worker, 0)

    def test_unresponsive_worker_close_escalates_and_reaps(self):
        session = Session()
        worker = session.worker_pid
        try:
            os.kill(worker, signal.SIGSTOP)
            session.submit('stalled', steps=100)
            self.assertEqual(session.cancel('stalled')['steps'], 0)
        finally:
            session.close()
        with self.assertRaises(ProcessLookupError):
            os.kill(worker, 0)

    def test_caller_disconnect_causes_owner_cleanup(self):
        session = Session(worker_delay_ms=1000)
        worker = session.worker_pid
        session.submit('disconnect', steps=100)
        session._disconnect_and_reap()
        with self.assertRaises(ProcessLookupError):
            os.kill(worker, 0)

    def test_request_capacity_is_bounded(self):
        with Session(worker_delay_ms=600) as session:
            session.submit('first', steps=1)
            for number in range(63):
                self.assertEqual(session.submit(f'refused-{number}')['state'], 'rejected')
            with self.assertRaisesRegex(SessionError, 'capacity'):
                session.submit('overflow')

    def test_partial_worker_startup_closes_backend(self):
        parent, child = socket.socketpair()
        try:
            with patch('robot_harness._host.subprocess.Popen', side_effect=OSError('injected spawn error')), \
                 patch('robot_harness._host.IncrementBackend.close', autospec=True) as close:
                with self.assertRaisesRegex(OSError, 'injected'):
                    Host(Channel(parent), 0)
                close.assert_called_once()
        finally:
            parent.close()
            child.close()

    def test_partial_core_or_socket_startup_closes_backend(self):
        for target in ('robot_harness._host._core.Gate', 'robot_harness._host.socket.socketpair'):
            with self.subTest(target=target), \
                 patch(target, side_effect=OSError('injected resource failure')), \
                 patch('robot_harness._host.IncrementBackend.close', autospec=True) as close:
                with self.assertRaisesRegex(OSError, 'injected resource failure'):
                    Host(None, 0)
                close.assert_called_once()

    def test_failed_reset_withdraws_session_and_preserves_old_result(self):
        parent, child = socket.socketpair()
        host = Host(Channel(parent), 0)
        try:
            host.initialize()
            host.submit({'request_id': 'reset', 'skill': 'fixture.increment', 'steps': 1})
            host.backend.step(1)
            host.finish('completed')
            old = host.snapshot(host.records['reset'])
            with patch.object(host.backend, 'reset', side_effect=OSError('reset failed')):
                host.command({'rpc': 1, 'command': 'reset'})
            self.assertEqual(host.phase, 'failed')
            self.assertEqual(host.snapshot(host.records['reset']), old)
            self.assertEqual(host.submit({'request_id': 'next', 'skill': 'fixture.increment',
                                          'steps': 1})['state'], 'rejected')
        finally:
            host.begin_close()
            host.run()
            child.close()

    def test_failed_native_budget_has_diagnostics_without_success_output(self):
        parent, child = socket.socketpair()
        host = Host(Channel(parent), 0)
        try:
            host.initialize()
            host.submit({'request_id': 'limit', 'skill': 'fixture.increment', 'steps': 1})
            host.backend.step(1)
            host.finish('step_limit')
            done = host.records['limit']
            self.assertIsNone(done['result'])
            self.assertEqual(done['execution']['value'], 1)
            self.assertEqual(done['receipt']['native_outcome'], 'failed')
            self.assertEqual(done['receipt']['output'], 'not_delivered')
            self.assertEqual(done['receipt']['settlement'], 'settled')
        finally:
            host.begin_close()
            host.run()
            child.close()

    def test_recording_failure_does_not_claim_settlement(self):
        parent, child = socket.socketpair()
        host = Host(Channel(parent), 0)
        try:
            host.initialize()
            host.submit({'request_id': 'seal', 'skill': 'fixture.increment', 'steps': 1})
            with patch.object(host.backend, 'finish', side_effect=OSError('recording failed')):
                with self.assertRaisesRegex(OSError, 'recording failed'):
                    host.finish('completed')
            receipt = host.gate.receipt(host.active['operation_id'])
            self.assertEqual(receipt['settlement'], 'pending')
            self.assertIsNone(host.active['result'])
        finally:
            host.begin_close()
            host.run()
            child.close()

    def test_repeated_close_retries_unconfirmed_owner_cleanup(self):
        session = Session.__new__(Session)
        session._closed = True
        session._channel = Mock()
        session._process = Mock(pid=999)
        session._process.wait.side_effect = [subprocess.TimeoutExpired('host', 5), 0]
        with self.assertRaisesRegex(SessionError, 'cleanup unconfirmed'):
            session.close()
        session.close()
        self.assertEqual(session._process.wait.call_count, 2)

    def test_idle_capability_observation_renews_healthy_session(self):
        parent, child = socket.socketpair()
        host = Host(Channel(parent), 0)
        try:
            deadline = time.monotonic() + 3
            while not host.worker_ready:
                for message in host.worker_channel.pump():
                    host.worker_message(message)
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.002)
            future = time.monotonic_ns() + 61_000_000_000
            self.assertFalse(host.gate.supports(1, future))
            with patch('robot_harness._host.now', return_value=future):
                host.maintain_capability()
                self.assertTrue(host.gate.supports(1, future))
                self.assertEqual(host.phase, 'ready')
        finally:
            host.begin_close()
            host.run()
            child.close()

    def test_deadline_crossed_inside_native_step_does_not_deliver_success(self):
        parent, child = socket.socketpair()
        host = Host(Channel(parent), 0)
        try:
            deadline = time.monotonic() + 3
            while not host.worker_ready:
                for message in host.worker_channel.pump():
                    host.worker_message(message)
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.002)
            host.submit({'request_id': 'late-step', 'skill': 'fixture.increment',
                         'steps': 1, 'deadline_ms': 50})
            native_step = host.backend.step

            def slow_step(action):
                native_step(action)
                time.sleep(0.07)

            with patch.object(host.backend, 'step', side_effect=slow_step):
                while host.active is not None:
                    for message in host.worker_channel.pump():
                        host.worker_message(message)
                    host.advance()
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.002)
            result = host.records['late-step']
            self.assertEqual(result['receipt']['native_outcome'], 'succeeded')
            self.assertEqual(result['receipt']['output_non_delivery_reason'], 'authority_revoked')
            self.assertEqual(result['receipt']['settlement'], 'settled')
            self.assertIsNone(result['result'])
        finally:
            host.begin_close()
            host.run()
            child.close()

    def test_expiry_at_output_evidence_records_non_delivery_and_settlement(self):
        parent, child = socket.socketpair()
        host = Host(Channel(parent), 0)
        try:
            wait_until = time.monotonic() + 3
            while not host.worker_ready:
                for message in host.worker_channel.pump():
                    host.worker_message(message)
                self.assertLess(time.monotonic(), wait_until)
                time.sleep(0.002)
            host.submit({'request_id': 'late-output', 'skill': 'fixture.increment',
                         'steps': 1, 'deadline_ms': 100})
            operation = host.active['operation_id']
            host.backend.step(1)
            self.assertEqual(host.gate.native(operation, 'started', time.monotonic_ns()), 'accepted')
            stamp = time.monotonic_ns()
            expired = host.gate.receipt(operation)['deadline'] + 1
            # tick, capability and native evidence precede deadline; sink evidence does not.
            clock = itertools.chain([stamp] * 3, itertools.repeat(expired))
            with patch('robot_harness._host.now', side_effect=lambda: next(clock)):
                host.finish('completed')
            result = host.records['late-output']
            self.assertEqual(result['state'], 'finished')
            self.assertIsNone(result['result'])
            self.assertEqual(result['receipt']['output_non_delivery_reason'], 'authority_revoked')
            self.assertEqual(result['receipt']['settlement'], 'settled')
        finally:
            host.begin_close()
            host.run()
            child.close()


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.parent, self.child = socket.socketpair()
        self.channel = Channel(self.parent)

    def tearDown(self):
        self.channel.close()
        self.child.close()

    def test_partial_frame_and_optional_metadata(self):
        data = b'{"version":2,"rpc":7,"optional":"ok"}'
        frame = struct.pack('!I', len(data)) + data
        self.child.sendall(frame[:5])
        self.assertEqual(self.channel.pump(), [])
        self.child.sendall(frame[5:])
        self.assertEqual(self.channel.pump()[0]['rpc'], 7)

    def test_binary_frame_is_bounded_and_exact_across_partial_reads(self):
        self.channel.binary = True
        self.channel.limit += MAX_PAYLOAD
        sender = Channel(self.child, binary=True)
        payload = bytes(range(256)) * 3600
        sender.queue({'rpc': 8, 'result': {'sequence': 4}}, payload)
        messages = []
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            sender.pump()
            messages.extend(self.channel.pump())
            if messages:
                break
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0]['_payload'], payload)
        self.assertEqual(messages[0]['result']['sequence'], 4)
        self.assertFalse(sender.outgoing)
        with self.assertRaises(BufferError):
            sender.queue({}, payload + b'x')
        sender.queue({}, payload)
        with self.assertRaises(BufferError):
            sender.queue({}, payload)

    def test_binary_payload_rejected_on_control_channel(self):
        sender = Channel(self.child, binary=True)
        sender.queue({'rpc': 8}, b'x')
        sender.pump()
        with self.assertRaises(ValueError):
            self.channel.pump()

    def test_oversize_peer_and_backpressure_are_bounded(self):
        self.child.sendall(struct.pack('!I', MAX_FRAME + 1))
        with self.assertRaises(ValueError):
            self.channel.pump()
        with self.assertRaises(BufferError):
            self.channel.queue({'blob': 'x' * MAX_FRAME})
        self.channel.queue({'blob': 'x' * 40000})
        self.channel.queue({'blob': 'x' * 40000})
        self.channel.queue({'blob': 'x' * 40000})
        with self.assertRaises(BufferError):
            self.channel.queue({'blob': 'x' * 40000})


class BridgeTests(unittest.TestCase):
    def test_core_requires_real_startup_and_current_capability(self):
        gate = _core.Gate('provider', 'domain', 'cap', 'profile', 10, 0)
        self.assertNotEqual(gate.admit('early', 1, None, 1)['status'], 'admitted')
        for kind in ('idle', 'worker', 'sink'):
            self.assertEqual(gate.startup(kind, True, 2), 'accepted')
        self.assertEqual(gate.capabilities(True, 3, 30), 'accepted')
        op = gate.admit('normal', 2, None, 4)['operation_id']
        self.assertTrue(gate.dispatch(op, 5))
        self.assertFalse(gate.dispatch(op, 6))
        self.assertFalse(gate.supports(2, 30))
        self.assertTrue(gate.supports(2, 29))
        self.assertFalse(gate.can_deliver(op))
        with self.assertRaises((ValueError, OverflowError)):
            gate.tick(op, -1)
        gate.close()


if __name__ == '__main__':
    unittest.main()
