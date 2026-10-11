"""Keep ROS context alive until the Owner has finished local cleanup."""
import signal


class ShutdownSignals:
    def __init__(self):
        self.requested = None
        self.cleaning = False
        self.previous = {}

    def __enter__(self):
        for kind in (signal.SIGINT, signal.SIGTERM):
            self.previous[kind] = signal.getsignal(kind)
            signal.signal(kind, self._request)
        return self

    def _request(self, kind, frame):
        if self.requested is None:
            self.requested = kind
            if not self.cleaning:
                raise KeyboardInterrupt(f'Owner stop requested: {signal.Signals(kind).name}')

    def begin_cleanup(self):
        # Further interrupts are latched; the supervisor owns the outer deadline.
        self.cleaning = True

    def __exit__(self, kind, value, traceback):
        for signum, handler in self.previous.items():
            signal.signal(signum, handler)
