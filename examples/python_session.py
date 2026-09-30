"""Consume the optional installed package using two operations in one session."""

import json
import time

from robot_harness import Session


def wait_result(session, reference):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        result = session.status(reference)
        if result['state'] == 'finished':
            if result['receipt']['settlement'] != 'settled' or result['result'] is None:
                raise RuntimeError(result)
            return result
        time.sleep(0.01)
    raise TimeoutError(reference)


with Session() as session:
    session.submit('first', steps=7)
    first = wait_result(session, 'first')
    session.reset()
    session.submit('second', steps=4)
    second = wait_result(session, 'second')
    assert session.status('first') == first
    assert first['result']['value'] == 7 and second['result']['value'] == 4
    print(json.dumps({'host_pid': session.host_pid, 'worker_pid': session.worker_pid,
                      'first': first, 'second': second}, indent=2))
