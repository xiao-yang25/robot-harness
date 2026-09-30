"""Protocol test barrier, followed by the real Core-backed Session."""

from pathlib import Path
import sys
import time

from robot_harness import Session
from robot_harness_mcp.server import Bridge, make_server


root = Path(sys.argv[1])


def blocked_session(**options):
    (root / 'startup-entered').touch()
    deadline = time.monotonic() + 10
    while not (root / 'release-startup').exists():
        if time.monotonic() > deadline:
            raise TimeoutError('test startup barrier was not released')
        time.sleep(.002)
    return Session(**options)


make_server(Bridge(root / 'server', session_factory=blocked_session)).run(transport='stdio')
