"""Actual Humble parameters/TF and owned display; no Gazebo or robot commands."""
import json
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

import rclpy
from rclpy.context import Context
from rclpy.executors import SingleThreadedExecutor
from rclpy.node import Node
from rclpy.signals import SignalHandlerOptions
from rclpy.time import Time
from tf2_ros import Buffer, TransformListener

import inspection_scene
from robot_harness_nav2._inspection import verify_controllers, PARAMETERS, YAW_TOLERANCE


class InspectionChecks(unittest.TestCase):
    def setUp(self):
        self.context = Context()
        rclpy.init(context=self.context, signal_handler_options=SignalHandlerOptions.NO)
        self.executor = SingleThreadedExecutor(context=self.context)
        self.nodes = []
        self.addCleanup(self.close)
        self.owner = self.node('inspection_check_owner')
        self.owner.motion_started = False
        self.owner.execution = self.owner.endpoint = None
        self.owner.deadline = time.monotonic()+10
        self.owner.inspection_case = 'normal'
        self.owner.contexts = {'B': {'namespace': '/inspection_check_b'}}
        def wait(predicate, budget):
            until = time.monotonic()+budget
            while not predicate():
                if time.monotonic() >= until:
                    raise TimeoutError('actual service wait expired')
                self.executor.spin_once(timeout_sec=min(.01, until-time.monotonic()))
        self.owner.wait = wait

    def node(self, name, **kwargs):
        result = Node(name, context=self.context, **kwargs)
        self.nodes.append(result); self.executor.add_node(result)
        return result

    def controller(self, namespace, tolerance, *, yaw=YAW_TOLERANCE):
        result = self.node('controller_server', namespace=namespace)
        values = (['general_goal_checker'], 'nav2_controller::SimpleGoalChecker',
                  True, yaw, tolerance, tolerance)
        for name, value in zip(PARAMETERS, values):
            result.declare_parameter(name, value)
        return result

    def close(self):
        self.executor.shutdown(timeout_sec=1)
        for node in reversed(self.nodes):node.destroy_node()
        self.context.try_shutdown()

    def test_actual_parameters_for_both_contexts_are_consumed_and_clients_disposed(self):
        self.controller('', .15); self.controller('/inspection_check_b', .15)
        with patch('robot_harness_nav2._observations.emit') as emit:
            verify_controllers(self.owner)
        self.assertEqual(emit.call_args.args, ('inspection_configuration_ready',))
        rows = emit.call_args.kwargs['controllers']
        self.assertEqual([r['stage'] for r in rows], ['A','B'])
        self.assertTrue(all(r['values']['general_goal_checker.yaw_goal_tolerance'] == YAW_TOLERANCE
                            and r['values']['FollowPath.xy_goal_tolerance'] == .15 for r in rows))
        self.assertTrue(emit.call_args.kwargs['checked_before_public_admission'])
        self.assertFalse(tuple(self.owner.clients))

    def test_wrong_actual_b_value_rejects_without_mutation_or_ready_fact(self):
        self.controller('', .15); b = self.controller('/inspection_check_b', .25)
        with patch('robot_harness_nav2._observations.emit') as emit, self.assertRaisesRegex(ValueError,'differs'):
            verify_controllers(self.owner)
        emit.assert_not_called()
        self.assertEqual(b.get_parameter('general_goal_checker.xy_goal_tolerance').value, .25)
        self.assertFalse(tuple(self.owner.clients))

    def test_wrong_actual_heading_rejects_even_when_xy_matches(self):
        self.controller('', .15)
        b = self.controller('/inspection_check_b', .15, yaw=.25)
        with patch('robot_harness_nav2._observations.emit') as emit, self.assertRaisesRegex(ValueError, 'yaw'):
            verify_controllers(self.owner)
        emit.assert_not_called()
        self.assertEqual(b.get_parameter('general_goal_checker.yaw_goal_tolerance').value, .25)
        self.assertFalse(tuple(self.owner.clients))

    def test_missing_actual_b_service_expires_without_retry_or_ready_fact(self):
        self.controller('', .15)
        started = time.monotonic()
        with patch('robot_harness_nav2._observations.emit') as emit, self.assertRaises(TimeoutError):
            verify_controllers(self.owner)
        self.assertLess(time.monotonic()-started, 5)
        emit.assert_not_called()
        self.assertFalse(tuple(self.owner.clients))

    def test_static_optical_tf_is_received_by_actual_buffer_without_extra_thread(self):
        buffer = Buffer()
        listener = TransformListener(buffer, self.owner, spin_thread=False)
        try:
            broadcaster = inspection_scene.camera_transform(self.owner)
            self.owner.wait(lambda: buffer.can_transform('base_link','inspection_optical',Time()), 2)
            value = buffer.lookup_transform('base_link','inspection_optical',Time()).transform
            self.assertEqual((value.translation.x,value.translation.y,value.translation.z), (.1,0.,.3))
            q = value.rotation
            # TF may return q or -q; compare the represented rotation below.
            self.assertAlmostEqual(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w, 1.)
            # Independent camera-axis mapping: optical right/down/forward to base.
            matrix = ((1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y-q.z*q.w),2*(q.x*q.z+q.y*q.w)),
                      (2*(q.x*q.y+q.z*q.w),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z-q.x*q.w)),
                      (2*(q.x*q.z-q.y*q.w),2*(q.y*q.z+q.x*q.w),1-2*(q.x*q.x+q.y*q.y)))
            self.assertEqual(matrix, ((0.,0.,1.),(-1.,0.,0.),(0.,-1.,0.)))
        finally:
            listener.unregister()


