"""Query actual Gazebo pose once, preserving the caller's failure/time budget."""
import math
from pathlib import Path
import subprocess
import time
from simulation_diagnostics import append_record, process_state, system_observation

COMMAND = ['gz', 'model', '-m', 'turtlebot3_waffle', '-p']


def query_pose(phase, timeout=5, output=Path('/output')):
    started = time.monotonic_ns()
    record = dict(event='gazebo_pose_query', phase=phase, started_ns=started,
                  timeout_seconds=timeout, outcome='error')
    try:
        with subprocess.Popen(COMMAND, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True) as process:
            record['pid'] = process.pid
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                # Snapshot before killing the query, while the server remains alive.
                record['outcome'] = 'timeout'
                try:
                    record.update(query=process_state(process.pid), **system_observation())
                except Exception as error:
                    record['diagnostic_error'] = str(error)
                finally:
                    process.kill()
                    stdout, stderr = process.communicate()
                    record.update(stdout=stdout[-4096:], stderr=stderr[-4096:])
                raise
            except BaseException:
                process.kill()
                process.communicate()
                raise
            record.update(returncode=process.returncode, stdout=stdout[-4096:], stderr=stderr[-4096:])
            if process.returncode:
                raise subprocess.CalledProcessError(process.returncode, COMMAND, stdout, stderr)
            values = [float(value) for value in stdout.split()]
            if len(values) != 6 or not all(math.isfinite(value) for value in values):
                raise ValueError('invalid Gazebo pose')
            record['outcome'] = 'success'
            return values
    finally:
        record['elapsed_ns'] = time.monotonic_ns() - started
        append_record(output / 'gazebo-queries.jsonl', record)
