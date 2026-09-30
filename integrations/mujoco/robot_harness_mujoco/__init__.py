"""Optional, stepped ALOHA backend; no model or Agent dependencies."""

import json
from pathlib import Path
import time


class AlohaBackend:
    capability = 'aloha.transfer_cube_trial'
    profile = 'mujoco-stepped-episode-v1'
    provider = 'act-worker'
    domain = 'aloha-simulator'
    step_interval = 0

    def __init__(self, output, seed=0):
        import gymnasium as gym
        import gym_aloha  # noqa: F401
        self.root = Path(output)
        self.root.mkdir(parents=True, exist_ok=False)
        self.env = self.video = self.trace = None
        self.closed = False
        self.epoch = -1
        try:
            self.env = gym.make('gym_aloha/AlohaTransferCube-v0',
                                obs_type='pixels_agent_pos', max_episode_steps=400)
            self.reset(seed=seed)
        except BaseException:
            self.close()
            raise

    def facts(self):
        physics = self.env.unwrapped._env.physics
        return {'sim_seconds': float(physics.data.time),
                'qpos': physics.data.qpos.tolist(), 'qvel': physics.data.qvel.tolist(),
                'ctrl': physics.data.ctrl.tolist(),
                'contacts': [[physics.model.id2name(int(c.geom1), 'geom'),
                              physics.model.id2name(int(c.geom2), 'geom')]
                             for c in physics.data.contact[:physics.data.ncon]]}

    def write(self, event):
        self.trace.write(json.dumps(event, allow_nan=False) + '\n')
        self.trace.flush()

    def reset(self, *, seed=None):
        if self.closed:
            raise RuntimeError('backend closed')
        self.observation_valid = False
        self.close_recording()
        self.epoch += 1
        self.seed = self.epoch if seed is None else seed
        self.steps = 0
        self.reward = self.max_reward = 0
        self.terminated = self.truncated = self.success = False
        self.error = None
        self.summary = None
        self.started = time.monotonic()
        self.observation, _ = self.env.reset(seed=self.seed)
        self.observed_at = time.monotonic_ns()
        self.observed_sim_seconds = float(self.env.unwrapped._env.physics.data.time)
        self.validate_observation()
        import imageio.v2 as imageio
        self.trace = (self.root / f'episode-{self.epoch}.jsonl').open('x')
        self.video = imageio.get_writer(self.root / f'episode-{self.epoch}.mp4', fps=50)
        self.video.append_data(self.observation['pixels']['top'])
        self.write({'event': 'reset', 'seed': self.seed, **self.facts()})
        self.observation_valid = True

    def validate_observation(self):
        import numpy as np
        frame, joints = self.observation['pixels']['top'], self.observation['agent_pos']
        if frame.shape != (480, 640, 3) or frame.dtype != np.uint8 or joints.shape != (14,) or not np.isfinite(joints).all():
            raise ValueError('invalid ALOHA observation')

    def observe(self):
        # Ground-truth cube pose and contacts are evaluator-only trace data.
        return {'epoch': self.epoch, 'sequence': self.steps,
                'sim_seconds': self.observed_sim_seconds, 'valid': self.observation_valid,
                'observed_at_ns': self.observed_at,
                'joints': self.observation['agent_pos'].tolist(),
                'image': {'shape': [480, 640, 3], 'dtype': 'uint8', 'camera': 'top'}}

    def sample(self):
        if not self.observation_valid:
            raise ValueError('observation invalid after backend failure')
        return self.observe(), self.observation['pixels']['top'].tobytes()

    def validate_actions(self, actions, count):
        import numpy as np
        if (not isinstance(actions, list) or len(actions) != count or
                any(not isinstance(a, list) or len(a) != 14 or
                    any(type(v) not in (int, float) for v in a) for a in actions)):
            raise ValueError('invalid ACT candidate chunk')
        try:
            with np.errstate(over='ignore', invalid='ignore'):
                native = np.asarray(actions, dtype=np.float32)
        except (ValueError, OverflowError) as error:
            raise ValueError('candidate is not representable as float32') from error
        if not np.isfinite(native).all():
            raise ValueError('nonfinite float32 candidate')

    def step(self, action):
        import numpy as np
        if self.closed or self.summary is not None or self.terminated or self.truncated:
            raise RuntimeError('episode is not executable')
        self.write({'event': 'action_attempt', 'step': self.steps + 1, 'action': action})
        try:
            observation, reward, terminated, truncated, info = self.env.step(np.asarray(action, dtype=np.float32))
            self.steps += 1
            self.observation = observation
            self.observed_at = time.monotonic_ns()
            self.observed_sim_seconds = float(self.env.unwrapped._env.physics.data.time)
            self.reward = float(reward)
            self.max_reward = max(self.max_reward, self.reward)
            self.terminated, self.truncated = bool(terminated), bool(truncated)
            self.success = bool(info['is_success'])
            self.validate_observation()
            self.write({'event': 'step', 'step': self.steps, 'action': action,
                        'reward': self.reward, 'is_success': self.success,
                        'terminated': self.terminated, 'truncated': self.truncated,
                        **self.facts()})
            self.video.append_data(self.observation['pixels']['top'])
        except BaseException as error:
            self.observation_valid = False
            self.error = f'{type(error).__name__}: {error}'
            failed = {'event': 'step_error', 'error': self.error, 'step': self.steps}
            try:
                failed.update(self.facts())
            except Exception as observation_error:
                failed['observation_error'] = str(observation_error)
            self.write(failed)
            raise

    def completion(self, limit):
        if self.terminated:
            return 'completed' if self.success else 'native_failure'
        if self.truncated or self.steps >= limit:
            return 'step_limit'
        return None

    def finish(self, reason):
        if self.summary is None:
            summary = {'native_reason': 'native_condition_reached' if reason == 'completed' else reason,
                       'steps': self.steps, 'seed': self.seed, 'max_reward': self.max_reward,
                       'task': {'full_handoff': 'unassessed', 'stable_holding': 'unassessed'},
                       'wall_seconds': time.monotonic() - self.started,
                       'video': str(self.root / f'episode-{self.epoch}.mp4'), 'error': self.error}
            self.close_recording()
            (self.root / f'episode-{self.epoch}.json').write_text(json.dumps(summary, indent=2) + '\n')
            self.summary = summary
        return self.summary

    def close_recording(self):
        errors = []
        for name in ('video', 'trace'):
            resource = getattr(self, name)
            if resource is not None:
                try:
                    resource.close()
                    setattr(self, name, None)
                except Exception as error:
                    errors.append(error)
        if errors:
            raise RuntimeError(f'recording closure unconfirmed: {errors}')

    def close(self):
        if self.closed:
            return
        try:
            self.close_recording()
        finally:
            if self.env is not None:
                self.env.unwrapped._env.physics.free()
                self.env.close()
                self.env = None
        self.closed = True
