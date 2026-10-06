"""Opt-in passive Gazebo records, never inputs or execution permissions."""
import argparse
import json
import math
import os
from pathlib import Path
import signal
import socket
import subprocess
import time


def write_json(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def prepare_context(output, share):
    """Consume actual prepared configuration of the one supported asset pair."""
    import re
    import xml.etree.ElementTree as ET
    import yaml
    scene = json.loads((output/'navigation-scene.json').read_text())
    profile = json.loads((output/'navigation-profile.json').read_text())
    params = yaml.safe_load((output/'native-params.yaml').read_text())
    source = share/'maps/turtlebot3_world.yaml'
    config = yaml.safe_load(source.read_text())
    header = re.match(rb'P5\s+(?:#[^\n]*\n\s*)?(\d+)\s+(\d+)\s+255\s',
                      (source.parent/config['image']).read_bytes())
    initial = params['amcl']['ros__parameters']
    spawn = {'x': -2., 'y': -.5, 'z': .01, 'R': 0., 'P': 0., 'Y': 0.}
    if (ET.parse(share/'package.xml').findtext('version') != '1.1.20'
            or scene != {'scene': 'normal', 'map_id': 'turtlebot3-world-v1', 'map_file': str(source)}
            or profile['profile'] != 'scoped-two-context-nav2-shim-v1'
            or config['resolution'] != .05 or config['origin'] != [-10., -10., 0.]
            or config['negate'] != 0 or config['image'] != 'turtlebot3_world.pgm'
            or header is None or tuple(map(int, header.groups())) != (384, 384)
            or initial['set_initial_pose'] is not True or initial['initial_pose'] != dict(x=-2., y=-.5, z=0., yaw=0.)):
        raise ValueError('unsupported actual scene/map/initial configuration')
    world = share/'worlds/world_only.model'
    if ET.parse(world).find('world') is None:
        raise ValueError('missing installed world')
    return dict(schema='nav2-passive-truth-v1', run_id=os.environ['M4_FRESH_NAV2_RUN'],
                container_name=socket.gethostname(), model='turtlebot3_waffle',
                clock='container-clock-monotonic', scene='normal', map_id=scene['map_id'],
                profile=profile['profile'], alignment=dict(
                    basis='Nav2 1.1.20 installed world/map pair with explicit matching initial poses',
                    world=str(world), map_file=str(source), resolution=config['resolution'],
                    origin=config['origin'], map_to_world_xy_yaw=[0., 0., 0.],
                    spawn=spawn, initial_pose=initial['initial_pose']))


class Collector:
    def __init__(self, output, context):
        self.output, self.context = output, context
        self.stopped = False
        self.query = None
        self.seen = set()
        self.samples = []
        self.errors = []
        self.children = []

    def stop(self, _signum, _frame):
        self.stopped = True

    def pose(self):
        """One bounded owned query; never retry after timeout or interruption."""
        self.query = subprocess.Popen(['gz', 'model', '-m', self.context['model'], '-p'],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        child = self.query
        until = time.monotonic()+5
        try:
            while True:
                if self.stopped or time.monotonic() >= until:
                    raise TimeoutError('pose query stopped or timed out')
                try:
                    stdout, stderr = child.communicate(timeout=min(.05, max(.001, until-time.monotonic())))
                    break
                except subprocess.TimeoutExpired:
                    continue
            if child.returncode != 0:
                raise RuntimeError(f'pose query exited {child.returncode}: {stderr[:200]}')
            pose = [float(value) for value in stdout.split()]
            if len(pose) != 6 or not all(math.isfinite(value) for value in pose):
                raise ValueError('pose query did not return six finite coordinates')
            return pose
        finally:
            if child.poll() is None:
                child.kill()
            try:
                child.communicate(timeout=.5)
            finally:
                reaped = child.returncode is not None
                self.children.append(dict(pid=child.pid, returncode=child.returncode, reaped=reaped))
                self.query = None
                if not reaped:
                    self.errors.append('pose query not reaped before cleanup deadline')

    def collect(self, row):
        event = row.get('event')
        stage = 'start' if event == 'ready' else row.get('stage')
        if event != 'ready' and (event != 'arrival' or stage not in ('A', 'B')):
            return
        if stage in self.seen:
            self.errors.append('duplicate trigger '+stage)
            return
        self.seen.add(stage)
        started = time.monotonic()
        try:
            pose = self.pose()
            finished = time.monotonic()
            if not row['steady'] <= started <= finished <= row['steady']+5.5:
                raise ValueError('late or inconsistent trigger/query interval')
            sample = dict(run_id=self.context['run_id'], container_name=self.context['container_name'],
                          model=self.context['model'], stage=stage, event=event,
                          trigger=row['steady'], started=started, finished=finished, xyz_rpy=pose)
            if stage == 'start':
                sample['map_pose'] = row['observation']['pose']
                if (not isinstance(sample['map_pose'], list) or len(sample['map_pose']) != 2
                        or not all(isinstance(value, (float, int)) and not isinstance(value, bool)
                                   and math.isfinite(value) for value in sample['map_pose'])):
                    raise ValueError('startup map pose not finite XY')
            else:
                sample['goal_id'] = row['goal_id']
            with (self.output/'physical.jsonl').open('a') as stream:
                stream.write(json.dumps(sample, allow_nan=False)+'\n')
            self.samples.append(stage)
        except (OSError, subprocess.SubprocessError, ValueError, TypeError, KeyError, RuntimeError) as error:
            self.errors.append(f'{stage}: {type(error).__name__}: {error}')

    def run(self):
        until = time.monotonic()+290  # Existing Owner lifetime; no task extension.
        path = self.output/'caller.jsonl'
        offset = 0
        pending = ''
        while not self.stopped and time.monotonic() < until:
            if path.exists():
                with path.open() as stream:
                    stream.seek(offset)
                    pending += stream.read()
                    offset = stream.tell()
                while '\n' in pending:
                    line, pending = pending.split('\n', 1)
                    self.collect(json.loads(line))
                    if self.stopped:
                        break
            time.sleep(.02)
        if pending:
            self.errors.append('incomplete event line')
        if time.monotonic() >= until:
            self.errors.append('collection deadline reached')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/output'))
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    if args.prepare:
        context = prepare_context(args.output, Path('/opt/ros/humble/share/nav2_bringup'))
        write_json(args.output/'collection-context.json', context)
        return 0
    collector = None
    outcome = dict(schema='nav2-passive-truth-v1', status='unknown', samples=[], errors=[], children=[])
    handlers = {}
    try:
        context = json.loads((args.output/'collection-context.json').read_text())
        collector = Collector(args.output, context)
        handlers = {sig: signal.signal(sig, collector.stop) for sig in (signal.SIGINT, signal.SIGTERM)}
        collector.run()
    except (OSError, ValueError, TypeError, KeyError) as error:
        outcome['errors'].append(f'{type(error).__name__}: {error}')
    finally:
        if collector is not None:
            outcome.update(run_id=collector.context['run_id'], container_name=collector.context['container_name'],
                           samples=collector.samples, children=collector.children)
            outcome['errors'].extend(collector.errors)
            if collector.samples == ['start', 'A', 'B'] and not outcome['errors']:
                outcome['status'] = 'completed'
        for sig, handler in handlers.items():
            signal.signal(sig, handler)
        write_json(args.output/'collection.json', outcome)
    # Collection gaps must never turn a valid native task into a cancellation.
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
