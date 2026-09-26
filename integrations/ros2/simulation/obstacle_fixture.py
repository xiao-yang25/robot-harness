"""Insert a real obstacle after motion; relay scans without deciding task authority."""
import json
import math
import os
from pathlib import Path
import time
import subprocess

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from gazebo_msgs.srv import SpawnEntity, DeleteEntity
from gazebo_pose import query_pose
from nav_msgs.msg import Odometry
from sensor_msgs.msg import LaserScan

SDF = """<sdf version="1.6"><model name="m4_corridor_obstacle"><static>true</static>
<pose>0 0 0 0 0 0</pose><link name="box">
<collision name="collision"><geometry><box><size>0.4 0.8 1</size></box></geometry></collision>
<visual name="visual"><geometry><box><size>0.4 0.8 1</size></box></geometry>
<material><ambient>0.8 0.15 0.05 1</ambient></material></visual>
</link></model></sdf>"""


class ObstacleFixture(Node):
    def __init__(self):
        super().__init__('obstacle_fixture')
        self.mode = os.environ['M4_CONTEXT_CASE']
        self.waiting_at = None
        self.remove_future = None
        self.removed = False
        self.remove_client = self.create_client(DeleteEntity, '/delete_entity')
        self.started = None
        self.future = None
        self.spawned = False
        self.moving = False
        self.last_scan = None
        self.frozen = None
        self.client = self.create_client(SpawnEntity, '/spawn_entity')
        self.publisher = self.create_publisher(LaserScan, '/owner_scan', qos_profile_sensor_data)
        self.scan_sub = self.create_subscription(LaserScan, '/scan', self.scan, qos_profile_sensor_data)
        self.odom_sub = self.create_subscription(Odometry, '/odom', self.odom, qos_profile_sensor_data)
        self.timer = self.create_timer(.05, self.tick)

    def scan(self, message):
        self.last_scan = message
        self.publisher.publish(self.frozen if self.frozen is not None else message)

    def odom(self, message):
        p = message.pose.pose.position
        self.moving = (message.header.frame_id == 'odom' and
                       math.hypot(p.x + 2, p.y + .5) >= .2 and
                       message.twist.twist.linear.x >= .05)

    def tick(self):
        if self.spawned:
            if self.mode.startswith('obstacle-resume'):
                self.remove_after_waiting()
            return
        if self.future is None:
            if not self.moving or self.last_scan is None or not self.client.service_is_ready():
                return
            request = SpawnEntity.Request()
            request.name = 'm4_corridor_obstacle'
            request.xml = SDF
            request.reference_frame = 'world'
            request.initial_pose.position.x = -.5
            request.initial_pose.position.y = -.5
            request.initial_pose.position.z = .5
            request.initial_pose.orientation.w = 1.
            if os.environ['M4_CONTEXT_CASE'] == 'obstacle-wait-frozen-scan':
                self.frozen = self.last_scan
            self.started = time.monotonic()
            self.future = self.client.call_async(request)
            print(json.dumps(dict(event='obstacle_spawn_requested', steady_ns=time.monotonic_ns(),
                                  frozen_scan=self.frozen is not None,
                                  frozen_scan_stamp_ns=(self.frozen.header.stamp.sec * 1000000000 +
                                                        self.frozen.header.stamp.nanosec)
                                  if self.frozen is not None else None)), flush=True)
        elif self.future.done():
            response = self.future.result()
            if not response.success:
                raise RuntimeError('obstacle spawn failed: ' + response.status_message)
            self.spawned = True
            print(json.dumps(dict(event='obstacle_spawned', steady_ns=time.monotonic_ns(),
                                  position=[-.5, -.5, .5], size=[.4, .8, 1.])), flush=True)
        elif time.monotonic() - self.started > 10:
            raise RuntimeError('obstacle spawn response timed out')

    def remove_after_waiting(self):
        if self.removed:
            return
        if self.waiting_at is None:
            owner = Path('/output/owner.jsonl')
            if not owner.exists():
                return
            # The fixture observes the stopped boundary only to schedule a world
            # change. Owner never consumes this removal response as clearance.
            events = [json.loads(line) for line in owner.read_text().splitlines()
                      if line.startswith('{') and line.endswith('}')]
            if not any(r['event'] == 'obstacle_waiting' for r in events):
                return
            self.waiting_at = time.monotonic()
            pose = query_pose('obstacle_waiting')
            box = subprocess.run(['gz', 'model', '-m', 'm4_corridor_obstacle', '-p'],
                                 check=True, text=True, capture_output=True, timeout=5)
            box_pose = [float(value) for value in box.stdout.split()]
            if len(box_pose) != 6 or math.dist(box_pose[:3], [-.5, -.5, .5]) >= .01:
                raise RuntimeError('physical obstacle pose missing at waiting boundary')
            print(json.dumps(dict(event='obstacle_waiting_pose', xyz_rpy=pose, box_pose=box_pose,
                                  steady_ns=time.monotonic_ns())), flush=True)
        if self.remove_future is None:
            if time.monotonic() - self.waiting_at < 2 or not self.remove_client.service_is_ready():
                return
            if self.mode == 'obstacle-resume-frozen-scan':
                self.frozen = self.last_scan
            request = DeleteEntity.Request()
            request.name = 'm4_corridor_obstacle'
            self.started = time.monotonic()
            self.remove_future = self.remove_client.call_async(request)
            print(json.dumps(dict(event='obstacle_remove_requested', steady_ns=time.monotonic_ns(),
                                  frozen_scan_stamp_ns=(self.frozen.header.stamp.sec * 1000000000 +
                                                        self.frozen.header.stamp.nanosec)
                                  if self.frozen is not None else None)), flush=True)
        elif self.remove_future.done():
            response = self.remove_future.result()
            if not response.success:
                raise RuntimeError('obstacle removal failed: ' + response.status_message)
            self.removed = True
            print(json.dumps(dict(event='obstacle_removed', steady_ns=time.monotonic_ns())), flush=True)
        elif time.monotonic() - self.started > 10:
            raise RuntimeError('obstacle removal response timed out')


if __name__ == '__main__':
    if os.environ.get('M4_ISOLATED_SIMULATION') != '1' or not Path('/.dockerenv').exists():
        raise SystemExit(2)
    rclpy.init()
    node = ObstacleFixture()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
