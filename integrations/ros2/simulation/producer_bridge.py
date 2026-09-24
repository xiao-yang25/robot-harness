#!/usr/bin/env python3
"""Fixed producer-channel identity, never a mutable current-generation relay."""
import json
import os
from pathlib import Path
import re
import time

import rclpy
from geometry_msgs.msg import Twist
from m4_drive_probe.msg import ScopedTwist
from rclpy.executors import SingleThreadedExecutor
from rclpy.node import Node


def contexts():
    data = json.loads(Path('/output/producer-context.json').read_text())
    scopes = [data['a_scope'], data['b_scope']]
    if (any(not isinstance(s, str) or not re.fullmatch('[0-9a-f]{32}', s)
            or s == '0' * 32 for s in scopes) or scopes[0] == scopes[1]):
        raise RuntimeError('invalid or reused producer channel identity')
    return scopes


class Bridge(Node):
    def __init__(self, scope, generation):
        super().__init__('producer_bridge_' + str(generation))
        topic = '/m4_motion/s_' + scope + '/smoothed'
        publisher = self.create_publisher(ScopedTwist, '/scoped_cmd_vel', 10)
        # These closure bindings never change. Each subscription is permanently
        # attached to one producer channel, even if another generation opens.
        def forward(message):
            command = ScopedTwist(scope_id=scope, generation=generation, twist=message)
            publisher.publish(command)
            print(json.dumps(dict(event='producer_command_forwarded', scope_id=scope,
                                  generation=generation, input_topic=topic,
                                  linear_x=message.linear.x, angular_z=message.angular.z,
                                  steady_ns=time.monotonic_ns())), flush=True)
        self.create_subscription(Twist, topic, forward, 10)
        print(json.dumps(dict(event='producer_binding', scope_id=scope,
                              generation=generation, input_topic=topic)), flush=True)


def main():
    if os.environ.get('M4_ISOLATED_SIMULATION') != '1' or not Path('/.dockerenv').exists():
        return 2
    scopes = contexts()
    rclpy.init()
    executor = SingleThreadedExecutor()
    nodes = [Bridge(scope, i + 1) for i, scope in enumerate(scopes)]
    for node in nodes:
        executor.add_node(node)
    try:
        executor.spin()
    finally:
        executor.shutdown()
        for node in nodes:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
