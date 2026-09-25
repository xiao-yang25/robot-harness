"""Focused independent checks of the new cancellation/perception evidence path."""
import copy
import unittest
from check_obstacle_wait import verify_observations


def records(frozen=False):
    mode = 'obstacle-wait-frozen-scan' if frozen else 'obstacle-wait'
    owner = [dict(event='obstacle_baseline_clear', scan_stamp_ns=500000000,
                  costmap_stamp_ns=500000000),
             dict(event='obstacle_wait_requested', reason='unknown' if frozen else 'blocked',
                  steady_ms=4000, sim_ns=2800000000, scan_progress_age_ms=3000 if frozen else 100,
                  scan_stamp_ns=1000000000 if frozen else 2700000000,
                  costmap_stamp_ns=2500000000, scan_fresh=not frozen, costmap_fresh=True,
                  costmap_occupied=True, clearance_m=.8),
             dict(event='owner_cancel_requested', goal_uuid='goal-a', operation_id=1, steady_ms=4100),
             dict(event='owner_cancel_response', goal_uuid='goal-a', operation_id=1,
                  steady_ms=4200, acknowledged=True),
             dict(event='native_result', goal_uuid='goal-a', operation_id=1, steady_ms=4300, result_code=5),
             dict(event='owner_cancellation_result', case=mode, passed=True, native_closed=True,
                  motion_observed=True, settled=False, output_delivered=False,
                  second_admission_blocked=True, native_send_count=1, native_cancel_count=1,
                  steady_ms=6000)]
    fixture = [dict(event='obstacle_spawn_requested', frozen_scan=frozen,
                    frozen_scan_stamp_ns=1000000000 if frozen else None),
               dict(event='obstacle_spawned', steady_ns=2000000000)]
    return owner, fixture, mode


class ObstacleEvidenceTest(unittest.TestCase):
    def test_distinct_blocked_and_scan_expired_outcomes(self):
        for frozen, expected in [(False, 'blocked'), (True, 'unknown')]:
            self.assertEqual(verify_observations(*records(frozen)), expected)

    def test_summary_cannot_replace_cancel_chain(self):
        owner, fixture, mode = records()
        with self.assertRaises(ValueError):
            verify_observations(owner[:2] + owner[3:], fixture, mode)
        for key, value in [('goal_uuid', 'goal-b'), ('acknowledged', False), ('steady_ms', 3900)]:
            changed = copy.deepcopy(owner)
            changed[3][key] = value
            with self.assertRaises(AssertionError):
                verify_observations(changed, fixture, mode)

    def test_success_result_is_not_cancelled_terminal(self):
        owner, fixture, mode = records()
        owner[4]['result_code'] = 4
        with self.assertRaises(AssertionError):
            verify_observations(owner, fixture, mode)

    def test_unknown_must_be_the_frozen_scan_expiring(self):
        owner, fixture, mode = records(True)
        changed = copy.deepcopy(owner)
        changed[1]['scan_stamp_ns'] += 1
        with self.assertRaises(AssertionError):
            verify_observations(changed, fixture, mode)
        owner[1]['scan_progress_age_ms'] = 100
        owner[1]['sim_ns'] = 1100000000
        with self.assertRaises(AssertionError):
            verify_observations(owner, fixture, mode)


if __name__ == '__main__':
    unittest.main()
