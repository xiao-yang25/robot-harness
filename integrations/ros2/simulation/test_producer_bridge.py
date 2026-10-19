"""Bridge stop/cleanup contract using controlled ROS interfaces, without ROS."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import signal
import sys
from types import ModuleType
import unittest
from unittest.mock import patch


class BridgeCleanupTests(unittest.TestCase):
    def setUp(self):
        self.facts = []
        self.context = None
        self.signum = signal.SIGINT
        self.fail_init = self.fail_node = self.fail_cleanup = False
        parent = self
        class Context:
            alive = False
            def __init__(self): parent.context = self
            def ok(self): return self.alive
            def try_shutdown(self):
                parent.facts.append('context-shutdown')
                self.alive = False
        def init(*, context, signal_handler_options):
            parent.assertIs(signal_handler_options, no_handlers)
            context.alive = True
            if parent.fail_init:
                raise RuntimeError('init interrupted after context creation')
        class Executor:
            def __init__(self, *, context): self.context = context
            def add_node(self, node): parent.assertIs(node.context, self.context)
            def spin_once(self, *, timeout_sec): os.kill(os.getpid(), parent.signum)
            def shutdown(self, *, timeout_sec):
                parent.facts.append(('executor-shutdown', self.context.ok()))
                return True
        class Node:
            def __init__(self, scope, generation, *, context):
                if parent.fail_node and generation == 2:
                    raise RuntimeError('second node construction failed')
                self.context, self.generation = context, generation
            def destroy_node(self):
                parent.facts.append(('destroy', self.generation, self.context.ok()))
                os.kill(os.getpid(), signal.SIGINT)
                os.kill(os.getpid(), signal.SIGTERM)
                if parent.fail_cleanup and self.generation == 1:
                    raise RuntimeError('first node disposal failed')
        no_handlers = object()
        modules = {name: ModuleType(name) for name in ('rclpy','rclpy.context',
            'rclpy.executors','rclpy.node','rclpy.signals','geometry_msgs',
            'geometry_msgs.msg','m4_drive_probe','m4_drive_probe.msg')}
        modules['rclpy'].init = init
        modules['rclpy.context'].Context = Context
        modules['rclpy.executors'].SingleThreadedExecutor = Executor
        modules['rclpy.node'].Node = object
        modules['rclpy.signals'].SignalHandlerOptions = type('Options', (), {'NO':no_handlers})
        modules['geometry_msgs.msg'].Twist = object
        modules['m4_drive_probe.msg'].ScopedTwist = object
        path = Path(__file__).with_name('producer_bridge.py')
        spec = importlib.util.spec_from_file_location('bridge_cleanup_fixture', path)
        with patch.dict(sys.modules, modules):
            self.bridge = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.bridge)
        self.bridge.Bridge = Node
        self.previous = {s:signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}

    def assert_disposed(self):
        self.assertFalse(self.context.ok())
        self.assertEqual({s:signal.getsignal(s) for s in self.previous}, self.previous)

    def run_bridge(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return self.bridge.run_bridge(('a', 'b'))

    def test_stop_retains_context_through_executor_and_nodes_despite_repeated_signals(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                self.facts.clear();self.signum = signum
                self.assertEqual(self.run_bridge(), 0)
                self.assertEqual(self.facts, [('executor-shutdown',True),
                    ('destroy',1,True),('destroy',2,True),'context-shutdown'])
                self.assert_disposed()

    def test_partial_initialization_preserves_exception_and_closes_context(self):
        self.fail_init = True
        with self.assertRaisesRegex(RuntimeError, 'init interrupted'):
            self.run_bridge()
        self.assertEqual(self.facts, ['context-shutdown'])
        self.assert_disposed()

    def test_second_node_failure_still_disposes_first_node_and_context(self):
        self.fail_node = True
        with self.assertRaisesRegex(RuntimeError, 'second node'):
            self.run_bridge()
        self.assertEqual(self.facts, [('executor-shutdown',True),('destroy',1,True),'context-shutdown'])
        self.assert_disposed()

    def test_failed_node_disposal_does_not_skip_other_nodes_or_context(self):
        self.fail_cleanup = True
        self.assertEqual(self.run_bridge(), 1)
        self.assertEqual(self.facts, [('executor-shutdown',True),
            ('destroy',1,True),('destroy',2,True),'context-shutdown'])
        self.assert_disposed()


if __name__ == '__main__':
    unittest.main()
