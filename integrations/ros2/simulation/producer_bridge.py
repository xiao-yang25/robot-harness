#!/usr/bin/env python3
"""Fixed producer-channel identity, never a mutable current-generation relay."""
import json
import os
from pathlib import Path
import re
import signal
import time

import rclpy
from geometry_msgs.msg import Twist
from m4_drive_probe.msg import ScopedTwist
from rclpy.context import Context
from rclpy.executors import SingleThreadedExecutor
from rclpy.node import Node
from rclpy.signals import SignalHandlerOptions


def contexts():
    data = json.loads(Path('/output/producer-context.json').read_text())
    scopes = [data['a_scope'], data['b_scope']]
    if (any(not isinstance(s, str) or not re.fullmatch('[0-9a-f]{32}', s)
            or s == '0' * 32 for s in scopes) or scopes[0] == scopes[1]):
        raise RuntimeError('invalid or reused producer channel identity')
    return scopes


class Bridge(Node):
    def __init__(self, scope, generation, *, context=None):
        super().__init__('producer_bridge_' + str(generation), context=context)
        topic = '/m4_motion/s_' + scope + '/smoothed'
        finite = os.environ.get('M6_NAVIGATION_PROFILE') == 'scoped-two-context-nav2-owner-permit-v1'
        if finite:
            from m4_drive_probe.msg import PermittedTwist
        kind = PermittedTwist if finite else ScopedTwist
        publisher = self.create_publisher(kind, '/owner_permission/cmd' if finite else '/scoped_cmd_vel', 10)
        operation = None
        # These closure bindings never change. Each subscription is permanently
        # attached to one producer channel, even if another generation opens.
        def forward(message):
            nonlocal operation
            fields = {}
            if finite:
                if operation is None:
                    binding = Path('/output')/('operation-'+scope+'.json')
                    if not binding.exists():
                        return
                    row = json.loads(binding.read_text())
                    if (row['scope_id'] != scope or row['generation'] != generation
                            or type(row['operation_id']) is not int or not 0 < row['operation_id'] < 2**64):
                        raise RuntimeError('producer immutable operation binding invalid')
                    operation = row['operation_id']
                fields['operation_id'] = operation
            command = kind(scope_id=scope, generation=generation, twist=message, **fields)
            publisher.publish(command)
            print(json.dumps(dict(event='producer_command_forwarded', scope_id=scope,
                                  generation=generation, input_topic=topic, **fields,
                                  linear_x=message.linear.x, angular_z=message.angular.z,
                                  steady_ns=time.monotonic_ns())), flush=True)
        self.create_subscription(Twist, topic, forward, 10)
        print(json.dumps(dict(event='producer_binding', scope_id=scope,
                              generation=generation, input_topic=topic)), flush=True)


def run_bridge(scopes):
    stopped = False
    previous, nodes, errors = {}, [], []
    context = executor = None
    def stop(*_):
        nonlocal stopped
        stopped = True
    def dispose(action):
        try:
            if action() is False:
                errors.append('executor shutdown incomplete')
        except Exception as error:
            errors.append(repr(error))
    try:
        for kind in (signal.SIGINT, signal.SIGTERM):
            previous[kind] = signal.signal(kind, stop)
        context = Context()
        rclpy.init(context=context, signal_handler_options=SignalHandlerOptions.NO)
        if not stopped:
            executor = SingleThreadedExecutor(context=context)
            for i, scope in enumerate(scopes):
                if stopped:
                    break
                node = Bridge(scope, i + 1, context=context)
                nodes.append(node)
                executor.add_node(node)
            while not stopped and context.ok():
                executor.spin_once(timeout_sec=.05)
    finally:
        if executor is not None:
            dispose(lambda: executor.shutdown(timeout_sec=.5))
        for node in nodes:
            dispose(node.destroy_node)
        if context is not None:
            dispose(context.try_shutdown)
        for kind, handler in previous.items():
            signal.signal(kind, handler)
        print(json.dumps(dict(event='producer_bridge_exit', stopped=stopped,
                              cleanup_errors=errors)), flush=True)
    return 1 if errors else 0


def main():
    if os.environ.get('M4_ISOLATED_SIMULATION') != '1' or not Path('/.dockerenv').exists():
        return 2
    return run_bridge(contexts())


if __name__ == '__main__':
    raise SystemExit(main())
