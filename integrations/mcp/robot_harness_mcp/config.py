"""Operator-owned startup configuration; never accepted as tool arguments."""

from dataclasses import dataclass
import json
from pathlib import Path


def bounded_integer(value, name, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f'{name} must be an integer in {minimum}..{maximum}')
    return value


@dataclass(frozen=True)
class Config:
    backend: str = 'fixture'
    worker_delay_ms: int = 0
    startup_timeout: float = 5.0
    worker_script: str | None = None
    checkpoint: str | None = None
    device: str = 'cpu'
    seed: int = 0

    def __post_init__(self):
        if self.backend not in ('fixture', 'mujoco'):
            raise ValueError('backend must be fixture or mujoco')
        bounded_integer(self.worker_delay_ms, 'worker_delay_ms', 0, 10000)
        bounded_integer(self.seed, 'seed', 0, 4)
        if (type(self.startup_timeout) not in (int, float) or
                not 0 < self.startup_timeout <= 120):
            raise ValueError('startup_timeout must be in (0, 120] seconds')
        if self.backend == 'mujoco':
            for name in ('worker_script', 'checkpoint'):
                value = getattr(self, name)
                if not isinstance(value, str) or not Path(value).is_absolute():
                    raise ValueError(f'{name} must be an absolute path')
            if not Path(self.worker_script).is_file() or not Path(self.checkpoint).is_dir():
                raise ValueError('worker_script file and checkpoint directory must exist')
            if not isinstance(self.device, str) or not self.device:
                raise ValueError('device must be a nonempty string')

    @classmethod
    def read(cls, path):
        if path is None:
            return cls()
        values = json.loads(Path(path).read_text())
        if not isinstance(values, dict) or 'backend' not in values:
            raise ValueError('config must be an object naming its backend')
        # Unknown optional metadata is not an execution or compatibility failure.
        return cls(**{name: values[name] for name in cls.__dataclass_fields__ if name in values})

    def session_options(self, output):
        options = {'startup_timeout': self.startup_timeout}
        if self.backend == 'fixture':
            options['worker_delay_ms'] = self.worker_delay_ms
        else:
            options['mujoco'] = {
                'output': str(Path(output) / 'episodes'), 'seed': self.seed,
                'worker_script': self.worker_script, 'checkpoint': self.checkpoint,
                'device': self.device,
            }
        return options
