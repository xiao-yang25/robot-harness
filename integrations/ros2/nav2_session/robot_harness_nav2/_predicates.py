"""Online observation predicates for the bounded Nav2 simulation profile."""
import math

GOALS = {'A': (0.7, -0.5), 'B': (-1.5, -0.5)}


def stream_at_clock(history, clock, now, last_advance):
    eligible = [(stamp, received) for stamp, received in history if stamp <= clock]
    if not eligible:
        return (0.0, 0.0, 0.0)
    stamp, received = eligible[-1]
    return (stamp, now-received, now-last_advance)


def observation_valid(clock, clock_age, streams, pose, pose_stamp):
    if not math.isfinite(clock) or clock <= 0 or not 0 <= clock_age <= 1.0:
        return False
    for stamp, receipt_age, advance_age in streams:
        if not all(math.isfinite(v) for v in (stamp, receipt_age, advance_age)):
            return False
        if not (stamp > 0 and 0 <= clock - stamp <= 0.5 and 0 <= receipt_age <= 1.0
                and 0 <= advance_age <= 1.0):
            return False
    return (len(streams) == 2 and len(pose) == 2
            and all(math.isfinite(v) for v in (*pose, pose_stamp))
            and 0 <= clock - pose_stamp <= 0.5)


def quiet_window(samples, *, after):
    """Return the first complete finite window; None means still unobserved."""
    first = previous = None
    window = []
    for row in samples:
        stamp, clock = row['stamp'], row['clock']
        if stamp <= after:
            continue
        if (not all(math.isfinite(row[k]) for k in
                    ('stamp', 'clock', 'clock_age', 'receive_age', 'advance_age',
                     'x', 'y', 'yaw', 'speed', 'angular_speed'))
                or not 0 <= clock-stamp <= .25
                or not all(0 <= row[k] <= 1 for k in ('clock_age', 'receive_age', 'advance_age'))
                or row['speed'] < 0):
            raise ValueError('invalid quiet observation')
        if previous and not 0 < stamp-previous['stamp'] <= .25:
            raise ValueError('non-advancing or gapped quiet observation')
        previous = row
        if first is None:
            if row['speed'] > .01 or abs(row['angular_speed']) > .02:
                continue
            first = row
        yaw_delta = math.atan2(math.sin(row['yaw']-first['yaw']), math.cos(row['yaw']-first['yaw']))
        if (math.hypot(row['x']-first['x'], row['y']-first['y']) > .01
                or abs(yaw_delta) > .02 or row['speed'] > .01 or abs(row['angular_speed']) > .02):
            raise ValueError('motion inside quiet window')
        window.append(row)
        if len(window) >= 10 and stamp-first['stamp'] >= 1:
            return window
    return None
