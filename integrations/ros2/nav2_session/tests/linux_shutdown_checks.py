"""Actual Humble context and signals through the entry; no native motion."""
import argparse
import os
import signal
import unittest
from unittest.mock import patch

import rclpy
from rclpy.node import Node
from robot_harness_nav2.__main__ import run_owner
from robot_harness_nav2._shutdown import ShutdownSignals


class ShutdownChecks(unittest.TestCase):
    def test_interrupted_initialization_cleans_up_created_context(self):
        original_init = rclpy.init
        def interrupted_init(**options):
            original_init(**options)
            raise KeyboardInterrupt('stop after context creation')
        class Owner:
            def abort(self):
                self.context_alive = rclpy.ok()
        args = argparse.Namespace(terminal_query_seconds=None, scene='normal',
                                  caller_wait_seconds=None)
        with ShutdownSignals() as shutdown, patch.object(rclpy, 'init', interrupted_init):
            code = run_owner(args, Owner, rclpy, lambda *a, **k: None,
                             lambda *a, **k: None, shutdown)
        self.assertEqual(code, 1)
        self.assertFalse(rclpy.ok())

    def test_interrupts_keep_context_alive_through_abort_and_dispose(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=signum):
                facts = []
                class Owner(Node):
                    def __init__(self):
                        super().__init__('shutdown_entry_check')
                    def ready(self):
                        os.kill(os.getpid(), signum)
                    def abort(self):
                        facts.append(('abort_context', rclpy.ok()))
                        # A second signal cannot terminate the cleanup dependency.
                        os.kill(os.getpid(), signal.SIGINT)
                        os.kill(os.getpid(), signal.SIGTERM)
                        rclpy.spin_once(self, timeout_sec=.01)
                        facts.append(('abort_spin', True))
                    def destroy_node(self):
                        facts.append(('dispose_context', rclpy.ok()))
                        return super().destroy_node()
                args = argparse.Namespace(terminal_query_seconds=None, scene='normal',
                                          caller_wait_seconds=None)
                previous = {kind: signal.getsignal(kind) for kind in
                            (signal.SIGINT, signal.SIGTERM)}
                with ShutdownSignals() as shutdown:
                    code = run_owner(args, Owner, rclpy, lambda *a, **k: None,
                                     lambda *a, **k: None, shutdown)
                self.assertEqual(code, 1)
                self.assertEqual(facts, [('abort_context', True), ('abort_spin', True),
                                         ('dispose_context', True)])
                self.assertFalse(rclpy.ok())
                self.assertEqual({kind: signal.getsignal(kind) for kind in previous}, previous)


if __name__ == '__main__':
    unittest.main()
