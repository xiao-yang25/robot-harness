"""Host result/cleanup boundaries; these do not simulate robot motion."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import simulate


class LauncherTest(unittest.TestCase):
    def exercise(self, *, running=False, cleanup_error=False, verification=True, exit_code=0,
                 output_links=False, blocked_record=False):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new'
            container = 'owned-container-id'
            calls = []
            outside = Path(directory) / 'outside-output'
            outside.write_text('untouched')

            def docker(command, **_kwargs):
                calls.append(command)
                if command[1:3] == ['image', 'inspect']:
                    return subprocess.CompletedProcess(command, 0, 'image-id')
                if command[1] == 'create':
                    Path(command[command.index('--cidfile') + 1]).write_text(container)
                    # A workload can write its output; it must not select which
                    # host container is removed, even with a custom image.
                    (output / 'container.cid').write_text('not-owned-container')
                    if verification:
                        (output / 'verification.json').write_text('{"case":"normal","passed":true}')
                if command[1] == 'inspect':
                    if blocked_record:
                        (output / 'container.cid').unlink()
                        (output / 'container.cid').mkdir()
                    if output_links:
                        for name in ('container.cid', 'run.json'):
                            path = output / name
                            path.unlink(missing_ok=True)
                            path.symlink_to(outside)
                    return subprocess.CompletedProcess(command, 0, json.dumps({
                        'Running': running, 'Status': 'running' if running else 'exited',
                        'ExitCode': exit_code, 'OOMKilled': False}))
                if command[1] == 'rm' and cleanup_error:
                    raise subprocess.CalledProcessError(1, command)
                return subprocess.CompletedProcess(command, 0, '')

            attached = Mock(returncode=0)
            attached.poll.return_value = 0
            with patch.object(simulate.subprocess, 'run', side_effect=docker), \
                    patch.object(simulate.subprocess, 'Popen', return_value=attached):
                code = simulate.run_case(argparse.Namespace(output=str(output), image='image', case='normal'))
            status = json.loads((output / 'run.json').read_text())
            self.assertIn(['docker', 'rm', '--force', container], calls)
            self.assertEqual(outside.read_text(), 'untouched')
            return code, status

    def test_natural_exit_and_checker_pass(self):
        code, status = self.exercise()
        self.assertEqual(code, 0)
        self.assertEqual(status['result'], 'passed')
        self.assertTrue(status['container_removed'])

    def test_attach_detach_is_not_completion(self):
        code, status = self.exercise(running=True)
        self.assertNotEqual(code, 0)
        self.assertEqual(status['result'], 'failed')

    def test_cleanup_failure_is_not_pass(self):
        code, status = self.exercise(cleanup_error=True)
        self.assertNotEqual(code, 0)
        self.assertEqual(status['result'], 'cleanup-failed')
        self.assertFalse(status['container_removed'])

    def test_missing_checker_cannot_pass(self):
        code, status = self.exercise(verification=False)
        self.assertNotEqual(code, 0)
        self.assertEqual(status['result'], 'failed')

    def test_container_failure_preserved(self):
        code, status = self.exercise(exit_code=124)
        self.assertEqual(code, 124)
        self.assertEqual(status['result'], 'failed')

    def test_existing_output_is_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'evidence'
            marker.write_text('retained')
            with patch.object(simulate.subprocess, 'run') as docker:
                with self.assertRaises(FileExistsError):
                    simulate.run_case(argparse.Namespace(output=directory, image='image', case='normal'))
            docker.assert_not_called()
            self.assertEqual(marker.read_text(), 'retained')

    def test_output_links_do_not_redirect_host_writes(self):
        code, status = self.exercise(output_links=True)
        self.assertEqual(code, 0)
        self.assertEqual(status['result'], 'passed')

    def test_record_failure_does_not_prevent_cleanup(self):
        code, status = self.exercise(blocked_record=True)
        self.assertNotEqual(code, 0)
        self.assertTrue(status['container_removed'])

    def test_mirror_is_normalized(self):
        self.assertEqual(simulate.validate_mirror_url('https://archive.example/ubuntu/'),
                         'https://archive.example/ubuntu')

    def test_mirror_rejects_source_injection_and_credentials(self):
        for value in ('https://archive.example/ubuntu\ntrusted=yes',
                      'https://user:secret@archive.example/ubuntu'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                simulate.validate_mirror_url(value)

    def test_create_timeout_is_not_confirmed_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new'
            inspected = subprocess.CompletedProcess([], 0, 'image-id')
            with patch.object(simulate.subprocess, 'run', side_effect=[
                    inspected, subprocess.TimeoutExpired(['docker', 'create'], 60)]):
                code = simulate.run_case(argparse.Namespace(output=str(output), image='image', case='normal'))
            status = json.loads((output / 'run.json').read_text())
            self.assertNotEqual(code, 0)
            self.assertFalse(status['container_removed'])
            self.assertTrue(status['container_name'].startswith('robot-harness-sim-'))


if __name__ == '__main__':
    unittest.main()
