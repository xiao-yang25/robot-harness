#!/usr/bin/env python3
"""Build and run a disposable, network-isolated Humble navigation simulation."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import uuid

from configure_mirror import validate_mirror_url

ROOT = Path(__file__).resolve().parents[3]
IMAGE = 'robot-harness-simulation:humble'
VISUAL_IMAGE = 'robot-harness-simulation:humble-visual'
CASES = ('normal', 'cancel-moving', 'replace-moving',
         'obstacle-wait', 'obstacle-wait-frozen-scan',
         'runtime-odometry-loss', 'runtime-odometry-replay',
         'runtime-odometry-resume', 'runtime-clock-loss',
         'runtime-stop-unreachable', 'runtime-stop-unreachable-withhold-drive-ack')


def write_host_record(path, text):
    # The workload can replace entries inside its output directory. Replace the
    # entry atomically instead of following a workload-created destination link.
    descriptor, temporary = tempfile.mkstemp(prefix='.host-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(text)
        os.replace(temporary, path)
    finally:
        if os.path.lexists(temporary):
            os.unlink(temporary)


def run_case(args):
    output = Path(args.output).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    # Keep the authoritative cleanup ID outside the container-writable mount.
    identity_directory = tempfile.TemporaryDirectory(prefix='robot-harness-')
    cidfile = Path(identity_directory.name) / 'container.cid'
    run_id = uuid.uuid4().hex
    container_name = 'robot-harness-sim-' + run_id
    create_attempted = False
    interrupted = 0

    def stop(signum, _frame):
        nonlocal interrupted
        interrupted = signum

    previous = {sig: signal.signal(sig, stop) for sig in (signal.SIGINT, signal.SIGTERM)}
    status = {'case': args.case, 'run_id': run_id, 'image': args.image,
              'container_name': container_name,
              'platform': 'linux/amd64', 'visual': bool(getattr(args, 'visual', False)),
              'result': 'incomplete'}
    exit_code = 1
    attached = None
    print(f'Logs and result: {output}', flush=True)
    try:
        image = subprocess.run(['docker', 'image', 'inspect', args.image,
                                '--format', '{{.Id}}'], check=True,
                               text=True, capture_output=True, timeout=30).stdout.strip()
        status['image_id'] = image
        # Use the inspected immutable local identity throughout this run.
        command = ['docker', 'create', '--cidfile', str(cidfile),
                   '--name', container_name,
                   '--init', '--network', 'none', '--platform', 'linux/amd64',
                   '--cpus=2', '--memory=4g', '--pids-limit=512', '--shm-size=256m',
                   '--cap-drop=ALL', '--security-opt=no-new-privileges',
                   '--user', f'{os.getuid()}:{os.getgid()}',
                   '--mount', f'type=bind,source={output},target=/output',
                   '-e', 'M4_ISOLATED_SIMULATION=1', '-e', f'M4_FRESH_NAV2_RUN={run_id}',
                   '-e', f'M4_CONTEXT_CASE={args.case}',
                   '-e', f'M4_VISUAL={int(getattr(args, "visual", False))}', image]
        create_attempted = True
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, timeout=60)
        container = cidfile.read_text().strip()
        write_host_record(output / 'container.cid', container + '\n')
        if interrupted:
            raise InterruptedError("interrupted before container start")
        with (output / 'container.log').open('w') as log:
            attached = subprocess.Popen(['docker', 'start', '--attach', container],
                                        stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 330
            while attached.poll() is None and not interrupted and time.monotonic() < deadline:
                time.sleep(0.2)
            if interrupted:
                exit_code = 128 + interrupted
                status['result'] = 'interrupted'
            elif attached.poll() is None:
                exit_code = 124
                status['result'] = 'host-timeout'
            else:
                state = json.loads(subprocess.run(
                    ['docker', 'inspect', '--format', '{{json .State}}', container],
                    check=True, text=True, capture_output=True, timeout=15).stdout)
                status['container_state'] = state
                exit_code = state['ExitCode'] or attached.returncode
                if state['Running'] or state['Status'] != 'exited' or state.get('OOMKilled'):
                    exit_code = exit_code or 1
                if exit_code == 0:
                    verification = json.loads((output / 'verification.json').read_text())
                    if (not isinstance(verification, dict) or verification.get('passed') is not True
                            or verification.get('case') != args.case):
                        exit_code = 1
                status['result'] = 'passed' if exit_code == 0 else 'failed'
    except (OSError, subprocess.SubprocessError, ValueError, KeyError) as error:
        exit_code = 1
        status['result'] = 'failed'
        status['error'] = str(error)
        print(f'Simulation failed: {error}', file=sys.stderr)
    finally:
        # Never select a resource by a shared tag/name. This invocation owns only
        # the container whose ID Docker wrote into its newly created directory.
        cleanup_ok = True
        if cidfile.exists() and cidfile.read_text().strip():
            try:
                subprocess.run(['docker', 'rm', '--force', cidfile.read_text().strip()],
                               check=True, timeout=30, stdout=subprocess.DEVNULL)
            except (OSError, subprocess.SubprocessError) as error:
                cleanup_ok = False
                status['cleanup_error'] = str(error)
            # Diagnostic failure must never prevent the cleanup attempt above.
            try:
                write_host_record(output / 'container.cid', cidfile.read_text())
            except OSError as error:
                exit_code = 1
                status['result'] = 'failed'
                status['error'] = f'cannot save container identity: {error}'
                print(f'Container ID: {cidfile.read_text().strip()}', file=sys.stderr)
        elif create_attempted:
            # A timed-out/disconnected create client does not prove that the
            # daemon created nothing. Retain the unique name for reconciliation.
            cleanup_ok = False
            status['cleanup_error'] = f'creation returned no ID; inspect {container_name}'
        if attached:
            try:
                attached.wait(timeout=5)
            except subprocess.TimeoutExpired:
                attached.kill()
                attached.wait()
        status['container_removed'] = cleanup_ok
        if not cleanup_ok:
            exit_code = 1
            status['result'] = 'cleanup-failed'
        if interrupted:
            status['result'] = 'interrupted'
            exit_code = 128 + interrupted
        status['exit_code'] = exit_code
        write_host_record(output / 'run.json', json.dumps(status, indent=2) + '\n')
        identity_directory.cleanup()
        for sig, handler in previous.items():
            signal.signal(sig, handler)
    print(f"{status['result']}; container removed: {status['container_removed']}", flush=True)
    return exit_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    build = commands.add_parser('build', help='build the image and run native/Owner unit checks')
    build.add_argument('--image')
    build.add_argument('--visual', action='store_true', help='include optional live display tools')
    build.add_argument('--ubuntu-mirror', help='optional HTTPS Ubuntu archive mirror')
    run = commands.add_parser('run', help='run one case in a fresh container')
    run.add_argument('case', choices=CASES)
    run.add_argument('--image')
    run.add_argument('--visual', action='store_true', help='capture actual RViz frames')
    run.add_argument('--output', required=True, help='new directory; existing paths are rejected')
    args = parser.parse_args()
    args.image = args.image or (VISUAL_IMAGE if args.visual else IMAGE)
    if args.command == 'build':
        command = ['docker', 'build', '--platform', 'linux/amd64',
                   '--progress=plain', '--target', 'visual' if args.visual else 'simulation',
                   '-t', args.image, '-f',
                   str(ROOT / 'integrations/ros2/simulation/Dockerfile')]
        if args.ubuntu_mirror:
            try:
                mirror = validate_mirror_url(args.ubuntu_mirror)
            except ValueError as error:
                parser.error(str(error))
            command.extend(['--build-arg', 'UBUNTU_MIRROR=' + mirror])
        return subprocess.call(command + [str(ROOT)])
    try:
        return run_case(args)
    except FileExistsError:
        parser.error('output directory already exists; select a new path')


if __name__ == '__main__':
    raise SystemExit(main())
