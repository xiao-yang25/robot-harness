"""Failure-sensitive checks; use real child processes for query outcomes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from gazebo_pose import query_pose
from simulation_diagnostics import StreamProgress


class DiagnosticsTest(unittest.TestCase):
    def run_query(self, program, timeout=2):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch('gazebo_pose.COMMAND', [sys.executable, '-c', program]):
                try:
                    value = query_pose('test', timeout=timeout, output=output)
                    error = None
                except (ValueError, subprocess.SubprocessError) as caught:
                    value, error = None, caught
            record, = [json.loads(line) for line in (output / 'gazebo-queries.jsonl').read_text().splitlines()]
            return value, error, record

    def test_pose_keeps_all_six_values(self):
        value, error, record = self.run_query("print('1 2 3 0.1 0.2 0.3')")
        self.assertIsNone(error)
        self.assertEqual(value, [1, 2, 3, .1, .2, .3])
        self.assertEqual(record['outcome'], 'success')
        self.assertGreater(record['elapsed_ns'], 0)

    def test_invalid_pose_remains_failure(self):
        for text in ('1 2 3', '1 2 3 4 5 nan', 'not a pose'):
            with self.subTest(text=text):
                value, error, record = self.run_query(f'print({text!r})')
                self.assertIsNone(value)
                self.assertIsInstance(error, ValueError)
                self.assertEqual(record['outcome'], 'error')

    def test_process_error_retains_stderr(self):
        _, error, record = self.run_query("import sys; print('query failed', file=sys.stderr); sys.exit(7)")
        self.assertIsInstance(error, subprocess.CalledProcessError)
        self.assertEqual(record['returncode'], 7)
        self.assertIn('query failed', record['stderr'])

    def test_timeout_records_live_child_and_reaps_it(self):
        _, error, record = self.run_query("import time; print('started', flush=True); time.sleep(30)", timeout=.3)
        self.assertIsInstance(error, subprocess.TimeoutExpired)
        self.assertEqual(record['outcome'], 'timeout')
        self.assertEqual(record['timeout_seconds'], .3)
        self.assertEqual(record['query']['pid'], record['pid'])
        self.assertIn('started', record['stdout'])
        with self.assertRaises(ProcessLookupError):
            os.kill(record['pid'], 0)

    def test_snapshot_failure_cannot_prevent_timeout_cleanup(self):
        with patch('gazebo_pose.system_observation', side_effect=RuntimeError('probe failed')):
            _, error, record = self.run_query("import time; time.sleep(30)", timeout=.3)
        self.assertIsInstance(error, subprocess.TimeoutExpired)
        self.assertEqual(record['outcome'], 'timeout')
        self.assertEqual(record['diagnostic_error'], 'probe failed')
        with self.assertRaises(ProcessLookupError):
            os.kill(record['pid'], 0)

    def test_logging_failure_does_not_change_pose(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'not-a-directory'
            output.write_text('occupied')
            with patch('gazebo_pose.COMMAND', [sys.executable, '-c', "print('1 2 3 4 5 6')"]):
                self.assertEqual(query_pose('test', output=output), [1, 2, 3, 4, 5, 6])

    def test_repeated_stamp_is_receipt_without_progress(self):
        progress = StreamProgress()
        self.assertEqual(progress.snapshot(10), {})
        progress.observe('scan', 100, 10)
        progress.observe('scan', 100, 20)
        value = progress.snapshot(25)['scan']
        self.assertEqual(value['count'], 2)
        self.assertEqual(value['message_age_ns'], 5)
        self.assertEqual(value['progress_age_ns'], 15)
        progress.observe('scan', 90, 30)
        progress.observe('scan', 95, 40)
        value = progress.snapshot(45)['scan']
        self.assertEqual(value['stamp_ns'], 95)
        self.assertEqual(value['max_stamp_ns'], 100)
        self.assertEqual(value['progress_age_ns'], 35)
        progress.observe('scan', 101, 50)
        self.assertEqual(progress.snapshot(55)['scan']['progress_age_ns'], 5)


if __name__ == '__main__':
    unittest.main()
