"""Finite, deterministic local effects used to validate session ownership."""


class IncrementBackend:
    capability = 'fixture.increment'
    profile = 'deterministic-session-v1'
    provider = 'deterministic-worker'
    domain = 'fixture-counter'
    step_interval = 0.005

    def __init__(self):
        self.value = 0
        self.steps = 0
        self.epoch = 0
        self.closed = False

    def step(self, action):
        if self.closed or type(action) is not int or action != 1:
            raise ValueError('invalid fixture action')
        self.value += action
        self.steps += 1

    def reset(self):
        if self.closed:
            raise RuntimeError('backend closed')
        self.value = self.steps = 0
        self.epoch += 1

    def observe(self):
        return {'epoch': self.epoch, 'sequence': self.steps, 'value': self.value}

    def close(self):
        self.closed = True

    def sample(self):
        return self.observe(), b''

    def validate_actions(self, actions, count):
        if not isinstance(actions, list) or len(actions) != count or any(type(a) is not int or a != 1 for a in actions):
            raise ValueError('invalid candidate chunk')

    def completion(self, limit):
        return 'completed' if self.steps == limit else None

    def finish(self, reason):
        return {'value': self.value, 'steps': self.steps}
