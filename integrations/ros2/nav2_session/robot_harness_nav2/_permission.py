"""Explicit finite motion permission for the isolated, same-host Nav2 profile."""
import json
import time

from ._owner import NavigationOwner
from ._profile import PERMISSION_PROFILE_ID
from ._observations import emit, OUT
from ._scoped import request_fields
from m4_drive_probe.srv import OpenPermit, RenewPermit, ClosePermit

LIFETIME_NS = 1_000_000_000
PERIOD_NS = 100_000_000


def now_ns():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)


class CloseClient:
    """Convert the unchanged trusted Owner close path at its native boundary."""
    def __init__(self, owner, native):
        self.owner, self.native = owner, native

    def service_is_ready(self):
        return self.native.service_is_ready()

    def call_async(self, request):
        context = next((c for c in self.owner.contexts.values()
                        if c['scope_id'] == request.scope_id and c['generation'] == request.generation), None)
        if context is None or not context.get('operation_id'):
            raise RuntimeError('close has no admitted operation binding')
        self.owner.permission_context = None
        return self.native.call_async(ClosePermit.Request(
            **request_fields(context), operation_id=context['operation_id']))


class PermissionOwner(NavigationOwner):
    profile = PERMISSION_PROFILE_ID

    def __init__(self):
        self.permission_context = self.permission_future = self.permission_fault = None
        self.permission_issued = self.permission_due = 0
        super().__init__()

    def prepare_b_initial(self):
        self.permit_open = self.create_client(OpenPermit, '/owner_permission/open')
        self.permit_renew = self.create_client(RenewPermit, '/owner_permission/renew')
        close = self.create_client(ClosePermit, '/owner_permission/close')
        self.scoped_clients['/close_scoped_motion'] = CloseClient(self, close)
        for client in (self.permit_open, self.permit_renew, close):
            self.wait(client.service_is_ready, 5, require_fresh=True)
        super().prepare_b_initial()
        emit('permission_profile_ready', profile=self.profile,
             lifetime_ns=LIFETIME_NS, period_ns=PERIOD_NS, clock='CLOCK_MONOTONIC')

    def prerequisites(self, context):
        record = self.execution.active
        if (self.aborting or record is None or record['operation_id'] != context['operation_id']
                or record['scope_id'] != context['scope_id'] or record['generation'] != context['generation']
                or not self.valid_observation() or self.context_process.poll() is not None
                or self.execution.tick()['authority_disposition'] != 'current'):
            raise RuntimeError('owner permission prerequisites lost')

    def open_scope(self, stage):
        # A normal close stops issuance, not ownership of an outstanding RPC.
        # Observe its result before opening the successor; timeout fails closed.
        if self.permission_future is not None:
            if self.permission_context is not None:
                raise RuntimeError('previous motion permission still active')
            future = self.permission_future
            self.wait(future.done, 5, require_fresh=True)
            response = future.result()
            if response.accepted and response.deadline_ns != self.permission_issued+LIFETIME_NS:
                raise RuntimeError('closed permission renewal identity invalid')
            self.permission_future = None
            emit('closed_permission_response', accepted=response.accepted,
                 deadline_ns=response.deadline_ns)
        record = self.execution.active
        if (self.aborting or record is None or record['stage'] != stage
                or not self.valid_observation() or not self.execution.dispatch(record)):
            raise RuntimeError('prepared request lost dispatch permission')
        if stage == 'B':
            self.client = self.b_observed_client
        emit('core_admitted', stage=stage, record=self.execution.snapshot(record))
        context = self.contexts[stage]
        context['operation_id'] = record['operation_id']
        path = OUT/('operation-'+context['scope_id']+'.json')
        if path.exists():
            raise RuntimeError('producer operation binding reused')
        temporary = path.with_suffix('.tmp')
        temporary.write_text(json.dumps(dict(**request_fields(context), operation_id=context['operation_id'])))
        temporary.replace(path)
        self.motion_started = True
        self.current = context
        emit('scope_open_requested', stage=stage, **request_fields(context))
        self.wait(self.permit_open.service_is_ready, 5, require_fresh=True)
        self.prerequisites(context)
        issued = now_ns()
        request = OpenPermit.Request(scope_id=context['scope_id'],
            expected_generation=context['generation']-1, operation_id=context['operation_id'], issued_ns=issued)
        future = self.permit_open.call_async(request)
        self.wait(future.done, 5, require_fresh=True)
        response = future.result()
        if (response.accepted is not True or response.generation != context['generation']
                or response.deadline_ns != issued+LIFETIME_NS or now_ns() >= response.deadline_ns):
            raise RuntimeError('finite scope opening refused or expired')
        context['initial_permission_deadline_ns'] = response.deadline_ns
        context['last_permission_deadline_ns'] = response.deadline_ns
        self.permission_context = context
        self.permission_due = issued+PERIOD_NS
        emit('scope_opened', stage=stage, **request_fields(context),
             operation_id=context['operation_id'], permission_deadline_ns=response.deadline_ns)

    def tick(self):
        try:
            super().tick()
        except RuntimeError as error:
            try:
                emit('owner_tick_failed', error=str(error), observation=self.observation(),
                     localization_ok=self.localization_ok)
            except Exception as diagnostic_error:
                emit('owner_tick_diagnostic_unavailable', error=str(diagnostic_error))
            raise
        if self.permission_fault is not None and not self.aborting:
            raise RuntimeError('native motion permission fault latched')
        context = self.permission_context
        if context is None or self.aborting:
            return
        if self.permission_future is not None:
            if not self.permission_future.done():
                return  # One in flight; another thread cannot keep it alive.
            response = self.permission_future.result()
            self.permission_future = None
            if (response.accepted is not True or response.deadline_ns != self.permission_issued+LIFETIME_NS
                    or now_ns() >= response.deadline_ns):
                raise RuntimeError('native permission renewal refused')
            emit('owner_permission_response', **request_fields(context),
                 operation_id=context['operation_id'], deadline_ns=response.deadline_ns)
        if now_ns() < self.permission_due:
            return
        self.prerequisites(context)  # After response logging; immediately before the stamp/send.
        issued = now_ns()
        self.permission_future = self.permit_renew.call_async(RenewPermit.Request(
            **request_fields(context), operation_id=context['operation_id'], issued_ns=issued))
        self.permission_issued = issued
        self.permission_due = issued+PERIOD_NS
        context['last_permission_deadline_ns'] = issued+LIFETIME_NS
        emit('owner_permission_issued', **request_fields(context),
             operation_id=context['operation_id'], issued_ns=issued, deadline_ns=issued+LIFETIME_NS)

    def drive_ack(self, message):
        if len(message.data) > 4096:
            raise ValueError('oversized drive fact')
        row = json.loads(message.data)
        if not isinstance(row, dict):
            raise ValueError('drive fact must be an object')
        for name in ('generation', 'operation_id', 'update_sequence', 'sim_ns',
                     'steady_ns', 'permission_deadline_ns'):
            if type(row.get(name)) is not int or not 0 < row[name] < 2**64:
                raise ValueError('invalid permission application field: '+name)
        context = next((c for c in self.contexts.values() if c['scope_id'] == row.get('scope_id')), None)
        if (context is None or row['operation_id'] != context.get('operation_id')
                or row['generation'] != context['generation']):
            raise ValueError('permission application identity mismatch')
        if (row.get('event') != 'motion_scope_applied' or row.get('sealed') is not True
                or row.get('wheel_targets_zero') is not True
                or row['steady_ns'] > now_ns()
                or not context.get('initial_permission_deadline_ns', 0)
                    <= row['permission_deadline_ns'] <= context.get('last_permission_deadline_ns', 0)):
            raise ValueError('permission application fact invalid')
        if row.get('closure_reason') in ('permission_expired', 'clock_invalid'):
            if (row['closure_reason'] == 'permission_expired'
                    and row['steady_ns'] < row['permission_deadline_ns']):
                raise ValueError('permission expiry application predates deadline')
            if self.permission_fault is not None:
                if self.permission_fault != row:
                    raise ValueError('permission fault application changed')
                return
            self.permission_fault = row
            # This unsolicited fact does not establish native cleanup, quiet,
            # settlement or task success. tick raises into the existing abort path.
            emit('owner_permission_fault', fact=row)
            return
        if row.get('closure_reason') != 'normal_close':
            raise ValueError('unexpected permission closure reason')
        super().drive_ack(message)
