"""Real Linux group lifecycles, including a leader that exits before its child."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

import navigation_groups as groups


@unittest.skipUnless(sys.platform == 'linux', 'isolated Session uses Linux sessions and /proc')
class GroupChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.children = []
        self.inner_groups = set()
        self.addCleanup(self.dispose)

    def dispose(self):
        for name in ('owner.ready', 'scene.ready'):
            path = self.root/name
            if path.exists() and path.read_text().strip():
                self.inner_groups.add(int(path.read_text()))
        for pid in self.inner_groups:
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for child, grouped in self.children:
            try:
                (os.killpg if grouped else os.kill)(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.wait(timeout=3)

    def ready(self, path, *, content=True):
        until = time.monotonic() + 3
        def complete():
            return path.exists() and (not content or path.read_text().strip().isdecimal())
        while not complete() and time.monotonic() < until:
            time.sleep(.01)
        self.assertTrue(complete(), str(path))

    def tree(self, name, *, exited=False, ignore=False):
        ready = self.root / (name + '.child')
        child_code = ('import os,signal,time\nfrom pathlib import Path\n'
            + ('signal.signal(signal.SIGINT,signal.SIG_IGN)\n' if ignore else
               'signal.signal(signal.SIGINT,lambda *_:exit(0))\n')
            + f'Path({str(ready)!r}).write_text(str(os.getpid()))\ntime.sleep(30)\n')
        code = ('import signal,subprocess,sys,time\nfrom pathlib import Path\n'
            'signal.signal(signal.SIGINT,' + ('signal.SIG_IGN' if ignore else 'lambda *_:exit(0)') + ')\n'
            f'subprocess.Popen([sys.executable,"-c",{child_code!r}])\n'
            f'while not Path({str(ready)!r}).exists():time.sleep(.01)\n'
            + ('sys.exit(0)\n' if exited else 'time.sleep(30)\n'))
        process = subprocess.Popen([sys.executable, '-c', code], start_new_session=True,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append((process, True))
        self.ready(ready)
        if exited:
            self.assertEqual(process.wait(timeout=3), 0)
        return process

    def test_exited_scene_and_owner_leaders_do_not_hide_their_children(self):
        scene = self.tree('scene', exited=True)
        owner = self.tree('owner', exited=True)
        unrelated = self.tree('unrelated')
        result = groups.stop_groups(dict(scene=scene.pid, owner=owner.pid), os.getpid(), time.monotonic()+2)
        self.assertTrue(result['clean'], result)
        self.assertTrue(all(f['before'] and not f['remaining'] for f in result['groups']))
        self.assertIsNone(unrelated.poll())
        os.kill(int((self.root/'unrelated.child').read_text()), 0)

    def test_unresponsive_tree_is_forced_and_never_reported_clean(self):
        owner = self.tree('owner', ignore=True)
        result = groups.stop_groups(dict(owner=owner.pid), os.getpid(), time.monotonic()+.8)
        owner.wait(timeout=3)
        self.assertFalse(result['clean'])
        self.assertTrue(result['groups'][0]['forced'])
        self.assertFalse(result['groups'][0]['remaining'])

    def test_partial_start_before_setsid_is_still_bounded_and_reaped(self):
        ready = self.root/'partial'
        code = ('import os,signal,time\nfrom pathlib import Path\n'
                'signal.signal(signal.SIGINT,signal.SIG_IGN)\n'
                f'Path({str(ready)!r}).touch()\n'
                'time.sleep(.15)\nos.setsid()\ntime.sleep(30)\n')
        child = subprocess.Popen([sys.executable, '-c', code])
        self.children.append((child, False))
        self.ready(ready, content=False)
        result = groups.stop_groups(dict(scene=child.pid), os.getpid(), time.monotonic()+.8)
        child.wait(timeout=3)
        self.assertFalse(result['clean'])
        self.assertTrue(result['groups'][0]['forced'])
        self.assertFalse(result['groups'][0]['remaining'])

    def test_current_supervisor_group_is_rejected_without_signalling(self):
        with self.assertRaises(ValueError):
            groups.stop_groups(dict(scene=os.getpgrp()), os.getpid(), time.monotonic()+1)

    def cleanup_script(self):
        source = Path(__file__).with_name('navigation_session.sh').read_text()
        cleanup = source[source.index('cleanup() {'):source.index('scene=${M6_NAVIGATION_SCENE')]
        cleanup = cleanup.replace('/simulation/navigation_groups.py', str(Path(groups.__file__)))
        return cleanup.replace('/output/', str(self.root) + '/')

    def supervisor(self, status, *, interrupts=()):
        cleanup = self.cleanup_script()
        def leaf(role):
            path = self.root/(role+'.child')
            return ('import os,signal,time\nfrom pathlib import Path\n'
                'signal.signal(signal.SIGINT,lambda *_:exit(0))\n'
                f'Path({str(path)!r}).write_text(str(os.getpid()))\ntime.sleep(30)\n')
        scene = self.root/'scene.py'
        scene.write_text('import os,signal,subprocess,sys,time\nfrom pathlib import Path\n'
            'signal.signal(signal.SIGINT,lambda *_:exit(0))\n'
            f'subprocess.Popen([sys.executable,"-c",{leaf("scene")!r}])\n'
            f'while not Path({str(self.root/"scene.child")!r}).exists():time.sleep(.01)\n'
            f'Path({str(self.root/"scene.ready")!r}).write_text(str(os.getpid()))\ntime.sleep(30)\n')
        owner = self.root/'owner.py'
        owner.write_text('import os,signal,subprocess,sys,time\nfrom pathlib import Path\n'
            'stopped=False\ndef stop(*_):\n global stopped\n stopped=True\n'
            'signal.signal(signal.SIGINT,stop)\n'
            f'subprocess.Popen([sys.executable,"-c",{leaf("owner")!r}])\n'
            f'while not Path({str(self.root/"owner.child")!r}).exists():time.sleep(.01)\n'
            f'Path({str(self.root/"owner.ready")!r}).write_text(str(os.getpid()))\n'
            'while not stopped:time.sleep(.01)\n'
            'os.kill(int(sys.argv[1]),0)\n'
            f'Path({str(self.root/"dependency-live")!r}).touch()\ntime.sleep(.2)\n')
        script = ('set -e\nsource "$1"\nowner_pid="" client_pid="" launch_pid="" bridge_pid="" collector_pid=""\n'
            'session_completed=1\n' + cleanup + '\n'
            f'setsid {sys.executable} {scene} &\nlaunch_pid=$!\n'
            f'setsid {sys.executable} {owner} "$launch_pid" &\nowner_pid=$!\n'
            f'while [[ ! -s {self.root/"scene.ready"} || ! -s {self.root/"owner.ready"} ]]; do sleep .01; done\n'
            + ('while true; do sleep .02; done\n' if interrupts else f'exit {status}\n'))
        process = subprocess.Popen(['bash', '-c', script, 'test',
            str(Path(__file__).with_name('navigation_cleanup.sh'))], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, start_new_session=True)
        self.children.append((process, True))
        self.ready(self.root/'owner.ready');self.ready(self.root/'scene.ready')
        self.inner_groups.update(int((self.root/(r+'.ready')).read_text())
                                 for r in ('owner','scene'))
        for signum in interrupts:
            process.send_signal(signum)
            time.sleep(.05)
        stdout, stderr = process.communicate(timeout=7)
        self.assertEqual(process.returncode, status, stdout + stderr)
        self.assertTrue((self.root/'dependency-live').exists())
        facts = json.loads((self.root/'launcher-processes.json').read_text())
        self.assertTrue(facts['clean'], facts)
        self.assertEqual({f['role'] for f in facts['groups']}, {'launch','owner'})
        self.assertTrue(all(f['reaped'] and not f['remaining'] for f in facts['groups']))
        self.assertEqual((self.root/'verification.json').exists(), status == 0)

    def test_success_is_marked_after_scene_and_owner_trees_close(self):
        self.supervisor(0)

    def test_partial_start_failure_preserves_original_exit_and_closes_trees(self):
        self.supervisor(17)

    def test_repeated_termination_preserves_signal_exit_and_closes_trees(self):
        self.supervisor(143, interrupts=(signal.SIGTERM, signal.SIGTERM))

    def test_interrupt_then_termination_preserves_original_signal_exit(self):
        self.supervisor(130, interrupts=(signal.SIGINT, signal.SIGTERM))

    def test_forced_collector_with_other_groups_absent_cannot_mark_success(self):
        ready = self.root/'collector.ready'
        child = self.root/'collector.py'
        child.write_text('import os,signal,time\nfrom pathlib import Path\n'
            'signal.signal(signal.SIGINT,signal.SIG_IGN)\n'
            f'Path({str(ready)!r}).write_text(str(os.getpid()))\ntime.sleep(30)\n')
        script = ('set -e\nsource "$1"\nowner_pid="" client_pid="" launch_pid="" bridge_pid="" collector_pid=""\n'
            'session_completed=1\n' + self.cleanup_script() + '\n'
            f'setsid {sys.executable} {child} &\ncollector_pid=$!\n'
            f'while [[ ! -s {ready} ]]; do sleep .01; done\nexit 0\n')
        process = subprocess.Popen(['bash','-c',script,'test',
            str(Path(__file__).with_name('navigation_cleanup.sh'))],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
        self.children.append((process, True))
        self.ready(ready);self.inner_groups.add(int(ready.read_text()))
        stdout, stderr = process.communicate(timeout=7)
        self.assertEqual(process.returncode, 1, stdout + stderr)
        self.assertFalse((self.root/'verification.json').exists())
        fact = json.loads((self.root/'collector-process.json').read_text())
        self.assertTrue(fact['group_forced'])
        self.assertTrue(fact['reaped'])
        self.assertFalse(fact['remaining'])


if __name__ == '__main__':
    unittest.main()
