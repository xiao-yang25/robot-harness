# Experimental Python session

An optional POSIX package connects a Python session host to the **existing C++
Core**. The default backend is a deterministic counter. An optional
[MuJoCo backend](../../integrations/mujoco/README.md) now runs the qualified ALOHA
trial with a consumer-supplied ACT worker. Only the host executes candidates.
This is an experimental Runtime session. Existing agents can consume it through
the optional [local MCP module](../../integrations/mcp/README.md); an embodied
business Agent may use this client directly. Its task strategy remains outside Runtime.

The client, host and worker are separate processes. One host thread drives Core,
checks current authority/capability/deadline before each effect, owns the candidate
queue and retains completed results/receipts. Client polling does not drive execution.
The private `_core` extension is not a stable public API; Core's C++ interface and
its default Python-free build are unchanged.

## Build and run

Requires a POSIX host, CMake3.18+, a C++17 compiler, Python3.9+ interpreter and its
matching development headers. Ubuntu22.04 uses `python3-dev`; choose the intended
interpreter explicitly when several are installed. On macOS the compiler and SDK
must be compatible; an explicit `CMAKE_OSX_SYSROOT` can select an installed SDK.

```sh
cmake -S . -B build-python -DROBOT_HARNESS_BUILD_PYTHON=ON \
  -DBUILD_TESTING=ON -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build build-python --target _core --parallel 2
ctest --test-dir build-python -R '^python_session$' --output-on-failure
PYTHONPATH="$PWD/build-python/python" python3 examples/python_session.py
```

Run these commands at the repository root. CMake copies Python sources during
configuration; building after a source edit updates that copy. Use the same Python
interpreter for building and running the extension.

The example performs seven increments, resets the backend explicitly, then performs
four increments using the same host and worker. It checks that the first result is
still available. Final result delivery, native outcome and settlement are distinct
receipt fields; arithmetic output does not imply a robotic domain verdict.

```python
from robot_harness import Session

with Session() as session:
    submission = session.submit("trial-1", steps=7)
    status = session.status("trial-1")  # may still be running
    observation = session.observe()
    # Poll status until state == "finished", then call reset before another trial.
```

`capabilities`, `observe`, `submit`, `status`, `cancel`, `reset` and `close` are
synchronous client calls to the independently progressing host. The client is not
thread-safe. Each valid submission ID is retained for the session, including a
refusal; repeated identical requests return their original decision/result, and
conflicting reuse fails. After an uncertain reply, query that ID rather than
resubmitting with a new one. No reconnect or host-restart recovery is implemented.

## Install and relocate

```sh
cmake --install build-python --prefix "$PWD/build-python-prefix"
PYTHONPATH="$PWD/build-python-prefix/lib/robot-harness/python" \
  python3 examples/python_session.py
```

The path uses the default `CMAKE_INSTALL_LIBDIR=lib`; use the chosen directory if
customized. This is a CMake-installed development package, not a published wheel.
The extension links Core statically. Its interpreter ABI must match the build;
relocating the prefix does not make it portable to another OS or Python version.
The Ubuntu workflow also tests an independent consumer after moving the prefix.
The same build/test/relocation sequence passed locally in an Ubuntu22.04 amd64
container with Python3.10.12; hosted workflow results are tracked in PR checks. See
[Testing](../../docs/TESTING.md#first-runtime-slice) for the environment boundary.

## Bounds and lifecycle

- One active operation, one pending prediction, at most100 queued candidates,
  at most400 native steps per operation and64 retained submission records.
- Private version2 frames have at most64 KiB of JSON plus an optional921,600-byte
  RGB payload (640×480×3); image bytes are not base64 JSON. Each pump sends/reads
  at most64 KiB and parses at most16 messages. Control buffers are128 KiB plus
  framing; image-enabled buffers add one maximum payload. Unknown optional metadata
  is ignored. Backpressure/malformed input fails the channel instead of growing it.
  Client `observe()` pulls the latest snapshot; it is not a live-video subscription.
  Version1 was an unpublished fixture protocol and is rejected; all participants
  must use the same package version.
- The fixture applies an increment no more frequently than every5 ms. This is a
  cooperative test cadence, not a real-time scheduler. Worker delay is a bounded
  test option; `worker_delay_ms` does not represent model performance.
- Cancellation/expiry clears queued candidates at host revocation. A late valid
  reply is discarded; settlement waits for outstanding work to resolve. No native
  step after observed revocation is allowed. Request send time is not revocation.
- Explicit close or caller disconnect closes admission, cancels active work and
  reaps the worker. The host first requests worker exit, then sends terminate after
  250 ms and kill after another250 ms if needed. It reports cleanup only after
  observing process exit. These thresholds are escalation policy, not hard bounds.
  A failed host or uninterruptible OS process is outside the guarantee.
  After reaping, an unread close reply gets at most1 s to flush before channel
  closure; a client missing that reply reports uncertainty. Repeated client close
  retries unconfirmed owner reaping rather than silently treating a timeout as success.
- The result sink is bounded host memory. Old Core receipts are copied before new
  admission; records are never silently evicted. Reset requires completed closure
  and a healthy worker. Native failure/cancel diagnostics do not become accepted
  successful output. All records are lost when the host exits.

For the design and remaining Agent boundary, see
[Runtime slice](../../docs/RUNTIME_SLICE.md). Validation scope and CI status belong
in [Testing](../../docs/TESTING.md#first-runtime-slice). No policy dependency is
installed by enabling this option.


## Continuous skills

The opt-in MuJoCo configuration `profile: "mujoco-stepped-handoff-v1"` retains one
episode between two finite skills. `submit` accepts an optional `skill` keyword;
default fixture/trial calls still use their original skill and10-step default.
For the new profile, omitted steps select400 for `aloha.transfer_cube_segment`
or50 for `aloha.hold_current_target`; other counts are refused.

```python
observation = session.observe()
submission = session.submit(
    "transfer", skill="aloha.transfer_cube_segment",
    expected_observation={"epoch": observation["epoch"], "sequence": observation["sequence"]},
)
# Wait for a finished, delivered result and settled receipt, then observe again.
observation = session.observe()
submission = session.submit(
    "hold", skill="aloha.hold_current_target",
    expected_observation={"epoch": observation["epoch"], "sequence": observation["sequence"]},
)
```

This sequence is a deterministic caller, not a model-backed Agent.
`capabilities()` adds per-skill bounds/availability and `requires_observation` for
this profile. A required epoch/sequence mismatch is retained as a refused request;
a new ID is required for a different decision. Optional metadata in the reference
is ignored. A current reference alone does not grant operation authority.

Transfer completion reports `segment_executed`; hold completion reports
`hold_executed`. These are native program outcomes, not task success. The episode
sequence advances0→400→450 while operation steps are400 and50. Transfer must
settle with accepted output before hold becomes available. Pending inference,
invalid observations, cancellation, expiry or execution failure block follow-up.
The Host repeats its saved target for hold without a worker prediction or caller
raw actions. It does not check contact truth to choose whether a hold is useful.
After hold or healthy cancellation, explicit reset is required for another task.
Worker/native failures make the session unavailable; they do not authorize reset
or automatic recovery.

[MuJoCo setup and example](../../integrations/mujoco/README.md#continuous-handoff)
provide the consumer paths. The Host keeps the recording open between operations;
the transfer result's video path names that still-growing recording. Read the
completed video after hold, reset or close confirms recorder closure. No live-video
subscription, arbitrary skill registry or physical safety guarantee is introduced.
