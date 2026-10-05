"""Opt-in, live-owner closure of an associated A cancellation before B reuse."""
import time

from m4_drive_probe.srv import CloseScopedMotion

from ._observations import emit
from ._owner import NavigationOwner
from ._profile import REVISION_PROFILE_ID
from ._scoped import ScopedDriver, request_fields


class CancelledVisit(RuntimeError):
    """Interrupt the A visit wait; native outcome is still to be observed."""


class RevisionOwner(NavigationOwner):
    profile = REVISION_PROFILE_ID

    def __init__(self):
        self.intent_record = None
        self.close_until = self.task_until = None
        self.closing_visit = False
        super().__init__()

    def open_scope(self, stage):
        if self.task_until is None:
            self.task_until = time.monotonic()+240
            self.deadline = min(self.deadline, self.task_until)
        super().open_scope(stage)

    def request_stop(self, record):
        receipt = self.execution.snapshot(record)['receipt']
        pending = next((p for p in self.pending if p['record'] is record), None)
        if (not self.requests.closing and not self.aborting
                and record is self.execution.active and record['stage'] == 'A'
                and self.stage_index == 0 and self.intent_record is None
                and self.task_until is not None and pending is not None
                and pending.get('wrapped') is not None
                and receipt['native_acceptance'] == 'accepted'
                and receipt['native_outcome'] == 'pending'
                and receipt['native_identity'] == pending['goal_id'] == record.get('goal_id')
                and receipt['expiry_observed_at'] is None):
            self.intent_record = record
            self.close_until = min(time.monotonic()+15, receipt['deadline']/1e9,
                                   self.task_until, self.deadline)
            emit('revision_cancel_requested', record=self.execution.snapshot(record),
                 close_until=self.close_until)
        else:
            # Unconfirmed/late acceptance, B cancellation and shutdown keep the
            # existing bounded abort and unknown closure behavior.
            super().request_stop(record)

    def authority_progress_allowed(self, receipt):
        if self.intent_record is None:
            return super().authority_progress_allowed(receipt)
        return (self.execution.active is self.intent_record
                and receipt['authority_disposition'] == 'revoked'
                and receipt['expiry_observed_at'] is None
                and time.monotonic() < self.close_until)

    def tick(self):
        # Parent pumps the endpoint before the authority check: a cancel arriving
        # in this tick can select this narrow closure branch immediately. Its
        # global-close, freshness and process-loss checks still take precedence.
        super().tick()
        if self.intent_record is not None and not self.aborting:
            self.cancel_pending()
            if not self.closing_visit:
                self.closing_visit = True
                raise CancelledVisit('associated A cancellation')

    def remaining(self):
        remaining = self.close_until-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('shared A closure deadline')
        return remaining

    def visit_and_close(self, stage):
        try:
            super().visit_and_close(stage)
        except CancelledVisit:
            if stage != 'A':
                raise
            self.finish_cancel()

    def finish_cancel(self):
        self.tick()
        record = self.intent_record
        pending = next(p for p in self.pending if p['record'] is record)
        context = self.contexts['A']
        context.setdefault('close_requested_ns', time.monotonic_ns())
        # Exact native cancel was initiated by tick. Seal the outlet separately,
        # before waiting for the retained native terminal future.
        seal = self.service(CloseScopedMotion, '/close_scoped_motion',
                            CloseScopedMotion.Request(**request_fields(context)),
                            min(5, self.remaining()), fresh=True)
        result_future = pending['wrapped'].get_result_async()
        self.wait(result_future.done, self.remaining(), require_fresh=True)
        result = result_future.result()
        if result.status not in (4, 5):
            raise RuntimeError('A did not terminate cancelled or succeeded')
        emit('revision_native_terminal', stage='A', goal_id=pending['goal_id'], status=result.status)
        self.wait(seal.done, self.remaining(), require_fresh=True)
        if seal.result().accepted is not True:
            raise RuntimeError('early outlet seal refused')
        self.active = None
        ScopedDriver.close_visit(self, 'A', goal_id=pending['goal_id'], close_until=self.close_until)
        self.fresh_b_ready(close_until=self.close_until)
        self.tick()
        self.remaining()
        receipt = self.execution.snapshot(record)['receipt']
        # A native success racing cancel stays succeeded. An already published
        # result must not be published again or erased by the cancellation.
        if receipt['output'] == 'pending':
            candidate = None if result.status == 5 else dict(stage='A', goal_id=pending['goal_id'],
                                                           observation=self.observation())
            self.execution.dispose_result(record, candidate, self.observation)
        self.tick()
        self.remaining()
        self.execution.settle(record)
        self.execution.release(record)
        self.stage_index = 1
        self.intent_record = None
        self.closing_visit = False
        emit('core_visit_closed', stage='A', record=self.execution.snapshot(record),
             permits_next=True, reason='associated-cancel')
