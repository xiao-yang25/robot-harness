"""Best-effort observations, never task evidence or an admission decision."""
import json
from pathlib import Path
import sys
import time


def process_state(pid, proc=Path('/proc')):
    try:
        fields = (proc / str(pid) / 'stat').read_text().rsplit(')', 1)[1].split()
        return dict(pid=pid, state=fields[0], user_ticks=int(fields[11]),
                    system_ticks=int(fields[12]),
                    wait_channel=(proc / str(pid) / 'wchan').read_text().strip())
    except (OSError, IndexError, ValueError):
        return dict(pid=pid, state='unavailable')


def system_observation():
    servers = []
    try:
        for path in Path('/proc').iterdir():
            if not path.name.isdigit():
                continue
            try:
                args = (path / 'cmdline').read_bytes().split(b'\0')
                if any(arg.rsplit(b'/', 1)[-1] == b'gzserver' for arg in args[:2]):
                    servers.append(process_state(int(path.name)))
            except OSError:
                pass  # A process can exit during enumeration.
    except OSError:
        return dict(gzserver=None, cpu_stat=None)
    try:
        cpu = dict(line.split() for line in Path('/sys/fs/cgroup/cpu.stat').read_text().splitlines())
        cpu = {key: int(value) for key, value in cpu.items()}
    except (OSError, ValueError):
        cpu = None
    return dict(gzserver=servers, cpu_stat=cpu)


def append_record(path, record):
    try:
        with Path(path).open('a') as stream:
            stream.write(json.dumps(record) + '\n')
    except OSError as error:
        print(f'Diagnostic write failed: {error}', file=sys.stderr, flush=True)


class StreamProgress:
    def __init__(self):
        self.values = {}

    def observe(self, name, stamp_ns, received_ns):
        previous = self.values.get(name)
        advanced = previous is None or stamp_ns > previous['max_stamp_ns']
        self.values[name] = dict(count=(previous['count'] + 1 if previous else 1),
                                 stamp_ns=stamp_ns, received_ns=received_ns,
                                 max_stamp_ns=max(stamp_ns, previous['max_stamp_ns']) if previous else stamp_ns,
                                 advanced_ns=received_ns if advanced else previous['advanced_ns'])

    def snapshot(self, now_ns):
        return {name: dict(count=value['count'], stamp_ns=value['stamp_ns'],
                           max_stamp_ns=value['max_stamp_ns'],
                           message_age_ns=now_ns-value['received_ns'],
                           progress_age_ns=now_ns-value['advanced_ns'])
                for name, value in self.values.items()}


def main():
    import signal
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import qos_profile_sensor_data
    from rosgraph_msgs.msg import Clock
    from tf2_msgs.msg import TFMessage
    from sensor_msgs.msg import LaserScan
    from nav_msgs.msg import Odometry

    rclpy.init()
    node = Node('simulation_diagnostics')
    progress = StreamProgress()
    stopping = False

    def stop(_signum, _frame):
        nonlocal stopping
        stopping = True

    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, stop)

    def observe(name, stamp):
        progress.observe(name, stamp.sec * 10**9 + stamp.nanosec, time.monotonic_ns())

    def transforms(message):
        for value in message.transforms:
            pair = (value.header.frame_id, value.child_frame_id)
            if pair in (('map', 'odom'), ('odom', 'base_footprint')):
                observe('tf:' + ':'.join(pair), value.header.stamp)

    subscriptions = [
        node.create_subscription(TFMessage, '/tf', transforms, qos_profile_sensor_data),
        node.create_subscription(Clock, '/clock', lambda m: observe('clock', m.clock), qos_profile_sensor_data),
        node.create_subscription(LaserScan, '/scan', lambda m: observe('scan', m.header.stamp), qos_profile_sensor_data),
        node.create_subscription(Odometry, '/odom', lambda m: observe('odom', m.header.stamp), qos_profile_sensor_data),
    ]
    path = '/output/simulation-diagnostics.jsonl'
    next_sample = time.monotonic()
    try:
        while not stopping and rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.2)
            if time.monotonic() >= next_sample:
                now = time.monotonic_ns()
                append_record(path, dict(event='simulation_sample', steady_ns=now,
                                         streams=progress.snapshot(now), **system_observation()))
                next_sample = time.monotonic() + 1
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
