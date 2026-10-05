"""Single fixed native controller policy for the isolated Nav2 Session."""
PROFILE_ID = 'scoped-two-context-nav2-shim-v1'
PERMISSION_PROFILE_ID = 'scoped-two-context-nav2-owner-permit-v1'
REVISION_PROFILE_ID = 'scoped-two-context-nav2-revision-v1'
FAILURE_PROFILE_ID = 'scoped-two-context-nav2-failure-recovery-v1'
PROFILES = (PROFILE_ID, PERMISSION_PROFILE_ID, REVISION_PROFILE_ID, FAILURE_PROFILE_ID)
SCENE_MAP_IDS = {'normal': 'turtlebot3-world-v1',
                 'occupied-a': 'turtlebot3-occupied-a-probe-v1'}


def scene_map_id(scene, profile):
    if scene not in SCENE_MAP_IDS or profile not in PROFILES:
        raise ValueError('unknown navigation scene or profile')
    if scene != 'normal' and profile != FAILURE_PROFILE_ID:
        raise ValueError('occupied-a requires the explicit failure recovery profile')
    return SCENE_MAP_IDS[scene]


def configure_controller(parameters):
    """Add native heading alignment while retaining the existing DWB settings."""
    follow = parameters['controller_server']['ros__parameters']['FollowPath']
    if follow['plugin'] != 'dwb_core::DWBLocalPlanner':
        raise ValueError('fixed navigation profile requires the original DWB controller')
    follow.update(plugin='nav2_rotation_shim_controller::RotationShimController',
                  primary_controller='dwb_core::DWBLocalPlanner',
                  angular_dist_threshold=0.785,
                  angular_disengage_threshold=0.785,
                  forward_sampling_distance=0.5,
                  rotate_to_heading_angular_vel=0.8,
                  max_angular_accel=3.2,
                  simulate_ahead_time=1.0,
                  rotate_to_goal_heading=False,
                  closed_loop=True)
