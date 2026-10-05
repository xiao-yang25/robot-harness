"""Opt-in closure of a confirmed planner-failed A before caller-selected B."""
import json
import time

from lifecycle_msgs.srv import GetState
from m4_drive_probe.srv import CloseScopedMotion
from nav2_msgs.srv import ManageLifecycleNodes
from std_srvs.srv import Trigger

from ._failure_facts import failed_children, controller_sealed
from ._observations import emit
from ._owner import NavigationOwner
from ._profile import FAILURE_PROFILE_ID
from ._scoped import ScopedDriver, request_fields


class FailedVisit(RuntimeError):
    """Only a real status6 A result selects the bounded evidence path."""


class FailureOwner(NavigationOwner):
    profile = FAILURE_PROFILE_ID

    def __init__(self):
        self.task_until = self.close_until = None
        self.failure_closing = False
        super().__init__()

    def open_scope(self, stage):
        if self.task_until is None:
            self.task_until = time.monotonic()+240
            self.deadline = min(self.deadline, self.task_until)
        super().open_scope(stage)

    def validate_result(self, stage, result):
        if stage == 'A' and result.status == 6:
            raise FailedVisit('associated native A failure')
        super().validate_result(stage, result)

    def remaining(self):
        remaining = self.close_until-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('original clipped failure closure deadline')
        return remaining

    def visit_and_close(self, stage):
        try:
            super().visit_and_close(stage)
        except FailedVisit:
            if stage != 'A':
                raise
            self.finish_failure()

    def finish_failure(self):
        if self.close_until is not None:
            raise RuntimeError('failure closure already attempted')
        self.tick()  # Global close/EOF/cancel, observation and authority win.
        record = self.execution.active
        if record is None:
            raise RuntimeError('no current failed A')
        pending = next((p for p in self.pending if p['record'] is record), None)
        snapshot = self.execution.snapshot(record)
        receipt = snapshot['receipt']
        if (record['stage'] != 'A' or self.stage_index != 0 or pending is None
                or pending.get('wrapped') is None or not pending['terminal']
                or receipt['native_acceptance'] != 'accepted' or receipt['native_outcome'] != 'failed'
                or not receipt['native_identity'] == pending['goal_id'] == record.get('goal_id')
                or receipt['authority_disposition'] != 'current' or receipt['expiry_observed_at'] is not None):
            raise RuntimeError('failure is not a current associated A terminal')
        emit('failure_native_terminal', stage='A', goal_id=pending['goal_id'], record=snapshot,
             observation=self.observation(), context=self.contexts['A'])
        self.close_until = min(time.monotonic()+15, receipt['deadline']/1e9,
                               self.task_until, self.deadline)
        emit('failure_close_requested', stage='A', goal_id=pending['goal_id'], close_until=self.close_until,
             task_until=self.task_until, owner_until=self.deadline)
        self.active = None  # Associated terminal observed; native work still needs proof.
        self.failure_closing = True
        try:
            ScopedDriver.close_visit(self, 'A', goal_id=pending['goal_id'], close_until=self.close_until)
            self.fresh_b_ready(close_until=self.close_until)
            self.tick()
            self.remaining()
            self.execution.dispose_result(record, None, self.observation)
            self.tick()
            self.remaining()
            self.execution.settle(record)
            self.execution.release(record)
            self.stage_index = 1
            emit('core_visit_closed', stage='A', record=self.execution.snapshot(record),
                 permits_next=True, reason='associated-planner-failure')
        except Exception as error:
            emit('failure_close_blocked', stage='A', goal_id=pending['goal_id'], error=f'{type(error).__name__}: {error}',
                 record=self.execution.snapshot(record), context=self.contexts['A'], close_until=self.close_until)
            raise
        finally:
            self.failure_closing = False

    def close_lane(self, stage, **kwargs):
        if not self.failure_closing:
            return super().close_lane(stage, **kwargs)
        context = self.contexts[stage]
        context['close_requested_ns'] = time.monotonic_ns()
        drive = self.service(CloseScopedMotion, '/close_scoped_motion',
            CloseScopedMotion.Request(**request_fields(context)), min(5, self.remaining()))
        self.wait(drive.done, self.remaining(), require_fresh=True)
        if drive.result().accepted is not True:
            raise RuntimeError('actual drive close refused')
        self.wait(lambda: 'drive_ack' in context, self.remaining(), require_fresh=True)
        context['bt_requested_ns'] = time.monotonic_ns()
        bt = self.service(Trigger, context['namespace']+'/close_navigation_scope', Trigger.Request(),
                          min(5, self.remaining()))
        self.wait(bt.done, self.remaining(), require_fresh=True)
        context['bt_received_ns'] = time.monotonic_ns()
        context['bt'] = json.loads(bt.result().message)
        emit('failure_bt_response', stage=stage, accepted=bt.result().success, fact=context['bt'])
        if bt.result().success is not True:
            raise RuntimeError('native leaf proof unavailable')
        child_id = failed_children(context['bt'], context)
        self.wait(lambda: set(context['children']['compute_path_to_pose']) == {'link', 'ack'},
                  self.remaining(), require_fresh=True)
        planner = context['children']['compute_path_to_pose']
        if planner['link']['child_uuid'] != child_id or planner['ack']['goal_uuid'] != child_id:
            raise ValueError('BT/planner worker identity mismatch')
        if context['children']['follow_path']:
            raise ValueError('controller submission contradicts never-entered native proof')
        context['controller_seal_requested_ns'] = time.monotonic_ns()
        seal = self.service(Trigger, context['namespace']+'/seal_unsubmitted_controller', Trigger.Request(),
                            min(5, self.remaining()))
        self.wait(seal.done, self.remaining(), require_fresh=True)
        context['controller_seal_received_ns'] = time.monotonic_ns()
        if seal.result().success is not True:
            raise RuntimeError('native unused controller seal refused')
        context['unsubmitted_controller'] = json.loads(seal.result().message)
        controller_sealed(context['unsubmitted_controller'], context)
        pause = self.service(ManageLifecycleNodes, context['namespace']+'/lifecycle_manager_navigation/manage_nodes',
            ManageLifecycleNodes.Request(command=ManageLifecycleNodes.Request.PAUSE), min(5, self.remaining()))
        self.wait(pause.done, self.remaining(), require_fresh=True)
        if pause.result().success is not True:
            raise RuntimeError('navigation manager pause refused')
        context['manager_paused'] = True
        state = self.service(GetState, context['namespace']+'/velocity_smoother/get_state', GetState.Request(),
                             min(5, self.remaining()))
        self.wait(state.done, self.remaining(), require_fresh=True)
        context['smoother_state'] = state.result().current_state.id
        if context['smoother_state'] != 2:
            raise RuntimeError('velocity producer remains active')
        emit('native_lane_closed', stage=stage, goal_id=kwargs['goal_id'], **context)
