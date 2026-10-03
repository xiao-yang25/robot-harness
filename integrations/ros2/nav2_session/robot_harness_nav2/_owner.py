"""Core authority and request ownership for the bounded Nav2 profile."""
import json
import time
from pathlib import Path

from ._scoped import ScopedDriver, request_fields, identity
from ._observations import OUT, emit, diagnose
from ._predicates import GOALS
from ._profile import PROFILE_ID
from ._native_lane import ObservedClient
from ._requests import RequestEndpoint
from robot_harness.navigation import NavigationRequests
from robot_harness import _core
from robot_harness._execution import ExecutionCoordinator, accepted
from lifecycle_msgs.srv import GetState
from m4_drive_probe.srv import CloseScopedMotion
from std_srvs.srv import Trigger
import rclpy


class ClientClosed(RuntimeError):
    """Public cancel/close/EOF interrupted the owner loop."""


class NavigationOwner(ScopedDriver):
    def __init__(self):
        self.requests = self.endpoint = None
        self.stop_requested = self.completed = False
        self.stage_index = 0
        self.gate = self.execution = None
        self.pending = []
        self.aborting = False
        self.capability_at = 0
        super().__init__()

    def prepare_b_initial(self):
        super().prepare_b_initial()
        # Startup observations belong to this exclusive, sealed, unsent scene.
        if (self.motion_started or self.active is not None or not self.odometry
                or self.odometry[-1]['speed'] > .01 or abs(self.odometry[-1]['angular_speed']) > .02
                or not self.valid_observation()):
            raise RuntimeError('initial binding not idle and observed')
        self.gate = _core.Gate('scoped-nav2', 'exclusive-waffle-drive', 'navigation.visit',
                              PROFILE_ID, 1, time.monotonic_ns(),
                              settlement_scope='native-outlet-quiet-and-next-context')
        self.execution = ExecutionCoordinator(self.gate, time.monotonic_ns)
        for kind in ('idle', 'worker', 'sink'):
            accepted(self.gate.startup(kind, True, time.monotonic_ns()))
        self.refresh_capability()
        self.client = ObservedClient(self.a_client, self)
        self.b_observed_client = ObservedClient(self.b_client, self)
        emit('core_ready', profile=PROFILE_ID)

        self.requests = NavigationRequests(self.execution,
            sites={stage: (*target, 0.0) for stage, target in GOALS.items()},
            profile=PROFILE_ID, map_id='turtlebot3-world-v1',
            observation=self.public_observation,
            ready=lambda: not self.aborting and self.stage_index < 2
                and self.context_process.poll() is None
                and (self.a_client if self.stage_index == 0 else self.b_client).server_is_ready(),
            context=self.next_context, request_stop=self.request_stop)
        self.endpoint = RequestEndpoint(self.requests, self.request_event)
        (OUT/'navigation-endpoint.json').write_text(json.dumps(dict(endpoint=self.endpoint.path)))
        emit('request_endpoint_ready')

    def public_observation(self):
        row = self.observation()
        return dict(epoch=0, map_id='turtlebot3-world-v1', frame='map',
                    valid=self.valid_observation(), pose=row['pose'],
                    sample_sim_seconds=row['pose_stamp'], sensor_health=dict(
                        localization=self.localization_ok, clock_age=row['clock_age'],
                        streams=row['streams']))

    def next_context(self):
        stage = ('A', 'B')[self.stage_index]
        return dict(stage=stage, **request_fields(self.contexts[stage]))

    def request_stop(self, record):
        # No ROS wait in the public intent handler. tick initiates abort cleanup.
        self.stop_requested = True

    def request_event(self, message, result):
        if (message.get('command') == 'cancel' and message.get('request_id') == 'A'
                and not result['affected_current'] and self.stage_index == 1):
            emit('old_core_cancel', stage='A', affected_current=False)

    def refresh_capability(self):
        stamp = time.monotonic_ns()
        accepted(self.gate.capabilities(True, stamp, stamp+60_000_000_000))
        self.capability_at = stamp+30_000_000_000

    def tick(self):
        # Every ROS/service/cleanup wait progresses the same owner; no new thread.
        super().tick()
        if self.endpoint is not None:
            self.endpoint.pump()
        # Native sequence completion leaves the final Core record pending.
        # Explicit cancellation still needs client shutdown handling; a normal
        # close already received may finish the existing terminal wait instead.
        closing = self.requests is not None and self.requests.closing
        if (self.stop_requested or closing) and not self.aborting and not (self.completed and closing):
            raise ClientClosed("client cancellation or shutdown requested")
        if self.execution is None or self.execution.active is None:
            return
        receipt = self.execution.tick()
        if self.completed and self.requests.closing:
            return
        if self.aborting:
            self.cancel_pending()
            return
        if (receipt['authority_disposition'] != 'current' or not self.valid_observation()
                or self.context_process.poll() is not None):
            self.aborting = True
            self.execution.cancel(self.execution.active)
            raise RuntimeError('Core authority or required navigation observation lost')
        if time.monotonic_ns() >= self.capability_at:
            self.refresh_capability()

    def open_scope(self, stage):
        record = self.execution.active
        if (self.aborting or record is None or record['stage'] != stage
                or not self.valid_observation() or not self.execution.dispatch(record)):
            raise RuntimeError('prepared request lost dispatch permission')
        if stage == 'B':
            self.client = self.b_observed_client
        emit('core_admitted', stage=stage, record=self.execution.snapshot(record))
        super().open_scope(stage)

    def before_send(self, goal_id):
        record = self.execution.active
        if self.aborting or record is None or not identity(goal_id) or 'goal_id' in record:
            raise RuntimeError('no current permission for native submission')
        record['goal_id'] = goal_id
        pending = dict(record=record, goal_id=goal_id, terminal=False)
        self.pending.append(pending)  # Retain identity before possible transmission.
        emit('core_native_reserved', stage=record['stage'], goal_id=goal_id,
             operation_id=record['operation_id'])
        return pending

    def permit_send(self, pending):
        if (self.aborting or pending['record'] is not self.execution.active
                or not self.valid_observation() or not self.gate.supports(1, time.monotonic_ns())):
            raise RuntimeError('native submission prerequisites lost')
        # Observation lookup/logging precede this final Core time observation.
        if self.execution.tick()['authority_disposition'] != 'current':
            raise RuntimeError('native submission authority lost')

    def fresh_b_ready(self):
        # Initial preparation is not enough at the later reuse boundary.
        namespace = self.contexts['B']['namespace']
        for name in ('bt_navigator', 'planner_server', 'controller_server', 'velocity_smoother'):
            future = self.service(GetState, namespace+'/'+name+'/get_state', GetState.Request())
            self.wait(future.done, 5, require_fresh=True)
            if future.result().current_state.id != 3:
                raise RuntimeError('B no longer active')
        for name in ('controller_transform_ready', 'planner_map_ready'):
            future = self.service(Trigger, namespace+'/'+name, Trigger.Request())
            self.wait(future.done, 5, require_fresh=True)
            if future.result().success is not True:
                raise RuntimeError('B native readiness lost')
        if (not self.b_client.server_is_ready() or self.context_process.poll() is not None
                or not self.valid_observation()):
            raise RuntimeError('B not currently ready')
        emit('core_next_ready', stage='B', observation=self.observation())
        # Recording may block. Do not carry a pre-log readiness observation
        # across that boundary into the actual result/settlement calls.
        if (not self.valid_observation() or self.context_process.poll() is not None
                or not self.b_client.server_is_ready()
                or self.execution.tick()['authority_disposition'] != 'current'):
            raise RuntimeError('B readiness or authority lost before settlement')

    def close_visit(self, stage, **kwargs):
        super().close_visit(stage, **kwargs)
        # The inherited driver has checked correlated native/outlet closure and
        # fresh quiet samples. Core does not independently measure those facts.
        record = self.execution.active
        candidate = dict(stage=stage, goal_id=record['goal_id'], observation=self.observation())
        if stage == 'A':
            self.fresh_b_ready()
        self.execution.dispose_result(record, candidate, self.observation)
        if stage == 'A':
            self.execution.settle(record)
            self.execution.release(record)
        else:
            # No prepared C: keep the aggregate operation occupied and unresolved.
            accepted(self.gate.capabilities(False, time.monotonic_ns(), time.monotonic_ns()+60_000_000_000))
        emit('core_visit_closed', stage=stage, record=self.execution.snapshot(record),
             permits_next=stage == 'A')
        self.stage_index += 1

    def cancel_pending(self):
        for pending in self.pending:
            future = pending.get('future')
            if (future is not None and future.done() and 'handle' not in pending
                    and not pending.get('fact_error')):
                try:
                    future.result()
                except Exception as error:
                    pending['fact_error'] = True
                    diagnose('cleanup_native_fact_error', error=str(error), goal_id=pending['goal_id'])
            handle = pending.get('handle')
            if (handle is not None and handle.accepted and not pending['terminal']
                    and bytes(handle.goal_id.uuid).hex() == pending['goal_id']
                    and not pending.get('cancel_attempted')):
                pending['cancel_attempted'] = True
                try:
                    pending['cancel_future'] = handle.cancel_goal_async()
                    diagnose('cleanup_native_cancel_requested', goal_id=pending['goal_id'])
                except Exception as error:
                    diagnose('cleanup_native_cancel_error', error=str(error), goal_id=pending['goal_id'])

    def abort(self):
        self.aborting = True
        if self.execution is not None and self.execution.active is not None:
            self.execution.cancel(self.execution.active)
        # Start cancellation and the exact outlet close without awaiting either.
        try:
            self.cancel_pending()
        except Exception as error:
            diagnose('cleanup_native_cancel_error', error=str(error), stop='unknown')
        future = None
        try:
            if self.current is not None:
                context = self.current
                context.setdefault('close_requested_ns', time.monotonic_ns())
                client = self.scoped_clients.get('/close_scoped_motion')
                if client is None:
                    client = self.create_client(CloseScopedMotion, '/close_scoped_motion')
                    self.scoped_clients['/close_scoped_motion'] = client
                if client.service_is_ready():
                    future = client.call_async(CloseScopedMotion.Request(**request_fields(context)))
        except Exception as error:
            diagnose('cleanup_outlet_error', error=str(error), stop='unknown')
        until = time.monotonic()+2
        while time.monotonic() < until:
            try:
                rclpy.spin_once(self, timeout_sec=.05)
                if self.execution is not None:
                    self.execution.tick()
            except Exception as error:
                diagnose('cleanup_progress_error', error=str(error), stop='unknown')
            self.cancel_pending()
        diagnose('core_abort', stop='unknown', outlet_response_received=future is not None and future.done(),
             record=None if self.execution is None or self.execution.active is None else
             self.execution.snapshot(self.execution.active))
