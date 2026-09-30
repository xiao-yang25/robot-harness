"""Backend boundaries without loading a model or constructing a renderer."""
import unittest
from unittest.mock import Mock

from robot_harness_mujoco import AlohaBackend


class BackendTests(unittest.TestCase):
    def test_candidates_must_remain_finite_in_native_float32(self):
        backend = AlohaBackend.__new__(AlohaBackend)
        for value in (float('nan'), float('inf'), 1e100, 10**1000, True):
            with self.subTest(value_type=type(value).__name__), self.assertRaises(ValueError):
                backend.validate_actions([[value] * 14], 1)
        # Radian joint targets above one must not be clipped/rejected as normalized controls.
        backend.validate_actions([[2.5] * 14], 1)
        with self.assertRaises(ValueError):
            backend.validate_actions([[0.] * 13], 1)

    def test_failed_reset_invalidates_previous_observation(self):
        import numpy as np
        backend = AlohaBackend.__new__(AlohaBackend)
        backend.closed = False
        backend.env = Mock()
        backend.env.reset.side_effect = RuntimeError('reset changed state then failed')
        backend.video = backend.trace = None
        backend.epoch = 0
        backend.observation_valid = True
        backend.observed_at = 10
        backend.observed_sim_seconds = 5.
        backend.observation = {'agent_pos': np.zeros(14),
                               'pixels': {'top': np.zeros((480, 640, 3), dtype=np.uint8)}}
        with self.assertRaisesRegex(RuntimeError, 'reset changed'):
            backend.reset(seed=1)
        self.assertFalse(backend.observe()['valid'])
        with self.assertRaisesRegex(ValueError, 'observation invalid'):
            backend.sample()

    def test_recording_failure_still_attempts_both_closures_and_frees_physics(self):
        backend = AlohaBackend.__new__(AlohaBackend)
        backend.closed = False
        backend.env = Mock()
        physics = backend.env.unwrapped._env.physics
        backend.video, backend.trace = Mock(), Mock()
        backend.video.close.side_effect = OSError('encoder close failed')
        with self.assertRaisesRegex(RuntimeError, 'closure unconfirmed'):
            backend.close()
        self.assertIsNone(backend.trace)
        physics.free.assert_called_once()
        self.assertFalse(backend.closed)
        backend.video.close.side_effect = None
        backend.close()
        self.assertTrue(backend.closed)
        physics.free.assert_called_once()


if __name__ == '__main__':
    unittest.main()
