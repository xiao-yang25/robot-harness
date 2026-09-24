"""Creates the B fixture on request; never admits, sends goals or opens drive."""
import os
import json
import time
import subprocess
from pathlib import Path
import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger
from gazebo_pose import query_pose


class ContextLauncher(Node):
    def __init__(self):
        super().__init__('owner_context_launcher')
        self.process = None
        self.log = None
        self.service = self.create_service(Trigger, '/prepare_next_context', self.prepare)

    def prepare(self, request, response):
        if self.process is not None:
            response.success = False
            response.message = 'single-use context already requested'
            return response
        pose = query_pose('A_closed', timeout=3)
        print(json.dumps(dict(event='gazebo_pose', phase='A_closed', xyz_rpy=pose,
                              steady_ns=time.monotonic_ns())), flush=True)
        Path('/output/b').mkdir(exist_ok=False)
        self.log = open('/output/b/launch.log', 'w')
        self.process = subprocess.Popen([
            'ros2', 'launch', '/simulation/native_worker/task_context.launch.py',
            'use_sim_time:=True', 'use_respawn:=False', 'use_composition:=False',
            'params_file:=/output/native-params.yaml'], stdout=self.log, stderr=subprocess.STDOUT)
        response.success = self.process.poll() is None
        response.message = 'spawn requested; Owner must independently establish readiness'
        return response


if __name__ == '__main__':
    if os.environ.get('M4_ISOLATED_SIMULATION') != '1' or not Path('/.dockerenv').exists():
        raise SystemExit(2)
    rclpy.init()
    node = ContextLauncher()
    try:
        rclpy.spin(node)
    finally:
        if node.process and node.process.poll() is None:
            node.process.terminate()
            node.process.wait(timeout=10)
        if node.log:
            node.log.close()
        node.destroy_node()
        rclpy.shutdown()
