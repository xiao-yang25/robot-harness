"""Synchronous client; simulation/worker progress belongs to a separate host."""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

from ._transport import Channel, integer


class SessionError(RuntimeError):
    pass


def child_environment():
    env = os.environ.copy()
    root = str(Path(__file__).resolve().parent.parent)
    env['PYTHONPATH'] = root + os.pathsep + env.get('PYTHONPATH', '')
    return env


class Session:
    """One cooperating local caller, one bounded deterministic session; not thread-safe."""

    def __init__(self, *, worker_delay_ms=0, startup_timeout=5.0, mujoco=None):
        integer(worker_delay_ms, 'worker_delay_ms', 0, 10000)
        if not isinstance(startup_timeout, (int, float)) or not 0 < startup_timeout <= 120:
            raise ValueError('invalid startup_timeout')
        parent, child = socket.socketpair()
        self._channel = Channel(parent, binary=mujoco is not None)
        self._skill = 'aloha.transfer_cube_trial' if mujoco is not None else 'fixture.increment'
        self._process = None
        self._sequence = 0
        self._closed = False
        try:
            self._process = subprocess.Popen(
                [sys.executable, '-m', 'robot_harness._host', str(child.fileno()),
                 str(worker_delay_ms), json.dumps(mujoco), str(startup_timeout)], pass_fds=(child.fileno(),), env=child_environment(),
                stdout=sys.stderr)
            child.close()
            deadline = time.monotonic() + startup_timeout
            while True:
                status = self._call('status', timeout=startup_timeout)
                if status['phase'] == 'ready':
                    self.host_pid = status['host_pid']
                    self.worker_pid = status['worker_pid']
                    break
                if status['phase'] == 'failed' or time.monotonic() >= deadline:
                    raise SessionError('session startup failed or timed out')
                time.sleep(0.01)
        except BaseException:
            child.close()
            self._disconnect_and_reap()
            raise

    def _call(self, command, timeout=5.0, **arguments):
        if self._closed:
            raise SessionError('session is closed')
        self._sequence += 1
        rpc = self._sequence
        try:
            self._channel.queue({'rpc': rpc, 'command': command, **arguments})
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                for reply in self._channel.pump():
                    # A timed-out request may answer before the next status query.
                    if reply.get('rpc') != rpc:
                        continue
                    if 'error' in reply:
                        raise SessionError(reply['error'])
                    result = reply['result']
                    if '_payload' in reply:
                        result['rgb'] = reply['_payload']
                    return result
                time.sleep(0.002)
        except (EOFError, OSError, ValueError, BufferError) as error:
            raise SessionError(f'connection failed; operation outcome may be unknown: {error}') from error
        raise SessionError('request timed out; query its request ID before resubmitting')

    def capabilities(self):
        return self._call('capabilities')

    def observe(self):
        return self._call('observe')

    def submit(self, request_id, *, steps=10, deadline_ms=None):
        return self._call('submit', request_id=request_id, skill=self._skill,
                          steps=steps, deadline_ms=deadline_ms)

    def status(self, request_id=None):
        return self._call('status', request_id=request_id)

    def cancel(self, request_id):
        return self._call('cancel', request_id=request_id)

    def reset(self, *, seed=None):
        return self._call('reset', seed=seed)

    def close(self):
        if self._closed:
            # A closed channel is not proof that its owner process was reaped.
            self._disconnect_and_reap()
            return
        error = None
        try:
            result = self._call('close', timeout=5.0)
            if not result.get('worker_reaped'):
                raise SessionError('worker cleanup not confirmed')
        except BaseException as caught:
            error = caught
        finally:
            try:
                self._disconnect_and_reap()
            except BaseException as caught:
                if error is None:
                    error = caught
        if error is not None:
            raise error

    def _disconnect_and_reap(self):
        self._closed = True
        self._channel.close()
        if self._process is not None:
            try:
                code = self._process.wait(timeout=5.0)
            except subprocess.TimeoutExpired as error:
                # Do not kill the owner and then claim its worker was cleaned up.
                raise SessionError(f'host cleanup unconfirmed; host PID {self._process.pid}') from error
            if code != 0:
                raise SessionError(f'host exited with error {code}')

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        self.close()
