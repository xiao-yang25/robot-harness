"""Actual Humble bridge context/signal cleanup; no input commands or movement."""
import os
import signal
import unittest
from unittest.mock import patch

from rclpy.context import Context
import producer_bridge as bridge


class BridgeShutdownChecks(unittest.TestCase):
    def test_inherited_ignored_signal_is_replaced_and_context_lives_through_cleanup(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                facts, contexts = [], []
                previous = {s:signal.signal(s, signal.SIG_IGN) for s in (signal.SIGINT, signal.SIGTERM)}
                def context():
                    result = Context();contexts.append(result);return result
                class StoppingBridge(bridge.Bridge):
                    def __init__(self, *args, **kwargs):
                        super().__init__(*args, **kwargs)
                        if self.get_name().endswith('_1'):
                            self.create_timer(.01, self.stop)
                    def stop(self):
                        facts.append('timer')
                        os.kill(os.getpid(), signum)
                    def destroy_node(self):
                        facts.append((self.get_name(), self.context.ok()))
                        os.kill(os.getpid(), signal.SIGINT)
                        os.kill(os.getpid(), signal.SIGTERM)
                        return super().destroy_node()
                try:
                    with patch.object(bridge,'Context',context), patch.object(bridge,'Bridge',StoppingBridge):
                        code = bridge.run_bridge(('1'*32, '2'*32))
                    self.assertEqual(code, 0)
                    self.assertEqual(facts, ['timer',('producer_bridge_1',True),('producer_bridge_2',True)])
                    self.assertFalse(contexts[0].ok())
                    self.assertTrue(all(signal.getsignal(s) == signal.SIG_IGN for s in previous))
                finally:
                    for s, handler in previous.items():signal.signal(s,handler)

    def test_interrupted_init_after_real_context_creation_is_disposed(self):
        original = bridge.rclpy.init
        contexts = []
        def init(**kwargs):
            original(**kwargs);contexts.append(kwargs['context'])
            raise RuntimeError('inert init interruption')
        previous = {s:signal.getsignal(s) for s in (signal.SIGINT, signal.SIGTERM)}
        with patch.object(bridge.rclpy,'init',init), self.assertRaisesRegex(RuntimeError,'inert init'):
            bridge.run_bridge(('1'*32,'2'*32))
        self.assertFalse(contexts[0].ok())
        self.assertEqual({s:signal.getsignal(s) for s in previous}, previous)


if __name__ == '__main__':
    unittest.main()
