"""Prepare one explicit static test map; never change the physical world."""
import argparse
import json
import math
from pathlib import Path
import re
import yaml


def prepare_occupied_a(source, output):
    config = yaml.safe_load(source.read_text())
    raw = (source.parent/config['image']).read_bytes()
    header = re.match(rb'P5\s+(?:#[^\n]*\n\s*)?(\d+)\s+(\d+)\s+255\n', raw)
    if header is None:
        raise ValueError('unsupported installed map format')
    width, height = map(int, header.groups())
    pixels = bytearray(raw[header.end():])
    if len(pixels) != width*height or config['negate'] != 0 or config['origin'][2] != 0:
        raise ValueError('unsupported installed map geometry')
    resolution = config['resolution']
    ox, oy, _ = config['origin']
    if not width or not height or not all(math.isfinite(v) for v in (resolution, ox, oy)) or resolution <= 0:
        raise ValueError('unsupported installed map geometry')
    changed = 0
    for row in range(height):
        for col in range(width):
            x, y = ox+(col+.5)*resolution, oy+(height-row-.5)*resolution
            if abs(x-.7) <= .4 and abs(y+.5) <= .4:
                pixels[row*width+col] = 0
                changed += 1
    def cell(x, y):
        col, row = math.floor((x-ox)/resolution), math.floor((y-oy)/resolution)
        if not (0 <= col < width and 0 <= row < height):
            raise ValueError('registered scene site outside installed map')
        return pixels[(height-1-row)*width+col]
    if not changed or cell(.7, -.5) != 0 or cell(-1.5, -.5) != 254 or cell(-2, -.5) != 254:
        raise ValueError('controlled A occupancy or free B/start differs')
    (output/'occupied-a.pgm').write_bytes(raw[:header.end()]+pixels)
    config['image'] = 'occupied-a.pgm'
    (output/'occupied-a.yaml').write_text(yaml.safe_dump(config))
    (output/'map-fixture.json').write_text(json.dumps(dict(
        case='static-map-occupied-A', goal=[.7,-.5], half_extent=.4,
        width=width, height=height, resolution=resolution, origin=config['origin'],
        a_pixel=cell(.7,-.5), b_pixel=cell(-1.5,-.5), start_pixel=cell(-2,-.5),
        changed_cells=changed, physical_obstacle=False),indent=2)+'\n')


def main():
    from robot_harness_nav2._profile import PROFILE_ID, SCENE_MAP_IDS, scene_map_id
    import os
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scene', choices=SCENE_MAP_IDS)
    args = parser.parse_args()
    map_id = scene_map_id(args.scene, os.environ.get('M6_NAVIGATION_PROFILE', PROFILE_ID))
    source = Path('/opt/ros/humble/share/nav2_bringup/maps/turtlebot3_world.yaml')
    output = Path('/output')
    if args.scene != 'normal':
        prepare_occupied_a(source, output)
        source = output/'occupied-a.yaml'
    (output/'navigation-scene.json').write_text(json.dumps(dict(
        scene=args.scene, map_id=map_id, map_file=str(source)), indent=2)+'\n')


if __name__ == '__main__':
    main()
