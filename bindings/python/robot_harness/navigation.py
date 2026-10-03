"""Local navigation requests to an already prepared, trusted single-writer owner."""

from copy import deepcopy
import math
import socket
import time
import uuid

from ._transport import Channel, identifier, integer
from .session import SessionError

SKILL = 'navigation.visit_site'


class NavigationSession:
    """One cooperating client; does not launch or own the navigation process.

    The owner must keep progressing native work independently of this client.
    A closed connection or an accepted shutdown intent is not a robot stop.
    """

    def __init__(self, endpoint, *, timeout=5.0):
        if (type(timeout) not in (int, float) or not math.isfinite(timeout)
                or not 0 < timeout <= 120):
            raise ValueError('invalid timeout')
        self._timeout = timeout
        self._sequence = 0
        self._closed = False
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.settimeout(timeout)
            sock.connect(str(endpoint))
            self._channel = Channel(sock)
        except BaseException:
            sock.close()
            raise

    def _call(self, command, **arguments):
        if self._closed:
            raise SessionError('navigation session is closed')
        self._sequence += 1
        rpc = self._sequence
        try:
            self._channel.queue(dict(rpc=rpc, command=command, **arguments))
            deadline = time.monotonic() + self._timeout
            while time.monotonic() < deadline:
                for reply in self._channel.pump():
                    if reply.get('rpc') != rpc:
                        continue
                    if 'error' in reply:
                        raise SessionError(reply['error'])
                    return reply['result']
                time.sleep(.002)
        except (EOFError, OSError, ValueError, BufferError) as error:
            raise SessionError(f'connection failed; navigation outcome may be unknown: {error}') from error
        raise SessionError('request timed out; query its request ID before resubmitting')

    def capabilities(self):
        return self._call('capabilities')

    def observe(self):
        return self._call('observe')

    def submit(self, request_id, *, site, expected_observation, deadline_ms=147000):
        return self._call('submit', request_id=request_id, skill=SKILL, site=site,
                          expected_observation=expected_observation, deadline_ms=deadline_ms)

    def status(self, request_id=None):
        return self._call('status', request_id=request_id)

    def cancel(self, request_id):
        return self._call('cancel', request_id=request_id)

    def close(self):
        if self._closed:
            return
        try:
            return self._call('close')
        finally:
            self._closed = True
            self._channel.close()

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        self.close()


