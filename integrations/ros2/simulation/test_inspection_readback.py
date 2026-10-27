"""Focused controlled tests; no ROS or controller runtime qualification."""
import math
from types import SimpleNamespace as N
import unittest
from pathlib import Path
import sys
from unittest.mock import patch, Mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'nav2_session'))
from robot_harness_nav2._inspection import verify_controllers


class Service:
    class Request:
        pass


def valid():
    return [N(type=9, string_array_value=['general_goal_checker']),
            N(type=4, string_value='nav2_controller::SimpleGoalChecker'),
            N(type=1, bool_value=True), N(type=3, double_value=math.radians(3)),
            N(type=3, double_value=.15), N(type=3, double_value=.15)]


class Owner:
    motion_started = False
    execution = endpoint = None
    deadline = 100.
    inspection_case = 'normal'
    contexts = {'B': {'namespace': '/second'}}

    def __init__(self, responses=None, delay=.1, unavailable=False):
        self.responses = responses or [valid(), valid()]
        self.time = 0.
        self.delay, self.unavailable = delay, unavailable
        self.created, self.destroyed, self.requests, self.waits = [], [], [], []

    def create_client(self, service, path):
        index = len(self.created)
        self.created.append(path)
        def call(request):
            self.requests.append(request.names)
            return N(done=lambda: True, result=lambda: N(values=self.responses[index]))
        return N(service_is_ready=lambda: not self.unavailable, call_async=call, path=path)

    def destroy_client(self, client):
        self.destroyed.append(client.path)

    def wait(self, predicate, timeout):
        self.waits.append(timeout)
        self.time += self.delay
        if self.delay > timeout or not predicate():
            raise TimeoutError('controlled service timeout')


def run(owner):
    emit = Mock()
    with patch.dict(sys.modules, {'rcl_interfaces.srv': N(GetParameters=Service),
                                 'robot_harness_nav2._observations': N(emit=emit)}), \
            patch('robot_harness_nav2._inspection.time.monotonic', side_effect=lambda: owner.time):
        try:
            verify_controllers(owner)
        except BaseException:
            emit.assert_not_called()
            raise
        return emit.call_args.kwargs['controllers']



class HeadingCheckTests(unittest.TestCase):
    def test_both_controllers_and_requested_fields(self):
        o = Owner(); rows = run(o)
        self.assertEqual([r['stage'] for r in rows], ['A', 'B'])
        self.assertEqual(o.created, ['/controller_server/get_parameters', '/second/controller_server/get_parameters'])
        self.assertEqual(o.created, o.destroyed)
        self.assertIn('general_goal_checker.yaw_goal_tolerance', o.requests[0])
        self.assertIn('goal_checker_plugins', o.requests[1])
        self.assertAlmostEqual(rows[1]['values']['general_goal_checker.yaw_goal_tolerance'], math.pi/60)

    def test_second_controller_mismatch_never_returns_partial_success(self):
        values = valid(); values[3].double_value = .25
        o = Owner([valid(), values])
        with self.assertRaises(ValueError): run(o)
        self.assertEqual(o.created, o.destroyed)
        self.assertEqual(len(o.created), 2)

    def test_important_wire_and_selection_failures(self):
        cases = []
        for index, field, value in ((0, 'string_array_value', ['general_goal_checker', 'other']),
                (1, 'string_value', 'OtherChecker'), (2, 'bool_value', False),
                (3, 'type', 0), (3, 'double_value', float('nan')),
                (4, 'double_value', .25), (5, 'double_value', .25)):
            values = valid(); setattr(values[index], field, value); cases.append(values)
        cases.append(valid()[:-1])
        for values in cases:
            with self.subTest(values=values):
                o = Owner([values, valid()])
                with self.assertRaises(ValueError): run(o)
                self.assertEqual(len(o.created), 1)
                self.assertEqual(o.created, o.destroyed)

    def test_shared_deadline_not_per_controller(self):
        o = Owner(delay=1.1)
        with self.assertRaises(TimeoutError): run(o)
        self.assertLess(o.waits[-1], 1.)
        self.assertEqual(o.created, o.destroyed)

    def test_owner_deadline_is_tighter(self):
        o = Owner(delay=.6); o.deadline = 1.
        with self.assertRaises(TimeoutError): run(o)
        self.assertEqual(len(o.created), 1)
        self.assertEqual(o.created, o.destroyed)

    def test_service_unavailable_cleanup(self):
        o = Owner(unavailable=True)
        with self.assertRaises(TimeoutError): run(o)
        self.assertEqual(o.created, o.destroyed)

    def test_admission_already_open(self):
        for field, value in (('motion_started', True), ('endpoint', object()), ('execution', object())):
            o = Owner(); setattr(o, field, value)
            with self.assertRaises(RuntimeError): run(o)
            self.assertEqual(o.created, [])

    def test_admission_opens_during_wait(self):
        o = Owner(); original = o.wait
        def wait(predicate, timeout):
            original(predicate, timeout); o.motion_started = True
        o.wait = wait
        with self.assertRaises(RuntimeError): run(o)
        self.assertEqual(o.requests, [])
        self.assertEqual(o.created, o.destroyed)

    def test_response_error_cleans_client(self):
        class BrokenOwner(Owner):
            def create_client(self, service, path):
                client = super().create_client(service, path)
                def fail(_): raise RuntimeError('transport failure')
                client.call_async = fail
                return client
        o = BrokenOwner()
        with self.assertRaises(RuntimeError): run(o)
        self.assertEqual(o.created, o.destroyed)


if __name__ == '__main__': unittest.main()
