"""A→B example for a prepared two-context owner; does not launch ROS.

The owner must register A and B. A must settle before B; final B may remain pending.
"""
import argparse
import json
import time

from robot_harness import NavigationSession


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('endpoint', help='private Unix socket provided by the owner')
    args = parser.parse_args()
    with NavigationSession(args.endpoint) as session:
        print(json.dumps(session.capabilities(), indent=2))
        previous = None
        for site in ('A', 'B'):
            observed = session.observe()
            # Keep these exact arguments to retry this request after a timeout.
            proposal = dict(site=site, expected_observation=observed['reference'])
            decision = session.submit(site, **proposal)
            if decision['state'] == 'rejected':
                raise RuntimeError(decision.get('reason', 'request rejected'))
            if previous is not None:
                session.cancel(previous)
            deadline = time.monotonic()+160
            while time.monotonic() < deadline:
                row = session.status(site)
                if (row.get('result') is not None and
                        (site == 'B' or row['receipt']['settlement'] == 'settled')):
                    print(json.dumps(row, indent=2))
                    break
                time.sleep(.1)
            else:
                raise TimeoutError('outcome unknown; query this request before resubmitting')
            previous = site


if __name__ == '__main__':
    main()
