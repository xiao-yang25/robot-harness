"""Regression checks for post-wait evidence and operation identity."""
import copy
import unittest
from test_obstacle_wait import records
from check_obstacle_resume import verify_resume


def recovery_records():
    owner, fixture, _ = records()
    owner = owner[:-1]
    owner.insert(1, dict(event='accepted', goal_uuid='goal-a', operation_id=1))
    waiting = dict(event='obstacle_waiting', steady_ms=6000, after_ns=4000000000)
    clear = dict(event='obstacle_resume_observation', steady_ms=9500, ready=True,
                 scan_fresh=True, costmap_fresh=True, scan_stamp_ns=6000000000,
                 costmap_stamp_ns=5500000000, clearance_m=2., costmap_occupied=False)
    owner += [waiting, clear,
              dict(clear, event='obstacle_resume_settlement_check', steady_ms=10000),
              dict(event='owner_core_handoff', steady_ms=10100, a_cancelled=True,
                   a_output_delivered=False, a_settled=True, b_admitted=True,
                   a_operation=1, b_operation=2),
              dict(clear, event='obstacle_resume_dispatch_check', steady_ms=10200),
              dict(event='accepted', goal_uuid='goal-b', operation_id=2),
              dict(event='native_result', goal_uuid='goal-b', operation_id=2, result_code=4),
              dict(event='owner_handoff_result', passed=True, native_send_count=2,
                   native_cancel_count=1, b_native_closed=True, b_settled=False,
                   third_admission_blocked=True)]
    fixture += [dict(event='obstacle_remove_requested', steady_ns=8000000000,
                     frozen_scan_stamp_ns=None),
                dict(event='obstacle_removed', steady_ns=8100000000)]
    return owner, fixture


class ObstacleResumeTest(unittest.TestCase):
    def test_new_identity_after_clear_and_settlement(self):
        self.assertTrue(verify_resume(*recovery_records(), 'obstacle-resume'))

    def test_clear_before_wait_and_loss_before_dispatch_rejected(self):
        for event, field, value in [('obstacle_resume_observation', 'scan_stamp_ns', 4000000000),
                                    ('obstacle_resume_dispatch_check', 'ready', False),
                                    ('obstacle_resume_settlement_check', 'costmap_occupied', True)]:
            owner, fixture = recovery_records()
            next(r for r in owner if r['event'] == event)[field] = value
            with self.assertRaises(AssertionError):
                verify_resume(owner, fixture, 'obstacle-resume')

    def test_same_operation_or_goal_cannot_resume(self):
        for key, value in [('operation_id', 1), ('goal_uuid', 'goal-a')]:
            owner, fixture = recovery_records()
            [r for r in owner if r['event'] == 'accepted'][1][key] = value
            with self.assertRaises((AssertionError, ValueError)):
                verify_resume(owner, fixture, 'obstacle-resume')

    def test_dispatch_refusal_requires_revocation_and_applied_isolation(self):
        owner, fixture = recovery_records()
        owner = owner[:-3]
        owner[-1].update(ready=False, scan_fresh=False)
        owner.append(dict(event='owner_resume_dispatch_refused', passed=True, b_operation=2,
                          b_submitted=False, b_revoked=True, b_settled=False,
                          drive_isolated=True, motion_observed=True, native_send_count=1))
        self.assertFalse(verify_resume(owner, fixture, 'obstacle-resume-dispatch-scan-loss'))
        for field in ['b_revoked', 'drive_isolated', 'motion_observed']:
            bad = copy.deepcopy(owner)
            bad[-1][field] = False
            with self.assertRaises(AssertionError):
                verify_resume(bad, fixture, 'obstacle-resume-dispatch-scan-loss')

    def test_frozen_and_missing_context_cannot_handoff(self):
        for mode, boundary in [('obstacle-resume-frozen-scan', 'clear-corridor'),
                               ('obstacle-resume-missing-controller', 'next-context')]:
            owner, fixture = recovery_records()
            owner = owner[:8]
            if mode.endswith('frozen-scan'):
                owner[-1].update(ready=False, scan_fresh=False)
                fixture[-2]['frozen_scan_stamp_ns'] = owner[-1]['scan_stamp_ns']
            owner.append(dict(event='owner_handoff_blocked', boundary=boundary,
                              a_settled=False, b_admitted=False, native_send_count=1))
            self.assertFalse(verify_resume(owner, fixture, mode))
            altered = copy.deepcopy(owner)
            altered[-1]['b_admitted'] = True
            with self.assertRaises(AssertionError):
                verify_resume(altered, fixture, mode)


if __name__ == '__main__':
    unittest.main()
