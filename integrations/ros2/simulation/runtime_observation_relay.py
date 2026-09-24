"""Drop/replay only the Owner's observation channel; native Nav2 stays connected."""
import json
import math
import os
import signal
from pathlib import Path
import time

import rclpy
from nav_msgs.msg import Odometry
from rosgraph_msgs.msg import Clock
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data


class Relay(Node):
    def __init__(self, mode):
        super().__init__('runtime_observation_fault_relay')
        self.mode = mode
        self.fault_at = None
        self.restored = False
        self.last_odometry = None
        self.odom_out = self.create_publisher(Odometry, '/owner_odom', qos_profile_sensor_data)
        self.clock_out = self.create_publisher(Clock, '/owner_clock', qos_profile_sensor_data)
        self.create_subscription(Odometry, '/odom', self.odometry, qos_profile_sensor_data)
        self.create_subscription(Clock, '/clock', self.clock, qos_profile_sensor_data)
        self.create_timer(.05, self.tick)

    def record(self, event, **fields):
        print(json.dumps(dict(event=event, steady_ns=time.monotonic_ns(), **fields)), flush=True)

    def clock(self, message):
        if self.mode != 'runtime-clock-loss' or self.fault_at is None:
            self.clock_out.publish(message)

    def odometry(self, message):
        pose = message.pose.pose.position
        speed = math.hypot(message.twist.twist.linear.x, message.twist.twist.linear.y)
        distance = math.hypot(pose.x + 2, pose.y + .5)
        if self.fault_at is None and distance >= .5 and speed >= .05:
            if self.last_odometry is None:
                raise RuntimeError('no pre-fault observation')
            if self.mode.startswith('runtime-stop-unreachable'):
                # Isolated fixture: freeze the real server's cancel/result executor.
                # Controller, smoother, drive and simulator remain scheduled.
                candidates = []
                for process in Path('/proc').iterdir():
                    if not process.name.isdigit():
                        continue
                    try:
                        # Emulated binaries may expose the interpreter through /proc/exe.
                        # Match the exact fixture argv, not a truncated comm name.
                        argv = (process / 'cmdline').read_bytes().split(b'\0')
                        if b'/native/native_scope_navigator' in argv:
                            candidates.append(int(process.name))
                    except OSError:
                        continue
                if len(candidates) != 1:
                    raise RuntimeError('expected exactly one native navigator process')
                os.kill(candidates[0], signal.SIGSTOP)
                self.record('native_navigator_stop_requested', pid=candidates[0],
                            executable=str(Path('/proc', str(candidates[0]), 'exe').resolve()))
            self.fault_at = time.monotonic()
            last_stamp = self.last_odometry.header.stamp
            self.record('observation_fault_started', case=self.mode, displacement_m=distance,
                        speed_m_s=speed, last_forwarded_odom_ns=last_stamp.sec * 10**9 + last_stamp.nanosec)
        if self.fault_at is None or self.restored or self.mode == 'runtime-clock-loss':
            self.odom_out.publish(message)
            self.last_odometry = message

    def tick(self):
        if self.fault_at is None:
            return
        if self.mode == 'runtime-odometry-replay':
            self.odom_out.publish(self.last_odometry)
            stamp = self.last_odometry.header.stamp
            self.record('old_odometry_replayed', stamp_ns=stamp.sec * 10**9 + stamp.nanosec)
        if ((self.mode == 'runtime-odometry-resume' or self.mode.startswith('runtime-stop-unreachable')) and not self.restored
                and time.monotonic() - self.fault_at >= 6):
            self.restored = True
            self.record('observation_flow_restored')


def main():
    mode = os.environ.get('M4_CONTEXT_CASE')
    if (os.environ.get('M4_ISOLATED_SIMULATION') != '1' or not Path('/.dockerenv').exists()
            or mode not in ('runtime-odometry-loss', 'runtime-odometry-replay',
                            'runtime-odometry-resume', 'runtime-clock-loss',
                            'runtime-stop-unreachable', 'runtime-stop-unreachable-withhold-drive-ack')):
        return 2
    rclpy.init()
    node = Relay(mode)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