class NavigationRequests:
    """Trusted-owner support, not a client-configurable backend/driver registry.

    Borrow the coordinator. The owner supplies live observation, readiness,
    immutable producer context and nonblocking stop scheduling. This helper
    creates no ROS resource or progress thread and never asserts settlement.
    """

    def __init__(self, execution, *, sites, profile, map_id, observation, ready,
                 context, request_stop, maximum_deadline_ms=147000):
        if not isinstance(sites, dict) or not 1 <= len(sites) <= 32:
            raise ValueError('invalid site table')
        self.sites = {}
        for name, target in sites.items():
            identifier(name, 'site')
            if (not isinstance(target, (list, tuple)) or len(target) != 3
                    or any(type(v) not in (int, float) or not math.isfinite(v) for v in target)):
                raise ValueError('site target must be finite x, y, yaw')
            self.sites[name] = tuple(target)
        self.profile = identifier(profile, 'profile')
        self.map_id = identifier(map_id, 'map_id')
        self.maximum_deadline_ms = integer(maximum_deadline_ms, 'maximum_deadline_ms', 1, 147000)
        self.execution = execution
        self.clock = execution.clock
        self.read_observation = observation
        self.ready = ready
        self.context = context
        self.request_stop = request_stop
        self.session_id = uuid.uuid4().hex
        self.observations = {}
        self.closing = False

    def _current(self):
        value = self.read_observation()
        if (not isinstance(value, dict) or type(value.get('epoch')) is not int
                or value['epoch'] < 0 or value.get('map_id') != self.map_id
                or value.get('frame') != 'map' or type(value.get('valid')) is not bool):
            raise ValueError('invalid owner observation identity')
        return value

    def observe(self):
        value = deepcopy(self._current())
        reference = dict(session_id=self.session_id, observation_id=uuid.uuid4().hex,
                         epoch=value['epoch'], map_id=self.map_id, frame='map')
        if len(self.observations) == 64:
            del self.observations[next(iter(self.observations))]
        self.observations[reference['observation_id']] = (reference, self.clock(), value['valid'])
        value['reference'] = dict(reference)
        return value

    def capabilities(self):
        value = self._current()
        available = (not self.closing and self.execution.active is None and value['valid']
                     and self.ready() and self.execution.gate.supports(1, self.clock()))
        return dict(skill=SKILL, profile=self.profile, map_id=self.map_id, frame='map',
                    sites=list(self.sites), available=bool(available),
                    maximum_deadline_ms=self.maximum_deadline_ms)

    def submit(self, message):
        reference = identifier(message.get('request_id'), 'request_id')
        if message.get('skill') != SKILL:
            raise ValueError('unsupported navigation skill')
        site = identifier(message.get('site'), 'site')
        if site not in self.sites:
            raise ValueError('unknown registered site')
        proposed = message.get('expected_observation')
        if not isinstance(proposed, dict):
            raise ValueError('observation reference required')
        fields = ('session_id', 'observation_id', 'epoch', 'map_id', 'frame')
        if (any(key not in proposed for key in fields) or type(proposed['epoch']) is not int
                or any(not isinstance(proposed[key], str) for key in fields if key != 'epoch')):
            raise ValueError('invalid observation reference')
        observation_reference = tuple(proposed[key] for key in fields)
        deadline_ms = integer(message.get('deadline_ms'), 'deadline_ms', 1, self.maximum_deadline_ms)
        arguments = (site, observation_reference, deadline_ms)
        # An idempotent read does not need a still-live issued observation.
        old = self.execution.records.get(reference)
        if old is not None:
            if old['arguments'] != arguments:
                raise ValueError('request ID reused with different arguments')
            return self.status(reference)
        record, _ = self.execution.reserve(reference, arguments,
            lambda: dict(skill=SKILL, site=site, target=self.sites[site],
                         observation_reference=dict(zip(fields, observation_reference))))
        issued = self.observations.get(proposed['observation_id'])
        available = self.ready() if not self.closing and self.execution.active is None else False
        context = self.context() if available else None
        # Driver callbacks can block. Check issued/current input and Core time
        # after them, at admission, rather than carrying an earlier permission.
        current = self._current()
        stamp = self.clock()
        reason = None
        if self.closing:
            reason = 'closing'
        elif (issued is None or not issued[2] or tuple(issued[0][key] for key in fields) != observation_reference
              or not 0 <= stamp-issued[1] <= 1_000_000_000
              or current['epoch'] != proposed['epoch'] or not current['valid']):
            reason = 'stale_or_foreign_observation'
        elif self.execution.active is not None:
            reason = 'busy'
        elif not available:
            reason = 'not_ready'
        if reason:
            record['reason'] = reason
        else:
            # Copy only the trusted driver's consumed association fields; do not
            # allow a callback to overwrite request arguments or Core ownership.
            record.update(stage=context['stage'], scope_id=context['scope_id'],
                          generation=context['generation'])
            self.execution.admit(record, 1, stamp+deadline_ms*1_000_000, stamp)
        return self.status(reference)

    def status(self, reference=None):
        if reference is not None:
            record = self.execution.records[identifier(reference, 'request_id')]
            return deepcopy(self.execution.snapshot(record))
        active = self.execution.active
        return dict(session_id=self.session_id, closing=self.closing,
                    active=None if active is None else self.status(active['request_id']))

    def cancel(self, reference):
        record = self.execution.records[identifier(reference, 'request_id')]
        affected = self.execution.cancel(record)
        if affected and not record.get('stop_requested'):
            self.request_stop(record)
            record['stop_requested'] = True
        return dict(intent_received=True, affected_current=affected, closure='unknown',
                    record=self.status(reference))

    def close(self):
        self.closing = True
        active = self.execution.active
        if active is not None:
            self.cancel(active['request_id'])
        return dict(intent_received=True, closure='unknown', owner_reaped=False)

    def command(self, message):
        command = message.get('command')
        if command == 'submit':
            return self.submit(message)
        if command == 'cancel':
            return self.cancel(message.get('request_id'))
        if command == 'status':
            return self.status(message.get('request_id'))
        if command in ('capabilities', 'observe', 'close'):
            return getattr(self, command)()
        raise ValueError('unsupported navigation command')
