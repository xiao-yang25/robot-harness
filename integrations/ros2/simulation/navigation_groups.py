"""Bounded cleanup of the fixed Session supervisor's own Linux process groups."""
import json
import math
import os
from pathlib import Path
import signal
import sys
import time


def processes():
    rows = {}
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():
            continue
        try:
            text = (path / 'stat').read_text()
        except (FileNotFoundError, ProcessLookupError):
            continue
        end = text.rfind(')')
        fields = text[end + 2:].split()
        if end < 0 or len(fields) < 4:
            raise RuntimeError('cannot observe process membership')
        state, parent, group, session = fields[:4]
        if state not in ('Z', 'X'):
            pid = int(path.name)
            rows[pid] = dict(pid=pid, ppid=int(parent), pgid=int(group),
                             sid=int(session), state=state)
    return rows


def members(pid, parent, rows):
    group = [row for row in rows.values() if row['pgid'] == pid]
    if any(row['sid'] != pid for row in group):
        raise RuntimeError('group is not the owned private session')
    leader = rows.get(pid)
    if leader and leader['ppid'] != parent:
        raise RuntimeError('leader is not a supervisor child')
    # setsid may not have executed yet during partial startup.
    if leader and leader not in group:
        group.append(leader)
    return group


def stop_groups(roots, parent, deadline):
    started = time.monotonic()
    if (not math.isfinite(deadline) or deadline - started > 4
            or len(set(roots.values())) != len(roots)
            or any(pid <= 1 or pid in (parent, os.getpid(), os.getpgrp())
                   for pid in roots.values())):
        raise ValueError('invalid owned cleanup request')
    grace = max(started, deadline - .5)
    facts = [dict(role=role, pid=pid, forced=False) for role, pid in roots.items()]
    errors = {}

    def observe():
        try:
            rows = processes()
        except (OSError, ValueError, RuntimeError) as error:
            errors['observation'] = str(error)
            return [None for _ in facts]
        result = []
        for fact in facts:
            try:
                result.append(members(fact['pid'], parent, rows))
            except (ValueError, RuntimeError) as error:
                errors[fact['role']] = str(error)
                result.append(None)
        return result

    def send(fact, live, signum):
        if not live:
            return
        targets = []
        if any(row['pgid'] == fact['pid'] for row in live):
            targets.append(-fact['pid'])
        if any(row['pid'] == fact['pid'] and row['pgid'] != fact['pid'] for row in live):
            targets.append(fact['pid'])
        for target in targets:
            try:
                os.kill(target, signum)
            except ProcessLookupError:
                pass
            except OSError as error:
                errors[fact['role']] = str(error)

    for fact, live in zip(facts, observe()):
        fact['before'] = live
        send(fact, live, signal.SIGINT)
    while time.monotonic() < grace and any(observe()):
        time.sleep(min(.05, max(0., grace - time.monotonic())))
    for fact, live in zip(facts, observe()):
        fact['after_grace'] = live
        if live:
            fact['forced'] = True
            send(fact, live, signal.SIGKILL)
    while time.monotonic() < deadline and any(observe()):
        time.sleep(min(.02, max(0., deadline - time.monotonic())))
    for fact, live in zip(facts, observe()):
        fact['remaining'] = live
    return dict(groups=facts, errors=errors, elapsed_seconds=time.monotonic() - started,
                clean=not errors and all(not f['forced'] and f['remaining'] == [] for f in facts))


if __name__ == '__main__':
    output, parent, deadline, *pairs = sys.argv[1:]
    roots = dict(pair.split('=') for pair in pairs)
    result = stop_groups({role: int(pid) for role, pid in roots.items()}, int(parent), float(deadline))
    Path(output).write_text(json.dumps(result, indent=2) + '\n')
    raise SystemExit(0 if result['clean'] else 1)
