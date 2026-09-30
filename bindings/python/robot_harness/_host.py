"""Single-writer session owner; the worker supplies candidates, never effects."""

from collections import deque
import json
import os
import socket
import subprocess
import sys
import time
import uuid

from . import _core
from ._fixture import IncrementBackend
from ._transport import Channel, identifier, integer
from .session import child_environment

MAX_RECORDS = 64
MAX_STEPS = 400


def now():
    return time.monotonic_ns()


def accepted(disposition):
    if disposition != 'accepted':
        raise RuntimeError(f'Core rejected evidence: {disposition}')


class Host:
    def __init__(self, channel, worker_delay_ms, mujoco=None, startup_timeout=5.0):
        self.channel = channel
        self.worker_channel = None
        self.worker = None
        self.startup_timeout = startup_timeout
        self.backend = self.gate = None
        parent = child = None
        try:
            if mujoco is None:
                self.backend = IncrementBackend()
            else:
                from robot_harness_mujoco import AlohaBackend
                self.backend = AlohaBackend(mujoco['output'], mujoco.get('seed', 0))
            self.gate = _core.Gate(self.backend.provider, self.backend.domain,
                                   self.backend.capability, self.backend.profile, MAX_STEPS, now())
            self.session_id = uuid.uuid4().hex
            self.phase = 'starting'
            self.records = {}
            self.active = None
            self.queue = deque()
            self.prediction = None
            self.prediction_sequence = 0
            self.worker_ready = False
            self.failure = None
            self.closing_at = None
            self.close_rpc = None
            self.peer_alive = True
            self.shutdown_sent = False
            self.terminated_at = None
            self.killed = False
            self.close_ready_at = None
            self.next_step = 0
            self.next_capability_observation = 0
            self.start_time = time.monotonic()
            parent, child = socket.socketpair()
            self.worker_channel = Channel(parent, binary=mujoco is not None)
            command = ([sys.executable, '-m', 'robot_harness._worker', str(child.fileno()),
                        str(worker_delay_ms)] if mujoco is None else
                       [sys.executable, mujoco['worker_script'], str(child.fileno()),
                        mujoco['checkpoint'], mujoco.get('device', 'mps')])
            self.worker = subprocess.Popen(command, pass_fds=(child.fileno(),),
                                           env=child_environment())
        except BaseException:
            try:
                if self.worker_channel is not None:
                    self.worker_channel.close()
                elif parent is not None:
                    parent.close()
                if self.backend is not None:
                    self.backend.close()
            finally:
                if self.gate is not None:
                    self.gate.close()
            raise
        finally:
            if child is not None:
                child.close()

    def refresh_capability(self, available):
        stamp = now()
        accepted(self.gate.capabilities(available, stamp, stamp + 60_000_000_000))
        self.next_capability_observation = stamp + 30_000_000_000

    def maintain_capability(self):
        if (self.phase in ('ready', 'running') and self.worker_ready and
                self.worker.poll() is None and not self.backend.closed and
                now() >= self.next_capability_observation):
            # Same live worker generation, unchanged fixture profile and owned sink.
            # This observes availability, not completion or inference responsiveness.
            self.refresh_capability(True)

    def initialize(self):
        # All observations come from resources created by this owner, not the caller.
        for kind in ('idle', 'worker', 'sink'):
            accepted(self.gate.startup(kind, True, now()))
        self.refresh_capability(True)
        self.phase = 'ready'

    def snapshot(self, record):
        result = {key: value for key, value in record.items() if key != 'arguments'}
        if self.active is record:
            result['receipt'] = self.gate.receipt(record['operation_id'])
            result['steps'] = self.backend.steps
            result['queue_remaining'] = len(self.queue)
            result['prediction_pending'] = self.prediction is not None
        return result

    def status(self):
        return {'phase': self.phase, 'host_pid': os.getpid(),
                'worker_pid': self.worker.pid, 'worker_reaped': self.worker.poll() is not None,
                'session_id': self.session_id, 'failure': self.failure,
                'observation': self.backend.observe()}

    def command(self, message):
        rpc = integer(message.get('rpc'), 'rpc', 1, 2**53)
        command = message.get('command')
        if command == 'close':
            self.close_rpc = rpc
            self.begin_close()
            return
        try:
            payload = b''
            if command == 'status':
                request_id = message.get('request_id')
                result = self.status() if request_id is None else self.snapshot(self.records[identifier(request_id, 'request_id')])
            elif command == 'observe':
                result, payload = self.backend.sample()
            elif command == 'capabilities':
                result = {'skill': self.backend.capability, 'profile': self.backend.profile, 'maximum_steps': MAX_STEPS,
                          'available': self.phase == 'ready' and self.gate.supports(1, now())}
            elif command == 'submit':
                result = self.submit(message)
            elif command == 'reset':
                if self.phase != 'needs_reset' or self.active is not None:
                    raise ValueError('reset requires a settled episode and healthy worker')
                seed = message.get('seed')
                if seed is not None:
                    integer(seed, 'seed', 0, 2**31 - 1)
                    if isinstance(self.backend, IncrementBackend):
                        raise ValueError('fixture reset does not accept a seed')
                try:
                    if seed is None:
                        self.backend.reset()
                    else:
                        self.backend.reset(seed=seed)
                except Exception as error:
                    self.fail_worker(f'backend reset error: {error}')
                    raise ValueError(self.failure) from error
                self.refresh_capability(True)
                self.phase = 'ready'
                result = self.backend.observe()
            elif command == 'cancel':
                record = self.records[identifier(message.get('request_id'), 'request_id')]
                if record is self.active:
                    self.gate.cancel(record['operation_id'], now())
                    self.queue.clear()
                    record['revoked_at_step'] = self.backend.steps
                result = self.snapshot(record)
            else:
                raise ValueError('unsupported command')
            self.channel.queue({'rpc': rpc, 'result': result}, payload)
        except (ValueError, KeyError) as error:
            self.channel.queue({'rpc': rpc, 'error': str(error)})

    def submit(self, message):
        reference = identifier(message.get('request_id'), 'request_id')
        steps = integer(message.get('steps'), 'steps', 1, MAX_STEPS)
        deadline_ms = message.get('deadline_ms')
        if deadline_ms is not None:
            integer(deadline_ms, 'deadline_ms', 1, 60000)
        arguments = (message.get('skill'), steps, deadline_ms)
        if reference in self.records:
            record = self.records[reference]
            if record['arguments'] != arguments:
                raise ValueError('request ID reused with different arguments')
            return self.snapshot(record)
        if len(self.records) >= MAX_RECORDS:
            raise ValueError('session record capacity reached; open a new session')
        record = {'request_id': reference, 'arguments': arguments, 'state': 'rejected',
                  'operation_id': None, 'steps': 0, 'result': None, 'receipt': None}
        self.records[reference] = record
        if arguments[0] != self.backend.capability or self.phase != 'ready':
            record['reason'] = 'unsupported_skill' if arguments[0] != self.backend.capability else 'not_ready'
            return self.snapshot(record)
        stamp = now()
        deadline = None if deadline_ms is None else stamp + deadline_ms * 1_000_000
        decision = self.gate.admit(reference, steps, deadline, stamp)
        record['reason'] = decision['status']
        if decision['status'] != 'admitted':
            return self.snapshot(record)
        record.update(operation_id=decision['operation_id'], state='running')
        self.active = record
        self.phase = 'running'
        if not self.gate.dispatch(record['operation_id'], now()):
            accepted(self.gate.non_submission(record['operation_id'], now()))
            self.finish('not_submitted', native=False)
        else:
            accepted(self.gate.native(record['operation_id'], 'accepted', now()))
        return self.snapshot(record)

    def identity(self):
        receipt = self.gate.receipt(self.active['operation_id'])
        return {'session': self.session_id, 'authority': receipt['authority'],
                'observation': self.backend.observe(), 'prediction': self.prediction_sequence}

    def worker_message(self, message):
        if message.get('kind') == 'ready' and not self.worker_ready and self.phase == 'starting':
            self.worker_ready = True
            self.initialize()
            return
        if message.get('kind') != 'prediction' or self.prediction is None:
            raise ValueError('unsolicited worker response')
        if message.get('identity') != self.prediction['identity']:
            raise ValueError('prediction identity mismatch')
        actions = message.get('actions')
        self.backend.validate_actions(actions, self.prediction['count'])
        self.prediction = None
        receipt = self.gate.receipt(self.active['operation_id'])
        if receipt['authority_disposition'] == 'current' and self.closing_at is None and self.failure is None:
            self.queue.extend(actions)

    def advance(self):
        if self.active is None:
            return
        operation = self.active['operation_id']
        self.gate.tick(operation, now())
        receipt = self.gate.receipt(operation)
        if receipt['authority_disposition'] != 'current':
            self.queue.clear()
            if self.prediction is None:
                self.finish('worker_error' if self.failure else 'cancelled')
            return
        if not self.gate.supports(self.active['arguments'][1], now()):
            self.gate.cancel(operation, now())
            self.queue.clear()
            self.active['revoked_at_step'] = self.backend.steps
            return
        if self.prediction is not None:
            return
        if not self.queue:
            self.prediction_sequence += 1
            count = min(100, self.active['arguments'][1] - self.backend.steps)
            self.prediction = {'identity': self.identity(), 'count': count}
            observation, payload = self.backend.sample()
            self.worker_channel.queue({'kind': 'predict', **self.prediction,
                                       'observation': observation}, payload)
            return
        if time.monotonic() < self.next_step:
            return
        # Single writer: no await, callback or bulk submission between permission and effect.
        action = self.queue.popleft()
        try:
            self.backend.step(action)
        except Exception as error:
            self.fail_worker(f'backend step error: {error}')
            return
        if self.backend.steps == 1:
            accepted(self.gate.native(operation, 'started', now()))
        self.next_step = time.monotonic() + self.backend.step_interval
        reason = self.backend.completion(self.active['arguments'][1])
        if reason is not None:
            self.finish(reason)

    def finish(self, reason, native=True):
        record = self.active
        operation = record['operation_id']
        self.gate.tick(operation, now())
        self.queue.clear()
        if self.closing_at is None:
            self.refresh_capability(False)
        summary = self.backend.finish(reason)
        record['execution'] = summary
        if native:
            outcome = 'succeeded' if reason == 'completed' else ('failed' if reason in ('worker_error', 'step_limit', 'native_failure') else 'cancelled')
            accepted(self.gate.native(operation, outcome, now()))
        record['reason'] = reason
        if reason == 'completed' and self.gate.can_deliver(operation):
            # The actual bounded in-memory record is the declared result sink.
            record['result'] = summary
            disposition = self.gate.output(operation, 'accepted', record['request_id'], now())
            if disposition != 'accepted':
                # No client can observe this staged value while the owner is here.
                # Expiry can occur between the permission check and evidence time.
                record['result'] = None
                record['diagnostic'] = self.backend.observe()
                self.gate.tick(operation, now())
                if self.gate.can_deliver(operation):
                    accepted(disposition)
                accepted(self.gate.output(operation, 'authority_revoked', record['request_id'], now()))
        else:
            record['diagnostic'] = self.backend.observe()
            disposition = 'authority_revoked' if reason == 'completed' else 'no_output'
            reference = record['request_id'] if reason == 'completed' else ''
            accepted(self.gate.output(operation, disposition, reference, now()))
        accepted(self.gate.settle(operation, now()))
        record.update(state='finished', steps=self.backend.steps, receipt=self.gate.receipt(operation))
        self.active = None
        self.phase = 'failed' if self.failure else ('closing' if self.closing_at else 'needs_reset')

    def fail_worker(self, error):
        if self.failure is None:
            self.failure = str(error)
            self.refresh_capability(False)
            if self.active:
                self.gate.cancel(self.active['operation_id'], now())
                self.active['revoked_at_step'] = self.backend.steps
            self.queue.clear()
            self.phase = 'failed'

    def begin_close(self):
        if self.closing_at is None:
            self.closing_at = time.monotonic()
            if self.active:
                self.gate.cancel(self.active['operation_id'], now())
            self.queue.clear()
            self.gate.close()
            self.phase = 'closing'

    def stop_worker(self):
        if self.worker.poll() is not None:
            self.prediction = None
            return
        if not self.shutdown_sent:
            try:
                if self.worker_channel is not None:
                    self.worker_channel.queue({'kind': 'close'})
            except (OSError, BufferError):
                pass
            self.shutdown_sent = True
            self.terminated_at = time.monotonic() + 0.25
        if time.monotonic() >= self.terminated_at and not self.killed:
            self.worker.terminate()
            self.killed = True
            self.terminated_at = time.monotonic() + 0.25
        elif self.killed and time.monotonic() >= self.terminated_at:
            self.worker.kill()
            self.terminated_at = time.monotonic() + 0.25

    def run(self):
        try:
            while True:
                self.maintain_capability()
                if self.peer_alive:
                    try:
                        for message in self.channel.pump():
                            self.command(message)
                    except (EOFError, OSError, ValueError, BufferError):
                        self.peer_alive = False
                        self.begin_close()
                if self.worker_channel:
                    try:
                        for message in self.worker_channel.pump():
                            self.worker_message(message)
                    except (EOFError, OSError, ValueError, BufferError) as error:
                        if self.closing_at is None:
                            self.fail_worker(error)
                        self.worker_channel.close()
                        self.worker_channel = None
                if self.worker.poll() is not None:
                    if self.closing_at is None:
                        self.fail_worker('worker exited')
                    self.prediction = None
                if self.failure or self.closing_at is not None:
                    self.stop_worker()
                if self.phase == 'starting' and time.monotonic() - self.start_time > self.startup_timeout:
                    self.fail_worker('worker readiness timeout')
                self.advance()
                if self.closing_at is not None and self.worker.poll() is not None:
                    if self.close_ready_at is None:
                        self.close_ready_at = time.monotonic()
                    self.backend.close()
                    if self.peer_alive and self.close_rpc is not None:
                        self.channel.queue({'rpc': self.close_rpc, 'result': {'worker_reaped': True}})
                        self.close_rpc = None
                    if (not self.peer_alive or not self.channel.outgoing or
                            time.monotonic() - self.close_ready_at >= 1):
                        return
                time.sleep(0.002)
        finally:
            # Unexpected host exceptions still retain worker ownership through reaping.
            self.gate.close()
            if self.worker.poll() is None:
                self.worker.kill()
                self.worker.wait(timeout=3)
            if self.worker_channel:
                self.worker_channel.close()
            self.backend.close()
            self.channel.close()


def main():
    mujoco = json.loads(sys.argv[3]) if len(sys.argv) > 3 else None
    channel = Channel(socket.socket(fileno=int(sys.argv[1])), binary=mujoco is not None)
    Host(channel, int(sys.argv[2]), mujoco, float(sys.argv[4]) if len(sys.argv) > 4 else 5).run()


if __name__ == '__main__':
    main()
