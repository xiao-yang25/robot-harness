"""ROS observations and native visits for the isolated scoped Nav2 profile."""
import json
from collections import deque
import math
import os
from pathlib import Path
import time
import uuid

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from rclpy.time import Time
from rclpy.duration import Duration
from geometry_msgs.msg import PoseWithCovarianceStamped
from lifecycle_msgs.srv import GetState
from nav2_msgs.action import NavigateToPose
from nav_msgs.msg import Odometry
from rosgraph_msgs.msg import Clock
from sensor_msgs.msg import LaserScan
from tf2_ros import Buffer, TransformListener, TransformException
from unique_identifier_msgs.msg import UUID

from ._predicates import GOALS, observation_valid, stream_at_clock

OUT = Path('/output')


def emit(event, **fields):
    row = dict(event=event, steady=time.monotonic(), **fields)
    with (OUT / 'caller.jsonl').open('a') as stream:
        stream.write(json.dumps(row, allow_nan=False) + '\n')


def diagnose(event, **fields):
    """Diagnostic storage must not interrupt failure cleanup."""
    try:
        emit(event, **fields)
    except Exception:
        pass


def seconds(stamp):
    return stamp.sec + stamp.nanosec * 1e-9


class Observations(Node):
    def __init__(self):
        super().__init__('robot_harness_nav2', parameter_overrides=[
            rclpy.parameter.Parameter('use_sim_time', value=True)])
        self.clock_sample = (0.0, 0.0)
        self.streams = {}
        self.histories = {name: deque(maxlen=16) for name in ('odom', 'scan')}
        self.pose_count = 0
        self.localization_ok = False
        self.deadline = time.monotonic() + 290
        self.client = ActionClient(self, NavigateToPose, '/navigate_to_pose')
        self.active = None
        self.tf = Buffer()
        self.listener = TransformListener(self.tf, self)
        self.subscriptions_keep = [
            self.create_subscription(Clock, '/clock', self.clock, qos_profile_sensor_data),
            self.create_subscription(Odometry, '/odom', lambda m: self.sample('odom', m), qos_profile_sensor_data),
            self.create_subscription(LaserScan, '/scan', lambda m: self.sample('scan', m), qos_profile_sensor_data),
            self.create_subscription(PoseWithCovarianceStamped, '/amcl_pose', self.localization, qos_profile_sensor_data)]

    def clock(self, message):
        value = seconds(message.clock)
        old = self.clock_sample[0]
        if value < old:
            raise RuntimeError('simulation clock reset')
        if value > old:
            self.clock_sample = (value, time.monotonic())

    def sample(self, name, message):
        stamp, now = seconds(message.header.stamp), time.monotonic()
        old = self.streams.get(name, (0.0, 0.0, 0.0))
        if stamp > old[0]:
            self.histories[name].append((stamp, now))
        self.streams[name] = (max(stamp, old[0]), now,
                              now if stamp > old[0] else old[2])

    def localization(self, message):
        covariance = message.pose.covariance
        self.localization_ok = bool(message.header.frame_id == 'map'
                and all(math.isfinite(v) for v in covariance)
                and 0 <= covariance[0] <= 0.25 and 0 <= covariance[7] <= 0.25)
        if self.localization_ok:
            self.pose_count += 1

    def observation(self):
        now = time.monotonic()
        transform = self.tf.lookup_transform('map', 'base_link', Time(), timeout=Duration(seconds=0))
        clock = self.clock_sample[0]
        if seconds(transform.header.stamp) > clock:
            # Lookup interpolates actual retained transforms at the observed clock;
            # do not overwrite the stamp or widen the allowed age for future data.
            transform = self.tf.lookup_transform('map', 'base_link',
                         Time(nanoseconds=round(clock * 1e9)), timeout=Duration(seconds=0))
        return dict(clock=self.clock_sample[0], clock_age=now-self.clock_sample[1],
                    streams=[stream_at_clock(self.histories[k], clock, now,
                             self.streams.get(k, (0, 0, 0))[2]) for k in ('odom', 'scan')],
                    pose=[transform.transform.translation.x, transform.transform.translation.y],
                    pose_stamp=seconds(transform.header.stamp))

    def tick(self):
        if time.monotonic() >= self.deadline:
            raise TimeoutError('caller total deadline')
        rclpy.spin_once(self, timeout_sec=0.05)

    def wait(self, predicate, budget, require_fresh=False):
        deadline = min(self.deadline, time.monotonic() + budget)
        while True:
            self.tick()
            if require_fresh and not self.valid_observation():
                raise RuntimeError('navigation input expired')
            if predicate():
                return
            if time.monotonic() >= deadline:
                raise TimeoutError('native wait budget expired')

    def ready(self):
        startup_deadline = time.monotonic() + 70
        self.wait(lambda: self.pose_count > 0 and self.client.server_is_ready()
                  and self.valid_observation(), 70)
        for name in ('amcl', 'bt_navigator'):
            service = self.create_client(GetState, f'/{name}/get_state')
            try:
                while True:
                    remaining = startup_deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError(name + ' not active within startup budget')
                    self.wait(service.service_is_ready, min(5, remaining))
                    future = service.call_async(GetState.Request())
                    self.wait(future.done, min(5, startup_deadline-time.monotonic()))
                    state = future.result().current_state.id
                    emit('startup_state', node=name, state=state)
                    if state == 3:
                        break
                    next_poll = time.monotonic() + 0.25
                    self.wait(lambda: time.monotonic() >= next_poll,
                              min(1, startup_deadline-time.monotonic()))
            finally:
                self.destroy_client(service)
        emit('startup_observation', observation=self.observation(),
             localization_ok=self.localization_ok)
        self.wait(self.valid_observation, startup_deadline-time.monotonic())
        emit('ready', observation=self.observation())

    def valid_observation(self):
        try:
            return self.localization_ok and observation_valid(**self.observation())
        except TransformException:
            return False

    def visit(self, stage):
        target = tuple(self.execution.active['target'][:2])
        goal_id = uuid.uuid4().hex
        message = NavigateToPose.Goal()
        message.pose.header.frame_id = 'map'
        message.pose.header.stamp = self.get_clock().now().to_msg()
        message.pose.pose.position.x, message.pose.pose.position.y = target
        message.pose.pose.orientation.w = 1.0

        def feedback(message):
            actual = bytes(message.goal_id.uuid).hex()
            if actual != goal_id:
                raise RuntimeError('feedback goal identity mismatch')
            # Feedback is sampled in the record, not used as a completion predicate.
            if time.monotonic() - getattr(feedback, 'last', 0) >= 1:
                emit('progress', stage=stage, goal_id=actual,
                     distance_remaining=message.feedback.distance_remaining)
                feedback.last = time.monotonic()

        if not self.valid_observation():
            raise RuntimeError('invalid input before goal dispatch')
        future = self.client.send_goal_async(message, feedback_callback=feedback,
                                            goal_uuid=UUID(uuid=list(bytes.fromhex(goal_id))))
        emit('goal_sent', stage=stage, goal_id=goal_id, target=target)
        self.wait(future.done, 5, require_fresh=True)
        self.active = future.result()
        if not self.active.accepted or bytes(self.active.goal_id.uuid).hex() != goal_id:
            raise RuntimeError('native goal rejected or mismatched')
        result_future = self.active.get_result_async()
        self.wait(result_future.done, 110, require_fresh=True)
        result = result_future.result()
        result_clock = self.clock_sample[0]
        result_tf_stamp = seconds(self.tf.lookup_transform('map', 'base_link', Time(),
                                   timeout=Duration(seconds=0)).header.stamp)
        emit('native_result', stage=stage, goal_id=goal_id, status=result.status,
             result_clock=result_clock, result_tf_stamp=result_tf_stamp)
        if result.status != 4:
            raise RuntimeError('native result unsuccessful')
        self.active = None
        self.wait(lambda: self.observation()['pose_stamp'] > max(result_clock, result_tf_stamp),
                  5, require_fresh=True)
        observation = self.observation()
        if math.hypot(observation['pose'][0]-target[0], observation['pose'][1]-target[1]) > 0.35:
            raise RuntimeError('fresh localization outside target')
        emit('arrival', stage=stage, goal_id=goal_id, status=result.status,
             result_clock=result_clock, result_tf_stamp=result_tf_stamp, observation=observation)
