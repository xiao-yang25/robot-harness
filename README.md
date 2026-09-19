# Robot Harness

A runtime for reliable robot execution, interruption, handoff, and recovery.

Robot Harness is a Physical AI Harness/Runtime project for integrating robot
capabilities with agents, behavior trees, and ordinary applications. Its first
implementation is a small, embeddable C++17 core for execution authority and
truthful operation feedback.

## Status

**M3a operation replacement is merged in `3fd5474` through
[PR #5](https://github.com/xiao-yang25/robot-harness/pull/5).** All 29 CTests passed on
macOS ARM64 and Ubuntu ARM64 Docker in normal and ASan/UBSan builds. The example
changes A to B within one live host/binding: B waits for A's settlement, A's late
result is rejected at the managed sink, and the compute path confirms child
exit/reaping before replacement. The [merged main-branch CI](https://github.com/xiao-yang25/robot-harness/actions/runs/35438203500)
also passed all 29 tests normally and with ASan/UBSan. See [replacement checks](docs/TESTING.md#m3a-replacement-checks).

The merged baseline `6bba453` includes normal execution, failures, cancellation,
deadlines and the local compute process adapter. Its recorded
[main-branch Ubuntu CI](https://github.com/xiao-yang25/robot-harness/actions/runs/34951525988)
passed 26 tests in both normal and ASan/UBSan builds. The compute adapter supplies
capability-based admission, running cancellation, exit collection and channel
cleanup for a bounded local workload. See [compute verification](docs/TESTING.md#local-compute-usage-and-verification)
and the [public host interface](include/robot_harness/compute_execution.hpp).

Cancellation and expiry revoke future dispatch/result permission; acknowledgement,
native termination, result disposition and cleanup remain separate facts. An
ignored stop can still end in native success while its result is discarded.
The sample fixture controls result acceptance; the compute profile additionally
covers declared local process behavior. Neither proves physical stopping, a hard
stop deadline or host-independent supervision. Stronger tasks require the
corresponding [stop and resource guarantees](docs/DESIGN.md#stop-and-resource-requirements-before-expanded-execution).

The working M3b increment adds [Core, sample and compute provider rebinding](docs/DESIGN.md#m3b-implementation-design-live-host-binding-handoff):
withdraw the old binding, preserve its cleanup obligations, then enable a fresh
provider only after old-scope quiescence and new readiness. Including the task
caller and withdrawal-allocation regression, all 47 CTests pass
locally on macOS and Ubuntu ARM64 Docker, in Debug and ASan/UBSan builds. The
Core/sample, compute and task-caller increments passed independent implementation review.
These uncommitted changes have no corresponding GitHub CI run yet. See the
[handoff examples and verification](docs/TESTING.md#m3b-focused-acceptance-mapping).
A minimal [two-step task caller](#two-step-task-example) now exercises public
compute APIs; its verification is tracked in [task checks](docs/TESTING.md#finite-task-caller-checks).
M3c restart recovery, M4 ROS integration,
a complete Agent and physical-task validation remain separate pending work.

Earlier research experiments support the shared-core architecture; they do not
establish production performance, physical safety, or end-to-end task success.

## Build

Requires CMake 3.16 or newer and a C++17 compiler. ROS is not required for the core.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
(cd build && ctest --output-on-failure)
```

For a local Ubuntu development container, see the
[Docker setup and validation commands](docs/TESTING.md#ubuntu-development-container).

## Normal execution example

After building, run:

```sh
./build/robot_harness_normal_execution
```

The example submits real sample data to a deterministic worker, receives a result
through a managed sink, inspects a layered receipt, and submits a second sample.
It exercises completion during submission and completion deferred until the host
explicitly advances the worker. No worker thread, ROS installation, or model is
required.

The caller must distinguish Core admission, native acceptance and completion,
result acceptance, and required cleanup. A native completion alone does not make
the domain available for a second conflicting request. The fixture result can be
checked directly; the domain/task verdict remains unassessed.

The example is the first caller of provisional repository-local C++ interfaces,
not a stable installed SDK. Its source is in
[normal_execution.cpp](examples/normal_execution.cpp).

Each completion mode runs sample A (`2, -3, 4`, sum of squares `29`) and then
sample B (`5, 6`, sum of squares `61`). Results and receipt layers are printed
from the actual execution. Link `robot_harness_core` for the Core alone, or
`robot_harness_sample_fixture` to use this deterministic host and worker.

## Failure execution example

```sh
./build/robot_harness_failure_execution
```

This example drives an accepted operation through a controlled native failure,
attempts another request while cleanup is pending and checks that it is blocked.
After processing output disposition and settlement, the caller submits a fresh
operation whose actual result is `16`. There is no automatic retry. See
[failure_execution.cpp](examples/failure_execution.cpp) and the
[M2a interface](docs/DESIGN.md#m2a-caller-and-integration-surface).

## Cancellation and deadline examples

```sh
./build/robot_harness_cancellation_execution
./build/robot_harness_deadline_execution
```

The cancellation example requests a stop that the worker acknowledges and ignores.
It checks blocked admission, later native success with no delivered result, then
executes a fresh request after cleanup. The deadline example moves a manual clock
to the deadline, calls `poll()`, adds a caller cancellation and verifies exactly one
native stop. Both print actual checks and return nonzero if an expected result fails.
See [manual verification](docs/TESTING.md#manual-verification) and the sources:
[cancellation](examples/cancellation_execution.cpp),
[deadline](examples/deadline_execution.cpp).

## Replacement example

```sh
./build/robot_harness_replacement_execution
```

The caller changes A (`2, -3, 4`) to B (`5, 6`). Cancellation revokes A's result
permission, while B stays blocked until A finishes and settles. Once B has
computed, the fixture replays A's real result before B's result is delivered.
The sink rejects the old authority and accepts only B's result, `61`.
The [example](examples/replacement_execution.cpp) checks these outcomes and exits
nonzero if they fail. Replay is an explicit fixture control, not a production retry.

## Two-step task example

```sh
./build/robot_harness_two_step_compute normal "$(pwd)/build/robot_harness_compute_worker"
```

Run from the repository root; the worker argument must be an absolute path.
The caller submits `3` iterations and checks the result `5`. It then selects
`6` iterations from that accepted result and checks `55`. Each step waits for
actual native cleanup before the next starts. Task success is an application
judgment; Core's domain verdict remains unassessed.

Use `revise` or `rebind` instead of `normal` to change the goal, wait for old
cleanup, and produce `14` then `91`; `rebind` also explicitly replaces the
provider. `shutdown` cancels pending intent and drives closure. Controlled
failure/unknown modes and their worker commands are in
[task checks](docs/TESTING.md#finite-task-caller-checks).
The [example controller](examples/finite_compute_task.hpp) owns goal revisions and
steps; it is not a public task scheduler or complete Agent.

## Code map

Implementation is grouped by module under `src/core/`, `src/compute/` and
`src/sample/`. Public headers remain under `include/robot_harness/`; private
process/protocol headers stay inside `src/compute/`. See
[module boundaries](docs/DESIGN.md#source-modules-and-public-headers).

| Entry | Role |
|---|---|
| [Core interface](include/robot_harness/authority_gate.hpp) and [implementation](src/core/authority_gate.cpp) | Operation identity, admission, evidence validation, and current receipt; no sample payload |
| [Sample host interface](include/robot_harness/sample_execution.hpp) and [implementation](src/sample/sample_execution.cpp) | Host, deterministic adapter/worker, callback staging, and managed result storage |
| [Compute host](include/robot_harness/compute_execution.hpp) and [implementation](src/compute/compute_execution.cpp) | Local process adapter backed by the private process owner and worker protocol in the same source directory |
| [Application example](examples/normal_execution.cpp) | Initializes the host and runs A then B in both completion modes |
| [Replacement example](examples/replacement_execution.cpp) and [M3a tests](tests/m3a_replacement_tests.cpp) | Caller-driven replacement and stale-result rejection at the actual managed sink |
| [Two-step driver](examples/two_step_compute.cpp), [controller](examples/finite_compute_task.cpp) and [task tests](tests/finite_compute_task_tests.cpp) | Public-API application loop, dependent results, goal changes, failure/uncertainty and cleanup |
| [Core tests](tests/authority_gate_tests.cpp), [normal fixture tests](tests/sample_execution_tests.cpp) and [M2a tests](tests/m2a_failure_tests.cpp), [cancellation tests](tests/m2b_cancellation_tests.cpp), [deadline tests](tests/m2c_deadline_tests.cpp) | Evidence boundaries, actual normal/failure/cancel/expiry execution, delivery, cleanup and shutdown |

For the request-to-result sequence, see [M1 caller and integration surface](docs/DESIGN.md#m1-caller-and-integration-surface).
For commands and what to observe, see [manual verification](docs/TESTING.md#manual-verification).

## Continuous integration

The [Ubuntu CI workflow](.github/workflows/core.yml) runs the same CMake/CTest
suite on GitHub-hosted Ubuntu 22.04. macOS remains a local development environment;
ROS integration and timing measurements use their relevant Ubuntu/Linux targets.
See [testing environments and evidence boundaries](docs/TESTING.md). A workflow
file alone is not evidence of a successful Linux run.

## macOS SDK selection

If the compiler and default SDK are incompatible, explicitly select a compatible
installed SDK for the local build. Replace the example path below with an actual
SDK path on your machine:

```sh
cmake -S . -B build-local -DBUILD_TESTING=ON -DCMAKE_OSX_SYSROOT="/path/to/compatible/MacOSX.sdk"
cmake --build build-local
(cd build-local && ctest --output-on-failure)
```

Machine-specific versions, paths, and diagnostic logs stay in the host's task
record. A local SDK selection is not a project requirement for other platforms.

## Responsibilities

- **Core:** operation identity, execution admission, generation fencing,
  settlement state, recovery, and layered operation results.
- **Adapters:** enforce authority at declared submission points and report native
  execution and settlement evidence.
- **Existing robot runtimes:** retain communication, scheduling, model execution,
  device I/O, continuous control, and native safety protection.
- **Agents and applications:** choose goals, skills, and task recovery strategies.

The core is host-driven and payload-independent. The first version does not add a
general scheduler, model-serving engine, robot controller, or agent framework.

## Implementation sequence

M1 delivers one normal sample operation through a native worker to a managed
result sink, with correlated events and a layered receipt. The minimal Core and
deterministic adapter are delivered together. M2 adds
failure and cancellation, M3 adds replacement and recovery, and M4 maps the same
behavior to ROS 2 Humble. Each increment includes its regression tests. A separate
Robot Agent can start using the available interfaces before all milestones finish.
M1 and M2a have passed macOS and Ubuntu validation. M2b cancellation and M2c
expiry now also have macOS and Ubuntu evidence for their implemented scope.
M3a now exercises operation replacement in the same host/binding; provider
rebinding, restart recovery and M4 ROS integration remain planned.

For project constraints and the engineering workflow entry, start with
[repository instructions](AGENTS.md). The accepted boundary and M1–M4 sequence
are in [Design](docs/DESIGN.md); runnable checks and remaining environment gaps
are in [Testing](docs/TESTING.md).
