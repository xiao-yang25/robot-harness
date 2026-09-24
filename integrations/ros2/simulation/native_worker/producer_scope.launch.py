"""Fix the real controller/smoother command channels before either is started."""
import importlib.util
import json
from pathlib import Path


def generate_launch_description():
    path = Path(__file__).with_name('worker_probe.launch.py')
    spec = importlib.util.spec_from_file_location('producer_worker_launch', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    scope = json.loads(Path('/output/producer-context.json').read_text())['a_scope']
    return module.generate_launch_description({
        'nav2_controller': ('controller_server', 'native_worker_controller'),
        'nav2_planner': ('planner_server', 'native_worker_planner'),
        'nav2_bt_navigator': ('bt_navigator', 'native_scope_navigator'),
    }, command_scope=scope)
