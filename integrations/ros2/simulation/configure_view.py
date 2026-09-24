"""Configure the optional RViz display; navigation parameters stay unchanged."""
import copy
import json
from pathlib import Path
import yaml
with open('/opt/ros/humble/share/nav2_bringup/rviz/nav2_default_view.rviz') as source:
    view = yaml.safe_load(source)
view['Window Geometry'].update(Width=1600, Height=900, X=0, Y=0)
view['Window Geometry'].update({'Hide Left Dock': True, 'Hide Right Dock': True})
view['Window Geometry'].pop('QMainWindow State', None)
view['Panels'] = []
manager = view['Visualization Manager']
manager['Global Options']['Frame Rate'] = 10
manager['Views']['Current'].update(Scale=135, Angle=0, X=0, Y=0)
manager['Global Options']['Background Color'] = '32; 39; 34'
for display in manager['Displays']:
    if display['Name'] == 'RobotModel':
        display.update(Enabled=True, Value=True)
        display['Description Topic']['Durability Policy'] = 'Transient Local'
    elif display['Name'] in ('TF', 'Amcl Particle Swarm', 'LaserScan', 'Bumper Hit'):
        display.update(Enabled=False, Value=False)
    # Costmaps remain active in Nav2; only their opaque visual overlays are off.
    for child in display.get('Displays', []):
        if child['Class'] != 'rviz_default_plugins/Path':
            child.update(Enabled=False, Value=False)
        else:
            child.update(Color='20; 140; 110', **{'Line Style': 'Billboards',
                         'Line Width': 0.035, 'Pose Style': 'None'})
# B uses distinct task-scoped navigation topics; do not show only A's old path.
b_scope = json.loads(Path('/output/producer-context.json').read_text())['b_scope']
for display in manager['Displays']:
    for child in list(display.get('Displays', [])):
        if child['Class'] == 'rviz_default_plugins/Path':
            candidate = copy.deepcopy(child)
            candidate['Name'] = 'B ' + child['Name']
            candidate['Color'] = '45; 105; 190'
            candidate['Topic']['Value'] = '/m4_task/s_' + b_scope + '/' + child['Topic']['Value'].lstrip('/')
            display['Displays'].append(candidate)
with open('/output/live-view.rviz', 'w') as output:
    yaml.safe_dump(view, output, sort_keys=False)
