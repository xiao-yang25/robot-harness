"""Actual Gazebo drive callbacks; isolated Linux only, no physical-stop verdict."""
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import unittest

import rclpy
from rclpy.qos import QoSProfile, DurabilityPolicy
from m4_drive_probe.msg import ScopedTwist, PermittedTwist
from m4_drive_probe.srv import OpenMotionScope, CloseScopedMotion, OpenPermit, RenewPermit
from std_msgs.msg import String


class DriveProtocolChecks(unittest.TestCase):
    def start(self, finite):
        if os.environ.get('ROS_LOCALHOST_ONLY') != '1':
            raise RuntimeError('requires isolated localhost-only Linux fixture')
        self.name = 'finite' if finite else 'legacy'
        self.output = Path('/output')
        self.output.mkdir(exist_ok=True)
        world = self.output/(self.name+'.world')
        # Two real ODE wheel joints. Gravity is disabled: this test checks wire
        # admission and actual zero application, not navigation or braking.
        links = ''.join('<link name="'+name+'"><inertial><mass>1</mass>'
                        '<inertia><ixx>1</ixx><iyy>1</iyy><izz>1</izz></inertia>'
                        '</inertial></link>' for name in ('base', 'left', 'right'))
        joints = ''.join('<joint name="'+name+'_joint" type="revolute">'
                         '<parent>base</parent><child>'+name+'</child>'
                         '<axis><xyz>0 1 0</xyz></axis></joint>' for name in ('left', 'right'))
        world.write_text('<sdf version="1.6"><world name="protocol"><gravity>0 0 0</gravity>'
                         '<physics type="ode"><max_step_size>0.001</max_step_size>'
                         '<real_time_update_rate>1000</real_time_update_rate></physics>'
                         '<model name="drive">'+links+joints+
                         '<plugin name="scoped_drive" filename="libscoped_diff_drive.so">'
                         '<generation_mode>true</generation_mode>'
                         '<owner_permission_mode>'+str(finite).lower()+'</owner_permission_mode>'
                         '<left_joint>left_joint</left_joint><right_joint>right_joint</right_joint>'
                         '<wheel_separation>0.4</wheel_separation><wheel_diameter>0.2</wheel_diameter>'
                         '<max_wheel_torque>20</max_wheel_torque><publish_odom>false</publish_odom>'
                         '<publish_odom_tf>false</publish_odom_tf></plugin></model></world></sdf>')
        self.log = (self.output/(self.name+'-gazebo.log')).open('w')
        self.process = subprocess.Popen(['gzserver', '--verbose', '-s', 'libgazebo_ros_init.so', str(world)],
                                        stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        self.addCleanup(self.cleanup)
        rclpy.init()
        self.node = rclpy.create_node('drive_protocol_check')
        self.acks = []
        self.node.create_subscription(String, '/motion_scope_ack',
            lambda message: self.acks.append(json.loads(message.data)),
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        self.old_open = self.node.create_client(OpenMotionScope, '/open_motion_scope')
        self.old_close = self.node.create_client(CloseScopedMotion, '/close_scoped_motion')
        self.old_command = self.node.create_publisher(ScopedTwist, '/scoped_cmd_vel', 10)
        self.open = self.node.create_client(OpenPermit, '/owner_permission/open')
        self.renew = self.node.create_client(RenewPermit, '/owner_permission/renew')
        self.command = self.node.create_publisher(PermittedTwist, '/owner_permission/cmd', 10)
        clients = [self.old_open, self.old_close]+([self.open, self.renew] if finite else [])
        self.wait(lambda: all(client.service_is_ready() for client in clients)
                  and self.old_command.get_subscription_count() > 0
                  and (not finite or self.command.get_subscription_count() > 0), 20)
        self.wait(lambda: any(row['event'] == 'drive_boot_zero_applied' for row in self.rows()), 5)

    def cleanup(self):
        if hasattr(self, 'node'):
            self.node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
        if self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=3)
        self.log.close()
        path = self.output/'drive-native.jsonl'
        if path.exists():
            (self.output/(self.name+'-drive.jsonl')).write_text(path.read_text())

    def rows(self):
        path = self.output/'drive-native.jsonl'
        if not path.exists():
            return []
        # std::endl flushes complete events, but a concurrent last write may be partial.
        lines = path.read_text().splitlines(keepends=True)
        return [json.loads(line) for line in lines if line.endswith('\n')]

    def wait(self, predicate, budget=5):
        until = time.monotonic()+budget
        while not predicate():
            self.assertIsNone(self.process.poll(), 'Gazebo exited; see raw log')
            if time.monotonic() >= until:
                self.fail('actual drive condition timed out; see raw logs')
            rclpy.spin_once(self.node, timeout_sec=.01)

    def call(self, client, request):
        future = client.call_async(request)
        self.wait(future.done)
        return future.result()

    def test_finite_refuses_legacy_and_late_renewal(self):
        self.start(True)
        scope = 'a'*32
        issued = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        grant = self.call(self.open, OpenPermit.Request(scope_id=scope, expected_generation=0,
                                                       operation_id=7, issued_ns=issued))
        self.assertTrue(grant.accepted)
        self.assertEqual(grant.deadline_ns, issued+1_000_000_000)
        self.command.publish(PermittedTwist(scope_id=scope, generation=1, operation_id=7))
        self.assertFalse(self.call(self.old_open,
            OpenMotionScope.Request(scope_id=scope, expected_generation=0)).accepted)
        self.assertFalse(self.call(self.old_close,
            CloseScopedMotion.Request(scope_id=scope, generation=1)).accepted)
        old = ScopedTwist(scope_id=scope, generation=1)
        old.twist.linear.x = .5
        self.old_command.publish(old)
        self.wait(lambda: any(row['event'] == 'legacy_command_rejected' for row in self.rows()))
        self.wait(lambda: any(row.get('closure_reason') == 'permission_expired' for row in self.acks))
        fact = next(row for row in self.acks if row.get('closure_reason') == 'permission_expired')
        self.assertEqual((fact['scope_id'], fact['generation'], fact['operation_id']), (scope, 1, 7))
        self.assertTrue(fact['wheel_targets_zero'])
        self.assertGreaterEqual(fact['steady_ns'], grant.deadline_ns)
        fresh = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        self.assertFalse(self.call(self.renew, RenewPermit.Request(scope_id=scope, generation=1,
                                                                 operation_id=7, issued_ns=fresh)).accepted)
        self.assertFalse(self.call(self.open, OpenPermit.Request(scope_id='b'*32, expected_generation=1,
                                                                operation_id=8, issued_ns=fresh)).accepted)

    def test_default_generation_protocol_still_opens_and_closes(self):
        self.start(False)
        self.assertFalse(self.open.service_is_ready())
        scope = 'a'*32
        response = self.call(self.old_open, OpenMotionScope.Request(scope_id=scope, expected_generation=0))
        self.assertTrue(response.accepted)
        self.assertEqual(response.generation, 1)
        self.assertTrue(self.call(self.old_close, CloseScopedMotion.Request(scope_id=scope, generation=1)).accepted)
        self.wait(lambda: any(row.get('scope_id') == scope for row in self.acks))
        fact = next(row for row in self.acks if row.get('scope_id') == scope)
        self.assertNotIn('operation_id', fact)
        self.assertNotIn('permission_deadline_ns', fact)
        self.assertTrue(fact['wheel_targets_zero'])


if __name__ == '__main__':
    unittest.main()
