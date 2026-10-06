"""Passive collection and real subprocess cleanup; no movement qualification."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import navigation_truth as truth


class TruthTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        self.context = dict(run_id='current-run', container_name='current-container', model='turtlebot3_waffle')
        self.collector = truth.Collector(self.output, self.context)

    def fake_query(self, body):
        command = self.output/'gz'
        command.write_text('#!'+sys.executable+'\n'+body+'\n')
        command.chmod(0o755)
        return patch.dict(os.environ, PATH=str(self.output)+os.pathsep+os.environ['PATH'])

    def test_real_query_finite_and_reaped(self):
        with self.fake_query('print(".7 -.5 .01 0 0 0")'):
            self.assertEqual(self.collector.pose(), [.7,-.5,.01,0,0,0])
        child = self.collector.children[0]
        self.assertTrue(child['reaped'])
        self.assertEqual(child['returncode'], 0)
        with self.assertRaises(ProcessLookupError):
            os.kill(child['pid'], 0)

    def test_failed_and_nonfinite_queries_preserve_gaps(self):
        for body in ('raise SystemExit(7)', 'print("nan 0 0 0 0 0")', 'print("1 2")'):
            with self.subTest(body=body), self.fake_query(body):
                self.collector.collect(dict(event='arrival',stage='A',goal_id='a',steady=time.monotonic()))
            self.assertFalse((self.output/'physical.jsonl').exists())
            self.assertTrue(self.collector.children[-1]['reaped'])
            self.assertTrue(self.collector.errors)
            self.collector.seen.clear()

    def test_one_query_timeout_is_bounded_and_reaped(self):
        started = time.monotonic()
        with self.fake_query('import time; time.sleep(30)'), self.assertRaises(TimeoutError):
            self.collector.pose()
        self.assertLess(time.monotonic()-started, 6)
        self.assertTrue(self.collector.children[0]['reaped'])
        with self.assertRaises(ProcessLookupError):
            os.kill(self.collector.children[0]['pid'], 0)

    def test_unique_current_samples_and_late_trigger(self):
        with self.fake_query('print(".7 -.5 .01 0 0 0")'):
            self.collector.collect(dict(event='ready',steady=time.monotonic(),observation=dict(pose=[-2.,-.5])))
            for stage in ('A','B'):
                self.collector.collect(dict(event='arrival',stage=stage,goal_id=stage.lower(),steady=time.monotonic()))
            self.collector.collect(dict(event='arrival',stage='A',goal_id='other',steady=time.monotonic()))
        rows = [json.loads(line) for line in (self.output/'physical.jsonl').read_text().splitlines()]
        self.assertEqual([row['stage'] for row in rows], ['start','A','B'])
        self.assertTrue(all(row['run_id']=='current-run' and row['container_name']=='current-container' for row in rows))
        self.assertIn('duplicate trigger A', self.collector.errors)
        self.collector.seen.clear()
        with self.fake_query('print(".7 -.5 .01 0 0 0")'):
            self.collector.collect(dict(event='arrival',stage='A',goal_id='a',steady=time.monotonic()-6))
        self.assertIn('late or inconsistent', self.collector.errors[-1])
        self.assertEqual(len((self.output/'physical.jsonl').read_text().splitlines()), 3)

    def test_missing_context_is_unknown_without_task_failure(self):
        result = subprocess.run([sys.executable,truth.__file__,'--output',str(self.output)],
                                capture_output=True,text=True,timeout=2)
        self.assertEqual(result.returncode, 0)
        outcome = json.loads((self.output/'collection.json').read_text())
        self.assertEqual(outcome['status'], 'unknown')
        self.assertTrue(outcome['errors'])

    def test_signal_during_live_query_reaps_child_and_writes_gap(self):
        (self.output/'collection-context.json').write_text(json.dumps(self.context))
        (self.output/'caller.jsonl').write_text(json.dumps(dict(
            event='arrival',stage='A',goal_id='a',steady=time.monotonic()))+'\n')
        pid_file = self.output/'query.pid'
        with self.fake_query('import os,time\nfrom pathlib import Path\n'
                             f'Path({str(pid_file)!r}).write_text(str(os.getpid()))\ntime.sleep(30)'):
            child = subprocess.Popen([sys.executable,truth.__file__,'--output',str(self.output)],
                                     stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                deadline = time.monotonic()+3
                while not pid_file.exists() and time.monotonic()<deadline:
                    time.sleep(.01)
                self.assertTrue(pid_file.exists())
                child.send_signal(signal.SIGINT)
                child.communicate(timeout=2)
                self.assertEqual(child.returncode, 0)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate()
        outcome = json.loads((self.output/'collection.json').read_text())
        self.assertEqual(outcome['status'], 'unknown')
        self.assertTrue(outcome['children'][0]['reaped'])
        with self.assertRaises(ProcessLookupError):
            os.kill(int(pid_file.read_text()), 0)

    def test_context_consumes_supported_prepared_configuration(self):
        import yaml
        share = self.output/'share'
        (share/'maps').mkdir(parents=True)
        (share/'worlds').mkdir()
        (share/'package.xml').write_text('<package><version>1.1.20</version></package>')
        (share/'worlds/world_only.model').write_text('<sdf><world name="default"/></sdf>')
        source = share/'maps/turtlebot3_world.yaml'
        config = dict(image='turtlebot3_world.pgm',resolution=.05,origin=[-10.,-10.,0.],negate=0)
        source.write_text(yaml.safe_dump(config))
        (share/'maps/turtlebot3_world.pgm').write_bytes(b'P5\n# fixture\n384 384\n255\n')
        (self.output/'navigation-scene.json').write_text(json.dumps(dict(
            scene='normal',map_id='turtlebot3-world-v1',map_file=str(source))))
        (self.output/'navigation-profile.json').write_text(json.dumps(dict(profile='scoped-two-context-nav2-shim-v1')))
        (self.output/'native-params.yaml').write_text(yaml.safe_dump(dict(amcl=dict(ros__parameters=dict(
            set_initial_pose=True,initial_pose=dict(x=-2.,y=-.5,z=0.,yaw=0.))))))
        with patch.dict(os.environ,M4_FRESH_NAV2_RUN='current-run'):
            context = truth.prepare_context(self.output,share)
            self.assertEqual(context['alignment']['map_to_world_xy_yaw'], [0,0,0])
            self.assertEqual(context['run_id'], 'current-run')
            config['origin']=[0.,0.,0.]
            source.write_text(yaml.safe_dump(config))
            with self.assertRaisesRegex(ValueError,'unsupported actual'):
                truth.prepare_context(self.output,share)

    @unittest.skipUnless(sys.platform=='linux', 'Session process groups use Linux setsid')
    def test_partial_startup_before_group_formation_keeps_cleanup_budget(self):
        source = Path(truth.__file__).with_name('navigation_session.sh').read_text()
        cleanup = source[source.index('cleanup() {'):source.index('trap cleanup EXIT')]
        cleanup = cleanup.replace("Path('/output/collector-process.json')", f'Path({str(self.output/"collector-process.json")!r})')
        # Deterministically expose the window before setsid establishes its group.
        # Background children inherit ignored SIGINT before Python installs handlers.
        child = self.output/'delayed-session.py'
        child.write_text('import os,time\ntime.sleep(.3)\nos.setsid()\ntime.sleep(5)\n')
        script = (cleanup+'\nowner_pid="" client_pid="" launch_pid="" bridge_pid=""\n'
                  'trap cleanup EXIT\n'+f'{sys.executable} {child} &\ncollector_pid=$!\nexit 17\n')
        started = time.monotonic()
        result = subprocess.run(['bash','-c',script],capture_output=True,text=True,timeout=7)
        self.assertEqual(result.returncode,17,result.stderr)
        self.assertLess(time.monotonic()-started,4.5)
        record = json.loads((self.output/'collector-process.json').read_text())
        self.assertTrue(record['reaped'])
        self.assertTrue(record['group_forced'])
        with self.assertRaises(ProcessLookupError):
            os.kill(record['pid'],0)

    @unittest.skipUnless(sys.platform=='linux', 'Session process groups use Linux setsid')
    def test_real_session_cleanup_reaps_collector_before_scene(self):
        # Exercise the actual supervisor cleanup function with real processes.
        # Only its fixed artifact path is redirected to this isolated fixture.
        source = Path(truth.__file__).with_name('navigation_session.sh').read_text()
        cleanup = source[source.index('cleanup() {'):source.index('trap cleanup EXIT')]
        cleanup = cleanup.replace("Path('/output/collector-process.json')", f'Path({str(self.output/"collector-process.json")!r})')
        sentinel = self.output/'scene-stopped'
        query_pid = self.output/'query.pid'
        (self.output/'collection-context.json').write_text(json.dumps(self.context))
        (self.output/'caller.jsonl').write_text(json.dumps(dict(
            event='arrival',stage='A',goal_id='a',steady=time.monotonic()))+'\n')
        scene = self.output/'scene.py'
        scene.write_text('import signal,time\nfrom pathlib import Path\n'
                         f'def stop(*_):\n Path({str(sentinel)!r}).write_text("after-collection" if '
                         f'Path({str(self.output/"collection.json")!r}).exists() else "early"); raise SystemExit(0)\n'
                         'signal.signal(signal.SIGINT,stop)\nwhile True: time.sleep(.01)\n')
        with self.fake_query('import os,time\nfrom pathlib import Path\n'
                             f'Path({str(query_pid)!r}).write_text(str(os.getpid()))\ntime.sleep(30)'):
            script = (cleanup+'\nowner_pid="" client_pid="" bridge_pid=""\n'
                      'trap cleanup EXIT\n'
                      f'{sys.executable} {scene} &\nlaunch_pid=$!\n'
                      f'setsid {sys.executable} {truth.__file__} --output {self.output} &\ncollector_pid=$!\n'
                      f'for i in $(seq 1 300); do [[ -f {query_pid} ]] && break; sleep .01; done\n'
                      f'[[ -f {query_pid} ]] || exit 19\nexit 17\n')
            result = subprocess.run(['bash','-c',script],capture_output=True,text=True,timeout=7)
        self.assertEqual(result.returncode,17,result.stderr)
        record = json.loads((self.output/'collector-process.json').read_text())
        self.assertTrue(record['reaped'])
        self.assertFalse(record['group_forced'])
        self.assertEqual(sentinel.read_text(),'after-collection')
        outcome = json.loads((self.output/'collection.json').read_text())
        self.assertEqual(outcome['status'],'unknown')
        self.assertTrue(outcome['children'][0]['reaped'])
        for pid in (record['pid'],int(query_pid.read_text())):
            with self.assertRaises(ProcessLookupError):
                os.kill(pid,0)


if __name__=='__main__':
    unittest.main()
