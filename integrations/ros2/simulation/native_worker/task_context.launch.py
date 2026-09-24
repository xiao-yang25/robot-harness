"""Fresh B navigation endpoints; simulator/localization and closed A stay alive."""
import importlib.util
import json
import os
from pathlib import Path
from launch.actions import GroupAction, SetLaunchConfiguration


def generate_launch_description():
    path = Path(__file__).with_name('worker_probe.launch.py')
    spec = importlib.util.spec_from_file_location('task_context_worker', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original_node = module.Node

    def context_node(**kwargs):
        if (os.environ.get('M4_CONTEXT_CASE') in ('missing-controller', 'replace-moving-missing-controller')
                and kwargs.get('executable') == '/native/native_worker_controller'):
            return GroupAction([])
        return original_node(**kwargs)

    module.Node = context_node
    scope = json.loads(Path('/output/producer-context.json').read_text())['b_scope']
    namespace = '/m4_task/s_' + scope
    result = module.generate_launch_description({
        'nav2_controller': ('controller_server', 'native_worker_controller'),
        'nav2_planner': ('planner_server', 'native_worker_planner'),
        'nav2_bt_navigator': ('bt_navigator', 'native_scope_navigator'),
    }, command_scope=scope, navigation_only=True, context_namespace=namespace)
    result.entities.insert(0, SetLaunchConfiguration('namespace', namespace))
    return result
