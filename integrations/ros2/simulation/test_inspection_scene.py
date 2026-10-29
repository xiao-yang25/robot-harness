"""Explicit fixture geometry and fail-closed configuration; no movement."""
import math
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace as N
import unittest
import xml.etree.ElementTree as ET

import yaml
import inspection_scene as scene
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'nav2_session'))
from robot_harness_nav2._inspection import validate_selection, validate_values
from robot_harness_nav2._profile import PROFILE_ID


class InspectionSceneTests(unittest.TestCase):
    def fixture(self, root):
        share, output = root/'share', root/'output'
        (share/'worlds').mkdir(parents=True); output.mkdir()
        (share/'worlds/world_only.model').write_text('<sdf><world name="default"><model name="walls"><static>true</static></model></world></sdf>')
        (output/'scoped-waffle.model').write_text('<sdf><model name="robot"><link name="base_link"><sensor type="depth"/><sensor type="ray"/></link><plugin filename="libscoped_diff_drive.so"/></model></sdf>')
        parameters = {'controller_server': {'ros__parameters': {
            'goal_checker_plugins': ['general_goal_checker'],
            'general_goal_checker': {'plugin': 'nav2_controller::SimpleGoalChecker',
                                     'yaw_goal_tolerance': .25, 'xy_goal_tolerance': .25, 'stateful': True},
            'FollowPath': {'xy_goal_tolerance': .25, 'plugin': 'nav2_rotation_shim_controller::RotationShimController'}}}}
        for name in ('native-params.yaml','b-params.yaml'):
            (output/name).write_text(yaml.safe_dump(parameters))
        return share, output

    def test_normal_and_recheck_pixels_camera_and_native_parameters_are_explicit(self):
        for case in ('normal', 'recheck'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                share, output = self.fixture(Path(directory))
                original = (share/'worlds/world_only.model').read_text()
                scene.prepare(case, output, share)
                world = ET.parse(output/'inspection.world')
                self.assertEqual([m.get('name') for m in world.findall('.//model')], ['walls','indicator_A','indicator_B'])
                self.assertFalse(world.findall('.//collision'))
                for panel in world.findall('.//model')[1:]:
                    expected = '0.85 0.05 0.05 1' if case == 'recheck' and panel.get('name') == 'indicator_A' else '0.05 0.85 0.05 1'
                    self.assertEqual(panel.findtext(".//visual[@name='state']/material/emissive"), expected)
                model = ET.parse(output/'scoped-waffle.model')
                sensor = model.find(".//sensor[@name='inspection_rgb']")
                self.assertEqual(sensor.findtext('camera/horizontal_fov'), '2.2')
                self.assertEqual(sensor.findtext('update_rate'), '5')
                self.assertEqual(sensor.findtext('camera/image/width'), '640')
                self.assertEqual(sensor.findtext('camera/image/height'), '480')
                self.assertEqual(sensor.findtext('plugin/frame_name'), 'inspection_optical')
                self.assertFalse(model.findall(".//sensor[@type='depth']"))
                self.assertTrue(model.findall(".//sensor[@type='ray']"))
                self.assertIsNotNone(model.find(".//plugin[@filename='libscoped_diff_drive.so']"))
                for name in ('native-params.yaml','b-params.yaml'):
                    value = yaml.safe_load((output/name).read_text())['controller_server']['ros__parameters']
                    self.assertEqual(value['general_goal_checker'], dict(xy_goal_tolerance=.15, stateful=True, yaw_goal_tolerance=math.radians(3),
                        plugin='nav2_controller::SimpleGoalChecker'))
                    self.assertEqual(value['FollowPath']['xy_goal_tolerance'], .15)
                self.assertEqual((share/'worlds/world_only.model').read_text(), original)
                selected, _ = scene.collection_world(output, share, case)
                self.assertEqual(selected, output/'inspection.world')

    def test_missing_or_modified_native_baseline_does_not_rewrite_robot_model(self):
        with tempfile.TemporaryDirectory() as directory:
            share, output = self.fixture(Path(directory))
            old = (output/'scoped-waffle.model').read_text()
            (output/'b-params.yaml').write_text('controller_server: {}')
            with self.assertRaises(KeyError): scene.prepare('normal',output,share)
            self.assertEqual((output/'scoped-waffle.model').read_text(), old)

    def test_wrong_b_heading_baseline_leaves_both_files_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            share, output = self.fixture(Path(directory))
            path = output/'b-params.yaml'
            data = yaml.safe_load(path.read_text())
            data['controller_server']['ros__parameters']['general_goal_checker']['yaw_goal_tolerance'] = .1
            path.write_text(yaml.safe_dump(data))
            before = {name: (output/name).read_text() for name in
                      ('native-params.yaml', 'b-params.yaml', 'scoped-waffle.model')}
            with self.assertRaises(ValueError):
                scene.prepare('normal', output, share)
            self.assertEqual(before, {name: (output/name).read_text() for name in before})
            self.assertFalse((output/'inspection.world').exists())

    def test_collection_rejects_collision_or_changed_base_world(self):
        for change in ('collision','include','model','base','wrong-case'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                share, output = self.fixture(Path(directory)); scene.prepare('normal',output,share)
                world = ET.parse(output/'inspection.world')
                if change in ('collision','include','model'): ET.SubElement(world.find(".//model[@name='indicator_A']/link"),change)
                elif change == 'base': world.find(".//model[@name='walls']").set('name','other')
                world.write(output/'inspection.world')
                with self.assertRaises(ValueError):scene.collection_world(output,share,'recheck' if change == 'wrong-case' else 'normal')

    def test_selection_and_actual_typed_values_reject_unsupported_inputs(self):
        validate_selection(None, PROFILE_ID, 'normal')
        validate_selection('normal', PROFILE_ID, 'normal')
        for args in (('normal', 'other', 'normal'),('recheck',PROFILE_ID,'occupied-a'),('unknown',PROFILE_ID,'normal')):
            with self.subTest(args=args), self.assertRaises(ValueError): validate_selection(*args)
        from test_inspection_readback import valid
        self.assertEqual(validate_values(valid())['general_goal_checker.yaw_goal_tolerance'], math.radians(3))
        for values in ([N(type=3,double_value=.15)]*2, []):
            with self.subTest(values=values),self.assertRaises(ValueError):validate_values(values)



if __name__ == '__main__': unittest.main()
