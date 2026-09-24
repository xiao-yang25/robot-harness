"""Inject old A raw velocity while B executes; never opens or admits work."""
import json
import os
from pathlib import Path
import time

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node


def records(path):
    if not path.exists():
        return []
    # Writers may currently be appending the final line.
    lines = path.read_text().splitlines(keepends=True)
    return [json.loads(line) for line in lines if line.startswith('{') and line.endswith('\n')]


def main():
    if (os.environ.get('M4_ISOLATED_SIMULATION') != '1'
            or os.environ.get('M4_CONTEXT_CASE') != 'replace-moving'
            or not Path('/.dockerenv').exists()):
        return 2
    root = Path('/output')
    scopes = json.loads((root / 'producer-context.json').read_text())
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        drive = records(root / 'drive-native.jsonl')
        bridge = records(root / 'producer-bridge.jsonl')
        if (any(row['event'] == 'motion_scope_opened' and row['generation'] == 2 for row in drive)
                and any(row['event'] == 'producer_command_forwarded'
                        and row['scope_id'] == scopes['b_scope']
                        and row['generation'] == 2 and abs(row['linear_x']) > .05 for row in bridge)):
            break
        time.sleep(.05)
    else:
        raise RuntimeError('B did not begin moving before injection deadline')
    rclpy.init()
    node = Node('late_a_raw_command_injector')
    topic = '/m4_motion/s_' + scopes['a_scope'] + '/raw'
    # Only create this extra publisher after B is admitted/open/moving. Initial
    # producer topology validation must still inspect the real single producer.
    publisher = node.create_publisher(Twist, topic, 10)
    until = time.monotonic() + 5
    while publisher.get_subscription_count() == 0 and time.monotonic() < until:
        rclpy.spin_once(node, timeout_sec=.05)
    if publisher.get_subscription_count() != 1:
        raise RuntimeError('expected the retained A smoother only')
    message = Twist()
    message.linear.x = .2
    message.angular.z = .5
    for _ in range(40):
        publisher.publish(message)
        print(json.dumps(dict(event='old_raw_command_injected', scope_id=scopes['a_scope'],
                              input_topic=topic, steady_ns=time.monotonic_ns())), flush=True)
        rclpy.spin_once(node, timeout_sec=.05)
    publisher.publish(Twist())
    node.destroy_node()
    rclpy.shutdown()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
