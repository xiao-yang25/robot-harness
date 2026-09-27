"""Exercise bounded recorder cleanup with real subprocesses and GNU timeout.

The encoder is a fault-injection substitute; actual media needs a visual run.
"""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('timeout'),
                     'requires Linux with GNU timeout')
class RecordingCleanupTests(unittest.TestCase):
    def check_cleanup(self, mode):
        source = Path(__file__).with_name('run_owner_handoff.sh').read_text()
        # Load the actual cleanup functions without starting ROS or Docker.
        functions = source[source.index('finish_capture() {'):source.index('\nexport M4_WORK_SCOPE_CASE=normal')]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'ffmpeg').write_text('''#!/usr/bin/env python3
import os, signal, sys, time
from pathlib import Path
Path(os.environ['RECORDING_TEST_ROOT'], 'remux-called').touch()
if os.environ['RECORDING_TEST_MODE'] == 'stuck-remux':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    time.sleep(30)
sys.exit(1 if os.environ['RECORDING_TEST_MODE'] == 'failed-remux' else 0)
''')
            (root / 'ffmpeg').chmod(0o700)
            (root / 'child.py').write_text('''import signal, sys, time
from pathlib import Path
root, role, mode = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
def stop(*_):
    (root / (role + '-stopped')).touch()
    sys.exit(255 if role == 'capture' else 0)
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, signal.SIG_IGN if mode == 'stuck-capture' else stop)
(root / (role + '-ready')).touch()
if role == 'capture' and mode == 'early-capture':
    sys.exit(0)
time.sleep(30)
''')
            body = '''
helpers=()
for role in helper diagnostic capture; do
    python3 "$RECORDING_TEST_ROOT/child.py" "$RECORDING_TEST_ROOT" "$role" "$RECORDING_TEST_MODE" &
    case "$role" in
      helper) helpers+=("$!");;
      diagnostic) diagnostic_pid=$!;;
      capture) capture_pid=$!; helpers+=("$!");;
    esac
    while [[ ! -f "$RECORDING_TEST_ROOT/$role-ready" ]]; do sleep 0.02; done
done
if [[ $RECORDING_TEST_MODE == early-capture ]]; then
    sleep 30 & task=$!
    while kill -0 "$capture_pid" 2>/dev/null; do sleep 0.02; done
    supervise "$task"
fi
exit 0
'''
            env = dict(os.environ, RECORDING_TEST_ROOT=str(root), RECORDING_TEST_MODE=mode)
            env['PATH'] = str(root) + os.pathsep + env['PATH']
            process = subprocess.Popen(
                ['bash', '-c', 'set -eo pipefail\n' +
                 functions.replace('/output/', str(root) + '/') + body],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, start_new_session=True)
            try:
                output = process.communicate(timeout=15)
                expected = 0 if mode == 'success' else 125 if mode == 'early-capture' else 1
                self.assertEqual(process.returncode, expected, output)
                self.assertTrue((root / 'helper-stopped').exists(), output)
                self.assertTrue((root / 'diagnostic-stopped').exists(), output)
                self.assertEqual((root / 'remux-called').exists(), mode != 'stuck-capture')
            finally:
                # A broken cleanup must not leave fault-injection children behind.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.communicate()

    def test_clean_capture_reaches_helper_cleanup(self):
        self.check_cleanup('success')

    def test_failed_remux_fails_and_cleans_helpers(self):
        self.check_cleanup('failed-remux')

    def test_remux_ignoring_term_is_killed(self):
        self.check_cleanup('stuck-remux')

    def test_capture_ignoring_int_is_killed_without_remux(self):
        self.check_cleanup('stuck-capture')

    def test_encoder_exiting_early_even_with_zero_fails_scenario(self):
        self.check_cleanup('early-capture')
