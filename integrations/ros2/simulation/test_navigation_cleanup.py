"""Supervisor preserves live dependencies until its own Owner has exited."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


HELPER = Path(__file__).with_name('navigation_cleanup.sh')
PROGRAM = r'''
set -e
source "$1"
report=$2 python=$3
sleep 30 & scene=$!
trap 'kill "$scene" "${owner:-}" 2>/dev/null || true; wait 2>/dev/null || true' EXIT
"$python" -u -c '
import os,signal,sys,time
signal.signal(signal.SIGINT, lambda *args: None)
print("ready", flush=True)
time.sleep(.3)
os.kill(int(sys.argv[1]), 0)
' "$scene" > "$report.ready" & owner=$!
while [[ ! -s $report.ready ]]; do sleep .01; done
stop_navigation_owner "$owner" 2 "$report"
kill -0 "$scene"
'''


class CleanupChecks(unittest.TestCase):
    def test_owner_finishes_with_scene_still_live_and_is_reaped(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)/'owner.json'
            run = subprocess.run(['bash', '-c', PROGRAM, 'test', str(HELPER),
                                  str(report), sys.executable], capture_output=True,
                                 text=True, timeout=5)
            self.assertEqual(run.returncode, 0, run.stderr)
            fact = json.loads(report.read_text())
            self.assertEqual(fact['returncode'], 0)
            self.assertFalse(fact['forced'])
            self.assertTrue(fact['reaped'])

    def test_unresponsive_owner_is_killed_after_finite_window(self):
        program = PROGRAM.replace('time.sleep(.3)', 'time.sleep(30)')
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)/'owner.json'
            run = subprocess.run(['bash', '-c', program, 'test', str(HELPER),
                                  str(report), sys.executable], capture_output=True,
                                 text=True, timeout=5)
            self.assertEqual(run.returncode, 0, run.stderr)
            fact = json.loads(report.read_text())
            self.assertNotEqual(fact['returncode'], 0)
            self.assertTrue(fact['forced'])
            self.assertTrue(fact['reaped'])


if __name__ == '__main__':
    unittest.main()
