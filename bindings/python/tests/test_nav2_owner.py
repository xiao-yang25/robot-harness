"""Installed optional package and local endpoint boundaries; no ROS required."""
import os
from pathlib import Path
import socket
import stat
import sys
import unittest
from unittest.mock import patch

import robot_harness_nav2
from robot_harness_nav2.__main__ import require_isolation
from robot_harness_nav2._requests import RequestEndpoint
from robot_harness._transport import Channel


class Requests:
    def __init__(self):
        self.closing = False
        self.calls = []

    def command(self, message):
        self.calls.append(message['command'])
        if message['command'] == 'close':
            self.close()
            return dict(closure='unknown', owner_reaped=False)
        return dict(value=message['command'])

    def close(self):
        self.closing = True


class EndpointTests(unittest.TestCase):
    def setUp(self):
        self.requests = Requests()
        self.endpoint = RequestEndpoint(self.requests)
        self.addCleanup(self.endpoint.dispose)

    def connect(self):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(self.endpoint.path)
        client = Channel(sock)
        self.addCleanup(client.close)
        return client

    def test_private_socket_and_batched_replies(self):
        self.assertEqual(stat.S_IMODE(os.stat(self.endpoint.path).st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(os.stat(Path(self.endpoint.path).parent).st_mode), 0o700)
        client = self.connect()
        for rpc, command in enumerate(('observe', 'status', 'close'), 1):
            client.queue(dict(rpc=rpc, command=command))
        client.pump()
        self.endpoint.pump()
        replies = client.pump()
        self.assertEqual([row['rpc'] for row in replies], [1, 2, 3])
        self.assertEqual(self.requests.calls, ['observe', 'status', 'close'])
        self.assertEqual(replies[-1]['result'], dict(closure='unknown', owner_reaped=False))

    def test_eof_closes_admission_and_removes_endpoint(self):
        client = self.connect()
        self.endpoint.pump()
        client.close()
        self.endpoint.pump()
        self.assertTrue(self.requests.closing)
        self.assertTrue(self.endpoint.closed)
        self.assertFalse(Path(self.endpoint.path).exists())
        self.endpoint.dispose()
        self.endpoint.pump()

    def test_invalid_rpc_closes_admission(self):
        client = self.connect()
        client.queue(dict(command='observe', rpc=False))
        client.pump()
        self.endpoint.pump()
        self.assertTrue(self.requests.closing)
        self.assertEqual(self.requests.calls, [])

    def test_partial_bind_failure_disposes_socket_and_directory(self):
        fake = unittest.mock.Mock()
        fake.bind.side_effect = OSError('bind refused')
        seen = []
        import tempfile
        original = tempfile.TemporaryDirectory
        def directory(*a, **k):
            value = original(*a, **k)
            seen.append(value.name)
            return value
        with patch('robot_harness_nav2._requests.socket.socket', return_value=fake), patch(
                'robot_harness_nav2._requests.tempfile.TemporaryDirectory', side_effect=directory):
            with self.assertRaisesRegex(OSError, 'bind refused'):
                RequestEndpoint(self.requests)
        fake.close.assert_called_once()
        self.assertFalse(Path(seen[0]).exists())


class PackageTests(unittest.TestCase):
    def test_import_and_startup_guard_do_not_require_ros(self):
        self.assertNotIn('rclpy', sys.modules)
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'isolated'):
                require_isolation()
        self.assertNotIn('rclpy', sys.modules)


class ProfileTests(unittest.TestCase):
    def test_heading_policy_retains_dwb_and_original_progress_and_speed_limits(self):
        from robot_harness_nav2._profile import PROFILE_ID, configure_controller
        parameters = {'controller_server': {'ros__parameters': {
            'progress_checker': {'movement_time_allowance': 10.0, 'required_movement_radius': 0.5},
            'FollowPath': {'plugin': 'dwb_core::DWBLocalPlanner', 'max_vel_theta': 1.0,
                           'acc_lim_theta': 3.2, 'max_vel_x': 0.26, 'critics': ['PathAlign']}}}}
        configure_controller(parameters)
        node = parameters['controller_server']['ros__parameters']
        follow = node['FollowPath']
        self.assertEqual(PROFILE_ID, 'scoped-two-context-nav2-shim-v1')
        self.assertEqual(follow['primary_controller'], 'dwb_core::DWBLocalPlanner')
        self.assertEqual(node['progress_checker'], dict(movement_time_allowance=10.0, required_movement_radius=0.5))
        self.assertEqual((follow['max_vel_theta'], follow['acc_lim_theta'], follow['max_vel_x']), (1.0, 3.2, 0.26))
        self.assertEqual(follow['critics'], ['PathAlign'])
        self.assertLessEqual(follow['rotate_to_heading_angular_vel'], follow['max_vel_theta'])
        self.assertEqual(follow['max_angular_accel'], follow['acc_lim_theta'])
        self.assertEqual(follow['simulate_ahead_time'], 1.0)
        self.assertTrue(follow['closed_loop'])
        self.assertFalse(follow['rotate_to_goal_heading'])

    def test_foreign_controller_policy_is_not_silently_replaced(self):
        from robot_harness_nav2._profile import configure_controller
        parameters = {'controller_server': {'ros__parameters': {'FollowPath': {'plugin': 'other'}}}}
        with self.assertRaisesRegex(ValueError, 'original DWB'):
            configure_controller(parameters)
        self.assertEqual(parameters['controller_server']['ros__parameters']['FollowPath']['plugin'], 'other')
