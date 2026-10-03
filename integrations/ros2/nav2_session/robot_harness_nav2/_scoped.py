"""Scoped native context and outlet closure for the bounded simulation profile."""
from collections import deque
import json
import math
from pathlib import Path
import subprocess
import time

from ._observations import Observations, OUT, emit, seconds
from ._predicates import quiet_window
import rclpy
from rclpy.action import ActionClient
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from lifecycle_msgs.srv import GetState
from nav2_msgs.srv import ManageLifecycleNodes
from m4_drive_probe.srv import OpenMotionScope, CloseScopedMotion
from nav2_msgs.action import NavigateToPose
from std_msgs.msg import String
from std_srvs.srv import Trigger


def identity(value):
    return (isinstance(value, str) and len(value) == 32 and value != '0'*32
            and all(c in '0123456789abcdef' for c in value))


class ScopedDriver(Observations):
    def __init__(self):
        self.odometry = deque(maxlen=512)
        self.context_process = self.context_log = self.b_client = None
        self.current = None
        self.scoped_clients = {}
        super().__init__()
        self.a_client = self.client
        self.deadline = time.monotonic()+380
        scopes = json.loads((OUT/'producer-context.json').read_text())
        self.contexts = {}
        self.context_process = self.context_log = None
        self.b_client = None
        self.motion_started = False
        self.scoped_clients = {}
        qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                         durability=DurabilityPolicy.TRANSIENT_LOCAL)
        for stage, scope, generation in zip(('A', 'B'), (scopes['a_scope'], scopes['b_scope']), (1, 2)):
            if not identity(scope):
                raise ValueError('invalid producer scope')
            namespace = '' if stage == 'A' else '/m4_task/s_'+scope
            context = dict(scope_id=scope, generation=generation, namespace=namespace,
                           children={action: {} for action in ('compute_path_to_pose', 'follow_path')})
            self.contexts[stage] = context
            for action, topic in (('compute_path_to_pose', 'navigation_planner_link'),
                                  ('follow_path', 'navigation_child_link')):
                for kind, suffix in (('link', topic), ('ack', action+'_worker_ack')):
                    self.subscriptions_keep.append(self.create_subscription(String,
                        namespace+'/'+suffix,
                        lambda m, c=context, a=action, k=kind: self.child(c, a, k, m), qos))
        if scopes['a_scope'] == scopes['b_scope']:
            raise ValueError('producer scope reused')
        self.subscriptions_keep.append(self.create_subscription(String, '/motion_scope_ack',
                                        self.drive_ack, qos))
        self.current = None

    def sample(self, name, message):
        super().sample(name, message)
        if name != 'odom':
            return
        stamp = seconds(message.header.stamp)
        if self.odometry and stamp <= self.odometry[-1]['stamp']:
            return
        p, q, v = message.pose.pose.position, message.pose.pose.orientation, message.twist.twist
        if (message.header.frame_id != 'odom'
                or message.child_frame_id not in ('base_footprint', 'base_link')
                or not all(math.isfinite(x) for x in (p.x, p.y, q.x, q.y, q.z, q.w,
                                                      v.linear.x, v.linear.y, v.angular.z))
                or abs(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w-1) > .01):
            raise ValueError('invalid planar odometry')
        self.odometry.append(dict(stamp=stamp, received=time.monotonic(), x=p.x, y=p.y,
            yaw=math.atan2(2*(q.w*q.z+q.x*q.y), 1-2*(q.y*q.y+q.z*q.z)),
            speed=math.hypot(v.linear.x, v.linear.y), angular_speed=v.angular.z))

    def child(self, context, action, kind, message):
        if len(message.data) > 4096:
            raise ValueError('oversized closure fact')
        row = json.loads(message.data)
        if kind == 'link':
            if (row['event'] != 'navigation_child_link' or row['scope_id'] != context['scope_id']
                    or row['action'] != action or not identity(row['child_uuid'])):
                raise ValueError('child link mismatch')
        elif (row['event'] != action+'_worker_ack' or not identity(row['goal_uuid'])
              or row['worker_future_ready'] is not True or row['endpoint_sealed'] is not True):
            raise ValueError('worker closure mismatch')
        child = context['children'][action]
        if kind in child and child[kind] != row:
            raise ValueError('changed native identity/fact')
        child[kind] = row
        if 'link' in child and 'ack' in child and child['link']['child_uuid'] != child['ack']['goal_uuid']:
            raise ValueError('child/worker identity mismatch')
        emit('child_fact', scope_id=context['scope_id'], action=action, kind=kind, fact=row)

    def drive_ack(self, message):
        if len(message.data) > 4096:
            raise ValueError('oversized drive fact')
        row = json.loads(message.data)
        context = next((c for c in self.contexts.values() if c['scope_id'] == row['scope_id']), None)
        if context is None or not context.get('close_requested_ns'):
            raise ValueError('unexpected drive acknowledgement')
        received = time.monotonic_ns()
        if (row['generation'] != context['generation'] or row['event'] != 'motion_scope_applied'
                or row['sealed'] is not True or row['wheel_targets_zero'] is not True
                or row['update_sequence'] <= 0 or row['sim_ns'] <= 0
                or not context['close_requested_ns'] <= row['steady_ns'] <= received):
            raise ValueError('uncorrelated drive acknowledgement')
        if 'drive_ack' in context:
            if context['drive_ack'] != row:
                raise ValueError('drive application changed')
            return
        context.update(drive_ack=row, ack_received_ns=received,
                       odom_at_ack=self.streams.get('odom', (0, 0, 0))[0])
        emit('drive_applied', scope_id=context['scope_id'], fact=row,
             odom_at_ack=context['odom_at_ack'])

    def service(self, kind, name, request, budget=5, fresh=True):
        if name not in self.scoped_clients:
            self.scoped_clients[name] = self.create_client(kind, name)
        client = self.scoped_clients[name]
        self.wait(client.service_is_ready, budget, require_fresh=fresh)
        return client.call_async(request)

    def prepare_b_initial(self):
        # Initial readiness may wait for fresh input while every motion outlet is
        # still sealed. Once any opening has been attempted, expiry stays fatal.
        if self.motion_started or self.b_client is not None:
            raise RuntimeError('B preparation is only permitted before first motion admission')
        emit('context_prepare_requested', stage='B', scope_id=self.contexts['B']['scope_id'],
             observation=self.observation())
        (OUT/'b').mkdir()
        self.context_log = (OUT/'b'/'launch.log').open('w')
        self.context_process = subprocess.Popen(['ros2', 'launch',
            '/simulation/native_worker/task_context.launch.py', 'use_sim_time:=True',
            'use_respawn:=False', 'use_composition:=False', 'params_file:=/output/b-params.yaml'],
            stdout=self.context_log, stderr=subprocess.STDOUT)
        namespace = self.contexts['B']['namespace']
        self.b_client = ActionClient(self, NavigateToPose, namespace+'/navigate_to_pose')
        ready_until = time.monotonic()+70
        for name in ('bt_navigator', 'planner_server', 'controller_server', 'velocity_smoother'):
            while True:
                remaining = ready_until-time.monotonic()
                if remaining <= 0 or self.context_process.poll() is not None:
                    raise RuntimeError('B context did not become ready')
                future = self.service(GetState, namespace+'/'+name+'/get_state', GetState.Request(), min(5, remaining), fresh=False)
                self.wait(future.done, min(5, ready_until-time.monotonic()))
                if future.result().current_state.id == 3:
                    break
                pause_until = time.monotonic()+.2
                self.wait(lambda: time.monotonic() >= pause_until, min(1, ready_until-time.monotonic()))
        for name in ('controller_transform_ready', 'planner_map_ready'):
            future = self.service(Trigger, namespace+'/'+name, Trigger.Request(),
                                  min(5, ready_until-time.monotonic()), fresh=False)
            self.wait(future.done, min(5, ready_until-time.monotonic()))
            if future.result().success is not True:
                raise RuntimeError('B native readiness unavailable')
        self.wait(self.b_client.server_is_ready, min(5, ready_until-time.monotonic()))
        self.wait(self.valid_observation, ready_until-time.monotonic())
        emit('context_ready', stage='B', scope_id=self.contexts['B']['scope_id'],
             observation=self.observation())

    def open_scope(self, stage):
        self.motion_started = True
        context = self.contexts[stage]
        self.current = context
        request = OpenMotionScope.Request(expected_generation=context['generation']-1,
                                         scope_id=context['scope_id'])
        emit('scope_open_requested', stage=stage, **request_fields(context))
        future = self.service(OpenMotionScope, '/open_motion_scope', request)
        self.wait(future.done, 5, require_fresh=True)
        response = future.result()
        if response.accepted is not True or response.generation != context['generation']:
            raise RuntimeError('scope opening refused')
        emit('scope_opened', stage=stage, **request_fields(context))

    def close_lane(self, stage, *, goal_id, close_until, fresh=True):
        context = self.contexts[stage]
        if not context.get('close_requested_ns'):
            context['close_requested_ns'] = time.monotonic_ns()
        request = CloseScopedMotion.Request(**request_fields(context))
        future = self.service(CloseScopedMotion, '/close_scoped_motion', request,
                              min(5, close_until-time.monotonic()), fresh=False)
        self.wait(future.done, min(5, close_until-time.monotonic()), require_fresh=fresh)
        if future.result().accepted is not True:
            raise RuntimeError('drive close refused')
        emit('drive_close_response', stage=stage, accepted=True, **request_fields(context))
        self.wait(lambda: all(set(c) == {'link', 'ack'} for c in context['children'].values()),
                  close_until-time.monotonic(), require_fresh=fresh)
        context['bt_requested_ns'] = time.monotonic_ns()
        bt = self.service(Trigger, context['namespace']+'/close_navigation_scope', Trigger.Request(),
                          min(5, close_until-time.monotonic()), fresh=fresh)
        self.wait(bt.done, close_until-time.monotonic(), require_fresh=fresh)
        context['bt_received_ns'] = time.monotonic_ns()
        if bt.result().success is not True:
            raise RuntimeError('native lane close refused')
        context['bt'] = json.loads(bt.result().message)
        fact = context['bt']
        if (fact['event'] != 'navigation_bt_closed' or fact['scope_id'] != context['scope_id']
                or not all(fact[k] is True for k in ('bt_worker_joined', 'endpoint_sealed', 'tree_idle'))
                or not context['bt_requested_ns'] <= fact['steady_ns'] <= context['bt_received_ns']):
            raise ValueError('BT closure correlation/ordering mismatch')
        # A direct child DEACTIVATE breaks the manager bond and can trigger its
        # automatic restart. Pause the owning navigation domain through Nav2;
        # localization belongs to a different manager and remains live.
        pause = self.service(ManageLifecycleNodes,
            context['namespace']+'/lifecycle_manager_navigation/manage_nodes',
            ManageLifecycleNodes.Request(command=ManageLifecycleNodes.Request.PAUSE),
            min(5, close_until-time.monotonic()), fresh=fresh)
        self.wait(lambda: pause.done() and 'drive_ack' in context,
                  close_until-time.monotonic(), require_fresh=fresh)
        if pause.result().success is not True:
            raise RuntimeError('navigation manager pause refused')
        context['manager_paused'] = True
        state = self.service(GetState, context['namespace']+'/velocity_smoother/get_state', GetState.Request(),
                             min(5, close_until-time.monotonic()), fresh=fresh)
        self.wait(state.done, close_until-time.monotonic(), require_fresh=fresh)
        context['smoother_state'] = state.result().current_state.id
        if context['smoother_state'] != 2:
            raise RuntimeError('velocity producer not inactive')
        emit('native_lane_closed', stage=stage, goal_id=goal_id, **context)

    def close_visit(self, stage, *, goal_id=None, close_until=None):
        context = self.contexts[stage]
        if goal_id is None:
            goal_id = self.execution.active['goal_id']
        if close_until is None:
            close_until = min(self.deadline, time.monotonic()+15)
        self.close_lane(stage, goal_id=goal_id, close_until=close_until)
        after = max(context['drive_ack']['sim_ns']/1e9, context['odom_at_ack'])
        sampled, last = [], max(after, self.odometry[-1]['stamp'] if self.odometry else 0)
        def quiet():
            nonlocal last
            now, clock = time.monotonic(), self.clock_sample[0]
            for raw in self.odometry:
                if not last < raw['stamp'] <= clock:
                    continue
                sampled.append({k: v for k, v in raw.items() if k != 'received'} | dict(
                    clock=clock, clock_age=now-self.clock_sample[1], receive_age=now-raw['received'],
                    advance_age=now-self.streams['odom'][2]))
                last = raw['stamp']
            return quiet_window(sampled, after=after) is not None
        self.wait(quiet, close_until-time.monotonic(), require_fresh=True)
        emit('visit_closed', stage=stage, goal_id=goal_id, quiet_after=after,
             quiet_samples=sampled, **context)
        self.current = None


def request_fields(context):
    return dict(scope_id=context['scope_id'], generation=context['generation'])
