#!/usr/bin/env python3
"""Loopback viewer for the packaged simulation's existing scenarios and evidence."""
import argparse
import json
import os
from pathlib import Path
import secrets
import signal
import stat
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

from simulate import ROOT, VISUAL_IMAGE

HERE = Path(__file__).resolve().parent
CASES = ('normal', 'cancel-moving', 'replace-moving')


def read_output(path, limit=8 * 1024 * 1024):
    """Do not follow files replaced by the container with symlinks or devices."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    except (FileNotFoundError, OSError):
        return None, None
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            return None, None
        return stream.read(limit), info.st_mtime


def read_json(path):
    data, _ = read_output(path)
    try:
        result = json.loads(data) if data else None
        return result if isinstance(result, dict) else None
    except (ValueError, UnicodeError):
        return None


def read_events(path):
    data, _ = read_output(path)
    events = []
    for line in (data or b'').splitlines():
        try:
            event = json.loads(line)
            if isinstance(event, dict):
                events.append(event)
        except (ValueError, UnicodeError):
            pass  # A writer may not yet have finished its current line.
    return events


class Session:
    def __init__(self, directory, image):
        self.directory = Path(directory).resolve()
        self.image = image
        self.lock = threading.RLock()
        self.process = None
        self.output = None
        self.case = None
        self.closing = False
        self.stopping = False

    def running(self):
        return self.process is not None and self.process.poll() is None

    def start(self, case):
        with self.lock:
            if case not in CASES:
                raise ValueError('未知演示。')
            if self.closing or self.running():
                raise ValueError('请等待当前演示结束及资源回收。')
            if self.process:
                previous = read_json(self.output / 'run.json')
                if not previous or previous.get('container_removed') is not True:
                    raise ValueError('上次资源回收未确认，请先检查该次运行记录。')
            session = self.directory / ('viewer-' + secrets.token_hex(12))
            session.mkdir(parents=True)
            output = session / 'run'
            # The existing launcher alone creates/owns/removes the container.
            # Keep its host console outside the container's writable directory.
            command = [sys.executable, str(HERE / 'simulate.py'), 'run', case,
                       '--visual', '--image', self.image, '--output', str(output)]
            with (session / 'launcher.log').open('wb') as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            self.process, self.output, self.case = process, output, case
            self.stopping = False

    def stop(self):
        with self.lock:
            if self.running() and not self.stopping:
                self.process.terminate()  # Launcher handles SIGTERM and owned cleanup.
                self.stopping = True

    def close(self):
        with self.lock:
            self.closing = True
            self.stop()
            process = self.process
        if process:
            try:
                process.wait(timeout=100)
            except subprocess.TimeoutExpired:
                print('Launcher cleanup still pending; inspect this session before restarting.',
                      file=sys.stderr)

    def snapshot(self):
        with self.lock:
            running = self.running()
            events = read_events(self.output / 'owner.jsonl') if self.output else []
            record = read_json(self.output / 'run.json') if self.output else None
            verification = read_json(self.output / 'verification.json') if self.output else None
            frame, stamp = read_output(self.output / 'live.jpg') if self.output else (None, None)
            result = next((e for e in reversed(events)
                           if e.get('event') in ('owner_handoff_result', 'owner_cancellation_result')), None)
            feedback = next((e for e in reversed(events) if e.get('event') == 'feedback'), None)
            # The final launcher result includes checker, exit and cleanup outcome.
            passed = (not running and self.process is not None and self.process.poll() == 0
                      and bool(record) and record.get('result') == 'passed'
                      and record.get('container_removed') is True
                      and bool(verification) and verification.get('passed') is True)
            timeline = [e for e in events if e.get('event') in (
                'harness_admitted', 'owner_cancel_requested', 'owner_cancel_response', 'native_result',
                'owner_quiet_observation', 'owner_a_closed', 'owner_core_handoff', 'owner_handoff_result',
                'owner_cancellation_result', 'owner_candidate_ready')]
            return dict(viewer='robot-harness-simulation-viewer',
                        session=self.output.parent.name if self.output else None,
                        case=self.case, running=running, stopping=self.stopping and running,
                        started=self.process is not None, passed=passed,
                        fresh=running and stamp is not None and 0 <= time.time() - stamp < 3,
                        frame_available=bool(frame), frame_time=stamp,
                        feedback=feedback, result=result, verification=verification,
                        handoff=next((e for e in reversed(events)
                                      if e.get('event') == 'owner_core_handoff'), None),
                        record=record, timeline=timeline[-30:],
                        output=str(self.output) if self.output else None,
                        exit_code=self.process.poll() if self.process else None)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send_data(self, status, content, kind):
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(content)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' blob:; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(content)

    def send_json(self, status, value):
        self.send_data(status, json.dumps(value).encode(), 'application/json')

    def do_GET(self):
        if self.headers.get('Host') != self.server.authority:
            self.send_error(403)
            return
        url = urlsplit(self.path)
        if url.path == '/api/status':
            self.send_json(200, self.server.session.snapshot())
        elif url.path == '/':
            self.send_data(200, (HERE / 'viewer.html').read_text().replace(
                '__TOKEN__', self.server.token).encode(), 'text/html; charset=utf-8')
        elif url.path == '/mark.svg':
            self.send_data(200, (ROOT / 'docs/assets/brand/kingfisher-dark.svg').read_bytes(),
                           'image/svg+xml')
        elif url.path == '/frame.jpg':
            with self.server.session.lock:
                state = self.server.session.snapshot()
                valid = parse_qs(url.query).get('session') == [state['session']]
                data, _ = (read_output(self.server.session.output / 'live.jpg')
                           if valid and state['frame_available'] else (None, None))
            if data:
                self.send_data(200, data, 'image/jpeg')
            else:
                self.send_error(404)
        else:
            self.send_error(404)

    def do_POST(self):
        if (self.headers.get('Host') != self.server.authority
                or self.headers.get('Origin') != 'http://' + self.server.authority
                or self.headers.get('X-Viewer-Token') != self.server.token):
            self.send_error(403)
            return
        if self.headers.get('Content-Length', '0') != '0' or self.headers.get('Transfer-Encoding'):
            self.send_error(400)
            return
        path = urlsplit(self.path).path
        try:
            if path == '/api/stop':
                self.server.session.stop()
            elif path in ['/api/run/' + case for case in CASES]:
                self.server.session.start(path.removeprefix('/api/run/'))
            else:
                self.send_error(404)
                return
            self.send_json(200, self.server.session.snapshot())
        except ValueError as error:
            self.send_json(409, {'error': str(error)})
        except (OSError, subprocess.SubprocessError) as error:
            print(f'Viewer failed: {error}', file=sys.stderr, flush=True)
            self.send_json(503, {'error': '启动器未完成操作，请查看本次 launcher.log。'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--image', default=VISUAL_IMAGE)
    parser.add_argument('--output', default='simulation-results/viewer')
    parser.add_argument('--open', action='store_true')
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.authority = f'127.0.0.1:{server.server_port}'
    server.token = secrets.token_urlsafe(32)
    server.session = Session(args.output, args.image)
    def interrupt(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupt)
    print('Viewer ready: http://' + server.authority, flush=True)
    if args.open:
        webbrowser.open('http://' + server.authority)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.session.close()
        server.server_close()


if __name__ == '__main__':
    main()
