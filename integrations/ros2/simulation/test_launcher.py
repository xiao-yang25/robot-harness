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
                 output_links=False, blocked_record=False, session=False, custom_client=False, profile=simulate.PROFILES[0], scene='normal'):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'new'
            container = 'owned-container-id'
            calls = []
            outside = Path(directory) / 'outside-output'
            outside.write_text('untouched')
            prefix = Path(directory) / 'install'
            marker = prefix / 'lib/robot-harness/python/robot_harness_nav2/__main__.py'
            marker.parent.mkdir(parents=True)
            marker.write_text('')
            case = 'installed-nav2-session' if session else 'normal'
            client = Path(directory) / 'custom-client.py'
            client.write_text('raise SystemExit(0)')
            client_prefix = Path(directory) / 'client-install'
            client_prefix.mkdir()

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
                        (output / 'verification.json').write_text(json.dumps(dict(case=case, passed=True)))
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
                code = simulate.run_case(argparse.Namespace(output=str(output), image='image', case=case,
                    session=session, profile=profile, scene=scene, python_prefix=str(prefix),
                    client_script=str(client) if custom_client else None,
                    client_prefix=str(client_prefix) if custom_client else None,
                    caller_wait_seconds=45 if custom_client else None))
            status = json.loads((output / 'run.json').read_text())
            self.assertIn(['docker', 'rm', '--force', container], calls)
            self.assertEqual(outside.read_text(), 'untouched')
            if session:
                command = next(call for call in calls if call[1] == 'create')
                self.assertEqual(command[-2:], ['image-id', '/simulation/navigation_session.sh'])
                self.assertIn('none', command)
                self.assertIn('M6_NATIVE_ISOLATED=1', command)
                self.assertIn('M6_NAVIGATION_PROFILE='+profile, command)
                self.assertIn('M6_NAVIGATION_SCENE='+scene, command)
                self.assertTrue(any('target=/installed,readonly' in part for part in command))
                self.assertTrue(any('target=/navigation-example.py,readonly' in part for part in command))
                self.assertFalse(any('target=/simulation/navigation_requests.py' in part for part in command))
                if custom_client:
                    self.assertIn(f'type=bind,source={client.resolve()},target=/navigation-example.py,readonly', command)
                    self.assertIn(f'type=bind,source={client_prefix.resolve()},target=/client-prefix,readonly', command)
                    self.assertIn('M6_CLIENT_PREFIX=/client-prefix', command)
                    self.assertIn('M6_CALLER_WAIT_SECONDS=45', command)
                else:
                    self.assertNotIn('M6_CLIENT_PREFIX=/client-prefix', command)
                    self.assertFalse(any(part.startswith('M6_CALLER_WAIT_SECONDS=') for part in command))
            return code, status

    def test_explicit_permission_profile_reaches_container_and_host_record(self):
        _, status = self.exercise(session=True, profile=simulate.PROFILES[1])
        self.assertEqual(status['profile'], simulate.PROFILES[1])

    def test_explicit_revision_profile_reaches_container_and_host_record(self):
        _, status = self.exercise(session=True, profile=simulate.PROFILES[2])
        self.assertEqual(status['profile'], 'scoped-two-context-nav2-revision-v1')

    def test_occupied_a_scene_reaches_only_explicit_failure_session(self):
        _, status = self.exercise(session=True, profile=simulate.PROFILES[3], scene='occupied-a')
        self.assertEqual(status['scene'],'occupied-a')

    def test_invalid_scene_selection_starts_no_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'new'
            for session, profile, scene in ((False,simulate.PROFILES[3],'occupied-a'),
                    (True,simulate.PROFILES[0],'occupied-a'),(True,simulate.PROFILES[3],'other')):
                with self.subTest(scene=scene,profile=profile), patch.object(simulate.subprocess,'run') as docker:
                    with self.assertRaises(ValueError):
                        simulate.run_case(argparse.Namespace(output=str(output),session=session,profile=profile,scene=scene))
                    self.assertFalse(output.exists()); docker.assert_not_called()

    def test_explicit_failure_profile_reaches_container_and_host_record(self):
        _, status = self.exercise(session=True, profile=simulate.PROFILES[3])
        self.assertEqual(status['profile'], 'scoped-two-context-nav2-failure-recovery-v1')

    def test_installed_session_launch_has_separate_readonly_example_mount(self):
        code, status = self.exercise(session=True)
        self.assertEqual(code, 0)
        self.assertTrue(status['container_removed'])

    def test_external_client_is_readonly_with_explicit_idle_budget(self):
        code, status = self.exercise(session=True, custom_client=True)
        self.assertEqual(code, 0)
        self.assertTrue(status['container_removed'])

    def test_external_client_failure_preserves_exit_and_exact_cleanup(self):
        code, status = self.exercise(session=True, custom_client=True, exit_code=7)
        self.assertEqual(code, 7)
        self.assertEqual(status['result'], 'failed')
        self.assertTrue(status['container_removed'])

    def test_invalid_client_configuration_cannot_create_a_container(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(simulate.subprocess, 'run') as docker:
            with self.assertRaisesRegex(ValueError, 'requires --client-script'):
                simulate.session_client(argparse.Namespace(client_prefix=directory))
            for value in (float('nan'), 0, 61):
                with self.subTest(wait=value), self.assertRaisesRegex(ValueError, 'caller wait'):
                    simulate.session_client(argparse.Namespace(caller_wait_seconds=value))
            with self.assertRaises(FileNotFoundError):
                simulate.session_client(argparse.Namespace(client_script=directory+'/absent.py'))
            docker.assert_not_called()

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
