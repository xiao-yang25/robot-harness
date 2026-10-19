"""Optional visual-only inspection fixture for the existing isolated Session."""
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import yaml


def prepare(case, output, share):
    from robot_harness_nav2._inspection import CASES, TOLERANCE
    if case not in CASES:
        raise ValueError('unsupported inspection case')
    output, share = Path(output), Path(share)
    model = ET.parse(output/'scoped-waffle.model')
    for link in model.getroot().iter('link'):
        for sensor in list(link.findall('sensor')):
            if sensor.get('type') == 'depth':
                link.remove(sensor)
    base = next(link for link in model.getroot().iter('link') if link.get('name') == 'base_link')
    base.append(ET.fromstring('''<sensor name="inspection_rgb" type="camera">
      <pose>0.1 0 0.3 0 0 0</pose><always_on>true</always_on><update_rate>5</update_rate>
      <camera><horizontal_fov>2.2</horizontal_fov>
        <image><width>640</width><height>480</height><format>R8G8B8</format></image>
        <clip><near>0.02</near><far>10</far></clip></camera>
      <plugin name="inspection_camera" filename="libgazebo_ros_camera.so">
        <camera_name>inspection</camera_name><frame_name>inspection_optical</frame_name>
      </plugin></sensor>'''))
    world = ET.parse(share/'worlds/world_only.model')
    node = world.getroot().find('world')
    if node is None:
        raise ValueError('base world missing')
    for site, x in (('A', .7), ('B', -1.5)):
        color = '0.85 0.05 0.05 1' if case == 'recheck' and site == 'A' else '0.05 0.85 0.05 1'
        node.append(ET.fromstring(f'''<model name="indicator_{site}"><static>true</static>
          <pose>{x+1} -0.5 0.31 0 0 0</pose><link name="panel">
            <visual name="border"><geometry><box><size>0.02 0.9 0.5</size></box></geometry>
              <material><ambient>1 1 1 1</ambient><diffuse>1 1 1 1</diffuse>
                <emissive>1 1 1 1</emissive></material></visual>
            <visual name="state"><pose>-0.016 0 0 0 0 0</pose>
              <geometry><box><size>0.01 0.7 0.32</size></box></geometry>
              <material><ambient>{color}</ambient><diffuse>{color}</diffuse>
                <emissive>{color}</emissive></material></visual>
          </link></model>'''))
    parameters = []
    for name in ('native-params.yaml', 'b-params.yaml'):
        data = yaml.safe_load((output/name).read_text())
        controller = data['controller_server']['ros__parameters']
        for plugin in ('general_goal_checker', 'FollowPath'):
            if controller[plugin]['xy_goal_tolerance'] != .25:
                raise ValueError('unexpected baseline controller tolerance')
            controller[plugin]['xy_goal_tolerance'] = TOLERANCE
        parameters.append((name, data))
    model.write(output/'scoped-waffle.model', encoding='unicode')
    world.write(output/'inspection.world', encoding='unicode')
    for name, data in parameters:
        (output/name).write_text(yaml.safe_dump(data))
    (output/'inspection-scene.json').write_text(json.dumps(dict(case=case,
        world=str(output/'inspection.world'), camera_profile='located-bright-panel-v2',
        camera_hfov=2.2, camera_hz=5, goal_tolerance=TOLERANCE,
        panels_visual_only=True), indent=2)+'\n')


def camera_transform(node):
    """Use the existing bridge node/executor; the node owns the TF publisher."""
    from geometry_msgs.msg import TransformStamped
    from tf2_ros import StaticTransformBroadcaster
    message = TransformStamped()
    message.header.stamp = node.get_clock().now().to_msg()
    message.header.frame_id, message.child_frame_id = 'base_link', 'inspection_optical'
    t, q = message.transform.translation, message.transform.rotation
    t.x, t.y, t.z = .1, 0., .3
    # Optical x/right,y/down,z/forward -> base -y,-z,+x.
    q.x, q.y, q.z, q.w = -.5, .5, -.5, .5
    broadcaster = StaticTransformBroadcaster(node)
    broadcaster.sendTransform(message)
    return broadcaster


def collection_world(output, share, case):
    """Declare alignment only when added panels leave the installed world intact."""
    output, share = Path(output), Path(share)
    fixture = json.loads((output/'inspection-scene.json').read_text())
    world_file = output/'inspection.world'
    if (fixture.get('case') != case or case not in ('normal', 'recheck')
            or fixture.get('world') != str(world_file)
            or fixture.get('camera_profile') != 'located-bright-panel-v2'
            or fixture.get('goal_tolerance') != .15):
        raise ValueError('inspection collection configuration differs')
    root = ET.parse(world_file).getroot()
    world = root.find('world')
    if world is None:
        raise ValueError('inspection world missing')
    panels = [m for m in world.findall('model') if m.get('name', '').startswith('indicator_')]
    if {m.get('name') for m in panels} != {'indicator_A', 'indicator_B'} or len(panels) != 2:
        raise ValueError('inspection requires two registered visual panels')
    for panel in panels:
        if any(list(panel.iter(tag)) for tag in ('collision', 'plugin', 'sensor', 'joint', 'include')):
            raise ValueError('inspection panel has nonvisual effects')
        if len(list(panel.iter('model'))) != 1:
            raise ValueError('inspection panel has nested models')
        if panel.findtext('static') != 'true':
            raise ValueError('inspection panel must be static')
        world.remove(panel)
    base = ET.parse(share/'worlds/world_only.model').getroot()
    if ET.tostring(root) != ET.tostring(base):
        raise ValueError('inspection changed installed world geometry')
    return world_file, fixture


if __name__ == '__main__':
    import sys
    prepare(sys.argv[1], '/output', '/opt/ros/humble/share/nav2_bringup')
