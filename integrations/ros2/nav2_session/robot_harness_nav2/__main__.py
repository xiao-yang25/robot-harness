"""Owner-only entry for the exclusive, isolated two-context simulation profile."""
import argparse
import os
import math
from pathlib import Path
import subprocess


def require_isolation():
    if (not Path('/.dockerenv').exists()
            or os.environ.get('M6_NATIVE_ISOLATED') != '1'
            or os.environ.get('ROS_LOCALHOST_ONLY') != '1'):
        raise RuntimeError('requires the isolated scoped Humble/Nav2 simulation launcher')


def dispose(owner):
    """Best-effort local cleanup; container supervisor owns final OS removal."""
    errors = []
    def attempt(action):
        try:
            action()
        except Exception as error:
            errors.append(str(error))
    endpoint = getattr(owner, 'endpoint', None)
    if endpoint is not None:
        attempt(endpoint.dispose)
    process = getattr(owner, 'context_process', None)
    if process is not None:
        def reap():
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
        attempt(reap)
    log = getattr(owner, 'context_log', None)
    if log is not None:
        attempt(log.close)
    gate = getattr(owner, 'gate', None)
    if gate is not None:
        attempt(gate.close)
    # destroy_node releases partially initialized ROS clients/subscriptions too.
    if hasattr(owner, '_handle'):
        attempt(owner.destroy_node)
    return errors


def caller_wait_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or not 0 < seconds <= 60:
        raise argparse.ArgumentTypeError('caller wait must be finite and in (0, 60] seconds')
    return seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    from ._profile import PROFILE_ID, PERMISSION_PROFILE_ID, REVISION_PROFILE_ID, FAILURE_PROFILE_ID, PROFILES
    parser.add_argument('--profile', choices=PROFILES, default=PROFILE_ID,
                        help='explicit native motion policy; existing v1 is the default')
    parser.add_argument('--caller-wait-seconds', type=caller_wait_seconds,
                        help='bounded idle wait for each admission and final client close; '
                             'default admission 15s / final close 10s')
    args = parser.parse_args()
    require_isolation()  # Fail before importing ROS or starting any resource.
    import rclpy
    from ._owner import NavigationOwner, ClientClosed
    if args.profile == PERMISSION_PROFILE_ID:
        from ._permission import PermissionOwner
        NavigationOwner = PermissionOwner
    elif args.profile == REVISION_PROFILE_ID:
        from ._revision import RevisionOwner
        NavigationOwner = RevisionOwner
    elif args.profile == FAILURE_PROFILE_ID:
        from ._failure import FailureOwner
        NavigationOwner = FailureOwner
    from ._observations import emit, diagnose
    rclpy.init()
    owner = NavigationOwner.__new__(NavigationOwner)
    code = 0
    try:
        owner.__init__()
        owner.ready()
        owner.prepare_b_initial()
        for stage in ('A', 'B'):
            owner.wait(lambda: owner.execution.active is not None, args.caller_wait_seconds or 15, require_fresh=True)
            owner.open_scope(stage)
            owner.visit_and_close(stage)
        owner.completed = True
        owner.wait(lambda: owner.requests.closing, args.caller_wait_seconds or 10)
        owner.abort()
        emit('caller_complete')
    except ClientClosed as error:
        try:
            owner.abort()
        except Exception as cleanup_error:
            code = 1
            diagnose('core_abort_error', error=str(cleanup_error), stop='unknown')
        diagnose('client_closed', error=str(error), stop='unknown')
    except BaseException as error:
        code = 1
        try:
            owner.abort()
        except Exception as cleanup_error:
            diagnose('core_abort_error', error=str(cleanup_error), stop='unknown')
        diagnose('caller_error', error=str(error), stop='unknown')
    finally:
        errors = dispose(owner)
        try:
            rclpy.shutdown()
        except Exception as error:
            errors.append(str(error))
        if errors:
            code = 1
            diagnose('owner_dispose_errors', errors=errors, stop='unknown')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
