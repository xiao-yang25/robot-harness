"""Fixed optional inspection configuration; never a task or execution authority."""
import math
import time

CASES = ('normal', 'recheck')
TOLERANCE = .15
YAW_TOLERANCE = math.radians(3)
PARAMETERS = ('goal_checker_plugins', 'general_goal_checker.plugin',
              'general_goal_checker.stateful', 'general_goal_checker.yaw_goal_tolerance',
              'general_goal_checker.xy_goal_tolerance', 'FollowPath.xy_goal_tolerance')


def validate_selection(case, profile, scene):
    from ._profile import PROFILE_ID
    if case is not None and (case not in CASES or profile != PROFILE_ID or scene != 'normal'):
        raise ValueError('inspection requires normal map and default navigation profile')


def validate_values(values):
    # Require typed ROS values, including the selected checker's identity.
    expected = ((9, 'string_array_value', ['general_goal_checker']),
                (4, 'string_value', 'nav2_controller::SimpleGoalChecker'),
                (1, 'bool_value', True), (3, 'double_value', YAW_TOLERANCE),
                (3, 'double_value', TOLERANCE), (3, 'double_value', TOLERANCE))
    if len(values) != len(expected):
        raise ValueError('actual inspection controller response incomplete')
    result = {}
    for name, value, (kind, field, wanted) in zip(PARAMETERS, values, expected):
        actual = getattr(value, field)
        if value.type != kind or actual != wanted:
            raise ValueError('actual inspection controller configuration differs: ' + name)
        result[name] = list(actual) if kind == 9 else actual
    return result


def verify_controllers(owner):
    """One shared startup deadline; no retry, no Core or public endpoint yet."""
    from rcl_interfaces.srv import GetParameters
    from ._observations import emit
    if owner.motion_started or owner.execution is not None or owner.endpoint is not None:
        raise RuntimeError('inspection configuration must precede public admission')
    until = min(owner.deadline, time.monotonic()+4)
    def remaining():
        if owner.motion_started or owner.execution is not None or owner.endpoint is not None:
            raise RuntimeError('inspection configuration must precede public admission')
        value = until-time.monotonic()
        if value <= 0:
            raise TimeoutError('inspection configuration startup budget expired')
        return value
    rows = []
    for stage, namespace in (('A', ''), ('B', owner.contexts['B']['namespace'])):
        remaining()
        client = owner.create_client(GetParameters, namespace+'/controller_server/get_parameters')
        try:
            owner.wait(client.service_is_ready, remaining())
            remaining()
            request = GetParameters.Request(); request.names = list(PARAMETERS)
            future = client.call_async(request)
            owner.wait(future.done, remaining())
            remaining()
            values = validate_values(future.result().values)
            remaining()
            rows.append(dict(stage=stage, namespace=namespace, values=values))
        finally:
            owner.destroy_client(client)
    emit('inspection_configuration_ready', case=owner.inspection_case,
         checked_before_public_admission=True, controllers=rows)
