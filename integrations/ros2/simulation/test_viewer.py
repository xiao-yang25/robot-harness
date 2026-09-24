"""Viewer boundary tests; real HTTP/process checks do not execute Docker."""
import http.client
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

import viewer


class ViewerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.session = viewer.Session(self.root, 'test-image')

    def prepare(self, code=0, record=None, verification=None):
        self.session.output = self.root / 'session' / 'run'
        self.session.output.mkdir(parents=True)
        self.session.case = 'normal'
        self.session.process = Mock()
        self.session.process.poll.return_value = code
        if record is not None:
            (self.session.output / 'run.json').write_text(json.dumps(record))
        if verification is not None:
            (self.session.output / 'verification.json').write_text(json.dumps(verification))

    def test_exit_zero_or_owner_success_alone_cannot_pass(self):
        self.prepare(record={'result': 'passed', 'container_removed': False},
                     verification={'passed': True})
        (self.session.output / 'owner.jsonl').write_text(
            '{"event":"owner_handoff_result","passed":true,"b_settled":false}\n')
        self.assertFalse(self.session.snapshot()['passed'])
        (self.session.output / 'run.json').write_text(
            '{"result":"passed","container_removed":true}')
        self.assertTrue(self.session.snapshot()['passed'])
        (self.session.output / 'verification.json').unlink()
        self.assertFalse(self.session.snapshot()['passed'])

    def test_stale_and_completed_frames_are_never_live(self):
        self.prepare(code=None)
        frame = self.session.output / 'live.jpg'
        frame.write_bytes(b'frame')
        self.assertTrue(self.session.snapshot()['fresh'])
        old = time.time() - 10
        os.utime(frame, (old, old))
        self.assertFalse(self.session.snapshot()['fresh'])
        os.utime(frame, None)
        self.session.process.poll.return_value = 0
        self.assertFalse(self.session.snapshot()['fresh'])
        self.assertTrue(self.session.snapshot()['frame_available'])

    def test_symlink_and_fifo_outputs_are_not_read(self):
        outside = self.root / 'outside'
        outside.write_bytes(b'private')
        link = self.root / 'frame'
        link.symlink_to(outside)
        self.assertEqual(viewer.read_output(link), (None, None))
        pipe = self.root / 'pipe'
        os.mkfifo(pipe)
        self.assertEqual(viewer.read_output(pipe), (None, None))

    def test_incomplete_event_and_cancel_ack_do_not_imply_settlement(self):
        self.prepare(code=None)
        (self.session.output / 'owner.jsonl').write_text(
            '{"event":"owner_cancel_response","acknowledged":true,"settled":false}\n'
            '{"event":"owner_cancellation_result","settled":false,"passed":true}\n'
            '{"event":')
        snapshot = self.session.snapshot()
        self.assertFalse(snapshot['passed'])
        self.assertFalse(snapshot['result']['settled'])
        self.assertEqual(len(snapshot['timeline']), 2)

    def test_unknown_cleanup_blocks_new_run(self):
        self.prepare()
        with patch.object(viewer.subprocess, 'Popen') as launch:
            with self.assertRaises(ValueError):
                self.session.start('normal')
            launch.assert_not_called()

    def test_one_launcher_and_no_research_mounts(self):
        child = Mock()
        child.poll.return_value = None
        with patch.object(viewer.subprocess, 'Popen', return_value=child) as launch:
            self.session.start('replace-moving')
            command = launch.call_args.args[0]
            self.assertIn('--visual', command)
            self.assertIn('replace-moving', command)
            self.assertTrue(command[1].endswith('/simulation/simulate.py'))
            self.assertNotIn('docker', command)
            with self.assertRaises(ValueError):
                self.session.start('normal')
            self.session.stop()
            self.session.stop()
            child.terminate.assert_called_once()

    def test_close_terminates_and_reaps_real_child(self):
        child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
        self.addCleanup(lambda: child.kill() if child.poll() is None else None)
        self.session.process = child
        self.session.close()
        self.assertIsNotNone(child.poll())

    def test_http_origin_token_routes_and_session_isolation(self):
        self.prepare(code=None)
        (self.session.output / 'live.jpg').write_bytes(b'frame')
        server = viewer.ThreadingHTTPServer(('127.0.0.1', 0), viewer.Handler)
        server.authority = f'127.0.0.1:{server.server_port}'
        server.token = 'test-token'
        server.session = self.session
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(lambda: (server.shutdown(), server.server_close(), thread.join()))
        def request(method, path, headers=None):
            connection = http.client.HTTPConnection('127.0.0.1', server.server_port)
            try:
                connection.request(method, path, headers=headers or {})
                response = connection.getresponse()
                body = response.read()
                return response.status, body
            finally:
                connection.close()
        self.assertEqual(request('GET', '/api/status')[0], 200)
        self.assertEqual(request('GET', '/api/status', {'Host': 'evil.test'})[0], 403)
        self.assertEqual(request('POST', '/api/stop')[0], 403)
        headers = {'Origin': 'http://' + server.authority, 'X-Viewer-Token': server.token}
        self.assertEqual(request('POST', '/api/stop', dict(headers, Origin='http://evil.test'))[0], 403)
        self.assertEqual(request('POST', '/api/run/arbitrary', headers)[0], 404)
        self.assertEqual(request('GET', '/frame.jpg?session=old-session')[0], 404)
        self.assertEqual(request('GET', '/frame.jpg?session=session'), (200, b'frame'))
        self.assertEqual(request('GET', '/../../outside')[0], 404)
        self.assertEqual(request('POST', '/api/stop', headers)[0], 200)
        self.session.process.terminate.assert_called_once()


if __name__ == '__main__':
    unittest.main()
