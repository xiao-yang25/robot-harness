"""Fixed scene preparation/actual-map guards; no ROS or physical qualification."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace as Box
import unittest
import yaml

import navigation_scene
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'nav2_session'))
from robot_harness_nav2._profile import scene_map_id, FAILURE_PROFILE_ID, PROFILE_ID
from robot_harness_nav2._scene_map import occupied_a_sample


def message():
    info=Box(width=60,height=40,resolution=.1,origin=Box(
        position=Box(x=-3.,y=-2.,z=0.),orientation=Box(x=0.,y=0.,z=0.,w=1.)))
    data=[0]*2400
    data[15*60+37]=100
    return Box(header=Box(frame_id='map'),info=info,data=data)


class SceneTests(unittest.TestCase):
    def test_explicit_scene_requires_failure_profile_and_preserves_default(self):
        self.assertEqual(scene_map_id('normal',PROFILE_ID),'turtlebot3-world-v1')
        self.assertEqual(scene_map_id('occupied-a',FAILURE_PROFILE_ID),'turtlebot3-occupied-a-probe-v1')
        with self.assertRaises(ValueError):scene_map_id('occupied-a',PROFILE_ID)

    def test_prepared_map_changes_a_without_changing_source_b_or_start(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source.yaml';output=root/'out';output.mkdir()
            raw=b'P5\n60 40\n255\n'+bytes([254])*2400
            (root/'source.pgm').write_bytes(raw)
            source.write_text(yaml.safe_dump(dict(image='source.pgm',negate=0,resolution=.1,
                origin=[-3.,-2.,0.],occupied_thresh=.65,free_thresh=.196)))
            navigation_scene.prepare_occupied_a(source,output)
            self.assertEqual((root/'source.pgm').read_bytes(),raw)
            config=yaml.safe_load((output/'occupied-a.yaml').read_text())
            self.assertEqual(config['image'],'occupied-a.pgm')
            pixels=(output/config['image']).read_bytes().split(b'255\n',1)[1]
            self.assertEqual(pixels[(40-1-15)*60+37],0)
            self.assertEqual(pixels[(40-1-15)*60+15],254)
            self.assertEqual(pixels[(40-1-15)*60+10],254)
            self.assertFalse(json.loads((output/'map-fixture.json').read_text())['physical_obstacle'])

    def test_actual_map_requires_occupied_a_and_free_backup_and_start(self):
        sample=occupied_a_sample(message())
        self.assertEqual((sample['a_cell'],sample['b_cell'],sample['start_cell']),(100,0,0))
        for index,value in ((15*60+37,0),(15*60+15,100),(15*60+10,-1)):
            row=message();row.data[index]=value
            with self.subTest(index=index),self.assertRaises(ValueError):occupied_a_sample(row)

    def test_invalid_actual_geometry_and_outside_site_fail_closed(self):
        for change in ('resolution','orientation','data','outside'):
            row=message()
            if change=='resolution':row.info.resolution=0
            elif change=='orientation':row.info.origin.orientation.z=.1
            elif change=='data':row.data.pop()
            else:row.info.origin.position.x=10.
            with self.subTest(change=change),self.assertRaises(ValueError):occupied_a_sample(row)


if __name__=='__main__':unittest.main()
