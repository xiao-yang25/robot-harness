"""Fixed, opt-in terminal query lifetime; never grants execution permission."""
import math
import time

from robot_harness._transport import identifier


class TerminalWindow:
    def __init__(self, seconds, clock=time.monotonic):
        if type(seconds) not in (int, float) or not math.isfinite(seconds) or not 0 < seconds <= 10:
            raise ValueError('terminal query wait must be finite and in (0, 10] seconds')
        self.seconds, self.clock = seconds, clock
        self.deadline = None

    def begin(self):
        if self.deadline is None:
            self.deadline = self.clock() + self.seconds

    @property
    def started(self):
        return self.deadline is not None

    @property
    def expired(self):
        return self.started and self.clock() >= self.deadline

    def command(self, requests, message):
        if self.started:
            command = message.get('command')
            if command == 'observe':
                raise ValueError('terminal owner does not issue new observations')
            if command == 'submit' and identifier(message.get('request_id'), 'request_id') not in requests.execution.records:
                # Reject before reserve: terminal callers cannot grow the ledger.
                raise ValueError('terminal owner does not accept new requests')
            if command == 'capabilities':
                result = requests.capabilities()
                result['available'] = False
                return result
        return requests.command(message)
