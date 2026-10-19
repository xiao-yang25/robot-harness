"""Fixed optional inspection configuration; never a task or execution authority."""
import time

CASES = ('normal', 'recheck')
TOLERANCE = .15
PARAMETERS = ('general_goal_checker.xy_goal_tolerance', 'FollowPath.xy_goal_tolerance')


def validate_selection(case, profile, scene):
    from ._profile import PROFILE_ID
    if case is not None and (case not in CASES or profile != PROFILE_ID or scene != 'normal'):
        raise ValueError('inspection requires normal map and default navigation profile')


def validate_values(values):
    # ROS ParameterType.PARAMETER_DOUBLE is3. Do not coerce another wire type.
    if len(values) != 2 or any(v.type != 3 or v.double_value != TOLERANCE for v in values):
        raise ValueError('actual inspection controller tolerance differs')
    return dict(zip(PARAMETERS, (v.double_value for v in values)))


def verify_controllers(owner):
    """One shared startup deadline; no retry, no Core or public endpoint yet."""
    from rcl_interfaces.srv import GetParameters
    from ._observations import emit
    if owner.motion_started or owner.execution is not None or owner.endpoint is not None:
        raise RuntimeError('inspection configuration must precede public admission')
    until = min(owner.deadline, time.monotonic()+4)
    def remaining():
        value = until-time.monotonic()
        if value <= 0:
            raise TimeoutError('inspection configuration startup budget expired')
        return value
    rows = []
    for stage, namespace in (('A', ''), ('B', owner.contexts['B']['namespace'])):
        client = owner.create_client(GetParameters, namespace+'/controller_server/get_parameters')
        try:
            owner.wait(client.service_is_ready, remaining())
            request = GetParameters.Request(); request.names = list(PARAMETERS)
            future = client.call_async(request)
            owner.wait(future.done, remaining())
            values = validate_values(future.result().values)
            remaining()
            rows.append(dict(stage=stage, namespace=namespace, values=values))
        finally:
            owner.destroy_client(client)
    emit('inspection_configuration_ready', case=owner.inspection_case,
         checked_before_public_admission=True, controllers=rows)
