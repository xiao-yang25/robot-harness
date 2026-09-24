"""Reuse installed launch descriptions, substituting only the native controller."""
import importlib.util
from pathlib import Path
import os
import xml.etree.ElementTree as ET

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescriptionSource
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node


def load(name):
    path = Path(get_package_share_directory('nav2_bringup')) / 'launch' / name
    spec = importlib.util.spec_from_file_location('probe_' + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def generate_launch_description(substitutions=None, command_scope=None,
                                navigation_only=False, context_namespace=None):
    substitutions = substitutions or {'nav2_controller': ('controller_server', 'native_worker_controller')}
    package = Path(get_package_share_directory('nav2_controller')) / 'package.xml'
    if ET.parse(package).findtext('version') != '1.1.20':
        raise RuntimeError('native worker probe requires Nav2 1.1.20')
    nav = load('navigation_launch.py')
    replacements = []

    def native_node(**kwargs):
        if context_namespace is not None:
            map_target = (context_namespace + '/unavailable_map'
                          if os.environ.get('M4_CONTEXT_CASE') in ('missing-map', 'replace-moving-missing-map') else '/map')
            kwargs['namespace'] = context_namespace
            kwargs['remappings'] = [(source, target) for source, target in kwargs.get('remappings', [])
                                    if source not in ('/tf', '/tf_static')]
            kwargs['remappings'].extend([('/tf', '/tf'), ('/tf_static', '/tf_static'),
                                         (context_namespace + '/map', map_target),
                                         (context_namespace + '/odom', '/odom'),
                                         (context_namespace + '/scan', '/scan')])
            kwargs['additional_env'] = {'M4_NATIVE_LOG_DIR': '/output/b'}
        if command_scope is not None:
            import re
            if not re.fullmatch('[0-9a-f]{32}', command_scope) or command_scope == '0' * 32:
                raise RuntimeError('invalid fixed producer scope')
            package_name = kwargs.get('package')
            prefix = '/m4_motion/s_' + command_scope
            if package_name in ('nav2_controller', 'nav2_velocity_smoother'):
                kwargs['remappings'] = [(source, target) for source, target in kwargs['remappings']
                                        if source not in ('cmd_vel', 'cmd_vel_smoothed')]
                kwargs['remappings'].append(('cmd_vel', prefix + '/raw'))
                if package_name == 'nav2_velocity_smoother':
                    kwargs['remappings'].append(('cmd_vel_smoothed', prefix + '/smoothed'))
        if kwargs.get('package') in substitutions:
            original, replacement = substitutions[kwargs['package']]
            if kwargs.get('executable') != original:
                raise RuntimeError('unexpected native launch')
            replacements.append(kwargs['package'])
            kwargs.pop('package')
            kwargs['executable'] = '/native/' + replacement
        return Node(**kwargs)

    nav.Node = native_node
    native_description = nav.generate_launch_description()
    if sorted(replacements) != sorted(substitutions):
        raise RuntimeError('expected each non-composed native substitution exactly once')
    if navigation_only:
        return native_description
    bringup = load('bringup_launch.py')

    def bringup_source(path):
        if str(path).endswith('/navigation_launch.py'):
            return LaunchDescriptionSource(native_description)
        return PythonLaunchDescriptionSource(path)

    bringup.PythonLaunchDescriptionSource = bringup_source
    bringup_description = bringup.generate_launch_description()
    sim = load('tb3_simulation_launch.py')

    def simulation_source(path):
        if str(path).endswith('/bringup_launch.py'):
            return LaunchDescriptionSource(bringup_description)
        return PythonLaunchDescriptionSource(path)

    sim.PythonLaunchDescriptionSource = simulation_source
    return sim.generate_launch_description()