class DisplayChecks(unittest.TestCase):
    def check_failed_display(self, forced):
        root = Path(__file__).parent
        source = (root/'navigation_session.sh').read_text()
        cleanup = source[source.index('cleanup() {'):source.index('trap cleanup EXIT')]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            cleanup = cleanup.replace('/output/', str(output)+'/').replace('/simulation/navigation_groups.py',str(root/'navigation_groups.py'))
            command = ("setsid python3 -u -c 'import signal,time; from pathlib import Path; signal.signal(signal.SIGINT,signal.SIG_IGN); Path(\""+temporary+"/ready\").touch(); time.sleep(30)'"
                       if forced else 'setsid Xvfb :95 -screen 0 640x480x24 -nolisten tcp')
            ready = '[[ -f $2/ready ]]' if forced else '[[ -S /tmp/.X11-unix/X95 ]]'
            program = 'set -e\nsource "$1"\n'+cleanup+"""
owner_pid='' client_pid='' bridge_pid='' collector_pid='' launch_pid='' display_pid=''
session_completed="""+('1' if forced else '0')+"""
trap cleanup EXIT
"""+command+""" > "$2/display.log" 2>&1 & display_pid=$!
for i in $(seq 1 60); do """+ready+""" && break; sleep .05; done
"""+ready+"\n"+('exit 0\n' if forced else 'exit 23\n')
            result = subprocess.run(['bash','-c',program,'check',str(root/'navigation_cleanup.sh'),temporary],
                capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,1 if forced else 23,result.stderr)
            fact = json.loads((output/'display-process.json').read_text())
            self.assertEqual(fact['clean'],not forced,fact)
            self.assertEqual(fact['groups'][0]['forced'],forced)
            self.assertTrue(fact['groups'][0]['reaped'])
            self.assertEqual(fact['groups'][0]['remaining'],[])
            self.assertFalse((output/'verification.json').exists())
            self.assertFalse(Path('/tmp/.X11-unix/X95').exists())

    def test_partial_startup_closes_actual_display_without_success(self):
        self.check_failed_display(False)

    def test_forced_display_cleanup_cannot_emit_success(self):
        self.check_failed_display(True)

    def test_display_outlives_scene_and_is_closed_before_success_marker(self):
        root = Path(__file__).parent
        source = (root/'navigation_session.sh').read_text()
        cleanup = source[source.index('cleanup() {'):source.index('trap cleanup EXIT')]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            cleanup = cleanup.replace('/output/', str(output)+'/').replace('/simulation/navigation_groups.py',str(root/'navigation_groups.py'))
            program = '''set -e
source "$1"
'''+cleanup+'''
owner_pid='' client_pid='' bridge_pid='' collector_pid='' launch_pid='' display_pid=''
session_completed=1
trap cleanup EXIT
setsid Xvfb :95 -screen 0 640x480x24 -nolisten tcp > "$2/display.log" 2>&1 & display_pid=$!
for i in $(seq 1 60); do [[ ! -S /tmp/.X11-unix/X95 ]] || break; sleep .05; done
[[ -S /tmp/.X11-unix/X95 ]]
setsid python3 -u -c '
import os,signal,sys,time
from pathlib import Path
def stop(*args):
 os.kill(int(sys.argv[1]),0)
 Path(sys.argv[2]+"/scene-display-alive").touch()
 raise SystemExit(0)
signal.signal(signal.SIGINT,stop)
Path(sys.argv[2]+"/ready").touch()
time.sleep(30)
' "$display_pid" "$2" > "$2/scene.log" 2>&1 & launch_pid=$!
for i in $(seq 1 60); do [[ ! -f $2/ready ]] || break; sleep .05; done
[[ -f $2/ready ]]
'''
            result = subprocess.run(['bash','-c',program,'check',str(root/'navigation_cleanup.sh'),temporary],
                capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertTrue((output/'scene-display-alive').exists())
            fact = json.loads((output/'display-process.json').read_text())
            self.assertTrue(fact['clean'],fact)
            self.assertTrue(fact['groups'][0]['reaped'])
            self.assertFalse(fact['groups'][0]['forced'])
            self.assertEqual(fact['groups'][0]['remaining'],[])
            self.assertTrue(json.loads((output/'verification.json').read_text())['passed'])
            self.assertFalse(Path('/tmp/.X11-unix/X95').exists())


if __name__ == '__main__': unittest.main()
