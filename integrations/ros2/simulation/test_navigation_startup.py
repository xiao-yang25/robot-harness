"""Real supervisor children; one live child must not hide a failed sibling."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HELPER = Path(__file__).with_name('navigation_startup.sh')
PROGRAM = r'''
set -e
source "$1"
endpoint=$2 role=$3
sleep 30 & owner=$!
sleep 30 & scene=$!
sleep 30 & bridge=$!
trap 'kill "$owner" "$scene" "$bridge" 2>/dev/null || true; wait 2>/dev/null || true' EXIT
if [[ $role != none ]]; then
  case "$role" in owner) failed=$owner;; scene) failed=$scene;; bridge) failed=$bridge;; esac
  kill "$failed"
  wait "$failed" 2>/dev/null || true
fi
# An endpoint file cannot hide a child that has already exited.
touch "$endpoint"
wait_navigation_endpoint "$endpoint" 2 owner "$owner" scene "$scene" bridge "$bridge"
'''


class NavigationStartupTests(unittest.TestCase):
    def test_each_failed_child_rejects_readiness_even_with_two_live_siblings(self):
        for role in ('owner', 'scene', 'bridge'):
            with self.subTest(role=role), tempfile.TemporaryDirectory() as directory:
                result = subprocess.run(['bash', '-c', PROGRAM, 'test', str(HELPER),
                    str(Path(directory)/'endpoint'), role], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1)
                self.assertIn('role='+role, result.stderr)
                self.assertIn('exit=143', result.stderr)
                self.assertNotIn('timed out', result.stderr)

    def test_ready_endpoint_requires_all_three_children_alive(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(['bash', '-c', PROGRAM, 'test', str(HELPER),
                str(Path(directory)/'endpoint'), 'none'], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_endpoint_still_has_original_bounded_wait(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(['bash', '-c', PROGRAM.replace('touch "$endpoint"', ':'),
                'test', str(HELPER), str(Path(directory)/'endpoint'), 'none'],
                capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertIn('timed out after 2 seconds', result.stderr)


if __name__ == '__main__':
    unittest.main()
