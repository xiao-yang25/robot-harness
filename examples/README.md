# Runnable examples

First follow the [build instructions](../README.md#build). All commands below run
from the repository root and assume the build directory is `build`. Substitute
your directory in both the executable and worker paths if different. Examples
return nonzero when their checks fail; a successful run establishes only the
stated software scenario.

## Normal execution

```sh
./build/robot_harness_normal_execution
```

Runs sample A (`2, -3, 4`, sum of squares `29`) followed by B (`5, 6`, sum `61`)
in synchronous and deferred completion modes. The four final receipts distinguish
native success, accepted output and settlement; the domain verdict is unassessed.
No worker thread, ROS installation or model is needed. In deferred mode the caller
explicitly advances the worker and processes its callbacks.

Read [normal_execution.cpp](normal_execution.cpp), then the
[event-by-event walkthrough](../docs/TESTING.md#manual-verification).

## Failure, cancellation and deadlines

```sh
./build/robot_harness_failure_execution
./build/robot_harness_cancellation_execution
./build/robot_harness_deadline_execution
```

The failure example blocks a second operation until the failed operation settles,
then returns `16` for a fresh request. The cancellation example uses a worker that
acknowledges but ignores stop: its eventual native success does not deliver a
result. The deadline example advances a manual clock, then adds cancellation and
checks that only one native stop request was issued. These controlled fixtures do
not measure real stopping latency.

Sources: [failure](failure_execution.cpp), [cancellation](cancellation_execution.cpp),
[deadline](deadline_execution.cpp). Further observations and focused tests are in
[manual verification](../docs/TESTING.md#manual-verification).

## Replacement and rebinding

```sh
./build/robot_harness_replacement_execution
./build/robot_harness_rebinding_execution
```

Replacement changes A to B within one binding: B waits for A's settlement, a real
held result from A is replayed, and the sink rejects it while accepting B's `61`.
Rebinding withdraws the old provider and waits for cleanup before granting a fresh
binding. The fixture deliberately controls callback order; it is not a production
message-replay service.

Sources: [replacement](replacement_execution.cpp), [rebinding](rebinding_execution.cpp).
See [replacement checks](../docs/TESTING.md#m3a-replacement-checks) and
[rebinding checks](../docs/TESTING.md#m3b-focused-acceptance-mapping).

## Local compute and two-step tasks

These examples run on the verified macOS/Linux environments and create real child
processes. Worker arguments must be **absolute paths**:

```sh
./build/robot_harness_compute_execution "$(pwd)/build/robot_harness_compute_worker"
./build/robot_harness_two_step_compute normal "$(pwd)/build/robot_harness_compute_worker"
```

The compute example observes a sum of `332833505`, process exit and cleanup. The
two-step caller requests `3` iterations and accepts `5`, then derives `6` iterations
and accepts `55`. Actual cleanup completes before the next step starts. The task
controller makes the application-level success judgment.

Use `revise` or `rebind` instead of `normal` to change the goal and produce `14`
then `91`. `rebind` also replaces the provider. `shutdown` cancels pending intent
and drives closure. Controlled `failure` and `unknown` modes need the specific
fixture workers documented in [task checks](../docs/TESTING.md#finite-task-caller-checks).

To observe provider replacement at the process boundary:

```sh
./build/robot_harness_compute_rebinding_execution   "$(pwd)/build/robot_harness_compute_worker"   "$(pwd)/build/robot_harness_compute_worker"
```

The same worker binary is installed under a fresh provider generation after the
old child exits, is reaped and its channels close. The new operation returns
`332833505`; this demonstrates rebinding, not a binary upgrade.

Sources: [compute driver](compute_execution.cpp), [two-step driver](two_step_compute.cpp),
[task controller](finite_compute_task.cpp), [compute rebinding](compute_rebinding_execution.cpp).
The [compute verification guide](../docs/TESTING.md#local-compute-usage-and-verification)
describes the polling and native ownership assumptions.

## Host recovery on Linux

Build and run these inside Linux, including the documented Ubuntu container.
They are not built on macOS. A surviving Owner retains the native child while
separate Host processes use a private recovery protocol.

```sh
./build/robot_harness_recovery_execution normal "$(pwd)/build/robot_harness_compute_worker"
./build/robot_harness_recovery_execution task-handoff "$(pwd)/build/robot_harness_compute_worker"
```

`normal` prints `result=5 launches=1 cleanup=1`. `task-handoff` kills an example
Host while its old work is still present. Its replacement initially blocks new
work, then records the interrupted goal as unknown even after permission recovers.
Only a separate explicit new goal produces `14` and `91`; total launches are
three, including the old first step. Controlled suspension establishes ordering,
not a bound on stop time. The demonstration manages its own child processes.

Additional modes and expected observations are in
[recovery checks](../docs/TESTING.md#m3c-prototype-checks). Read
[recovery_execution.cpp](recovery_execution.cpp) and the
[task bridge](recovery_task_execution.cpp). This private prototype requires a
live, polled Owner; it is not durable task recovery or an installed recovery API.

## If a command fails

- Missing executable: check that the build succeeded and that the example is
  available on your platform. Linux recovery targets do not exist on macOS.
- Worker prerequisites unavailable: use an absolute path to the built executable;
  check that it belongs to the same platform/build as the driver.
- Unexpected receipt or timeout: run the matching CTest from [Testing](../docs/TESTING.md)
  and include its failing output, revision and environment in a
  [bug report](https://github.com/xiao-yang25/robot-harness/issues/new?template=bug_report.md).
  A timeout is not proof of cleanup or permission to retry work.
- Compiler/SDK mismatch on macOS: follow [SDK selection](../docs/TESTING.md#macos-sdk-selection)
  and use a fresh build directory for the corrected toolchain.
