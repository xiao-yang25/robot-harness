# Robot Harness

A runtime for reliable robot execution, interruption, handoff, and recovery.

Robot Harness is a Physical AI Harness/Runtime project for integrating robot
capabilities with agents, behavior trees, and ordinary applications. Its first
implementation is a small, embeddable C++17 core for execution authority and
truthful operation feedback.

## Status

**M2a: failure closure implemented, reviewed, and validated on macOS and Ubuntu.**
The sample now distinguishes non-submission, native rejection, native failure,
and result delivery failure. New work is allowed only after the relevant outcome,
output disposition and cleanup evidence are complete. Local macOS tests, sanitizer
checks and independent code review have passed.
The [M2a Ubuntu run](https://github.com/xiao-yang25/robot-harness/actions/runs/34864391097)
for `16b2888` passed all five tests in both normal and ASan/UBSan builds.
See [Testing](docs/TESTING.md) for evidence and limits.

The declared effect is acceptance of a sample result in a cooperative in-process
fixture. Cancellation, deadlines, provider replacement, restart recovery, ROS
integration, and physical safety remain later work.

Earlier research experiments support the shared-core architecture; they do not
establish production performance, physical safety, or end-to-end task success.

## Build

Requires CMake 3.16 or newer and a C++17 compiler. ROS is not required for the core.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
(cd build && ctest --output-on-failure)
```

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

## Code map

| Entry | Role |
|---|---|
| [Core interface](include/robot_harness/authority_gate.hpp) and [implementation](src/core.cpp) | Operation identity, admission, evidence validation, and current receipt; no sample payload |
| [Sample host interface](include/robot_harness/sample_execution.hpp) and [implementation](src/sample_execution.cpp) | Host, deterministic adapter/worker, callback staging, and managed result storage |
| [Application example](examples/normal_execution.cpp) | Initializes the host and runs A then B in both completion modes |
| [Core tests](tests/authority_gate_tests.cpp), [normal fixture tests](tests/sample_execution_tests.cpp) and [M2a tests](tests/m2a_failure_tests.cpp) | Evidence boundaries, actual normal/failure execution, delivery, cleanup and shutdown |

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
M1 is complete and M2a is the current failure-closure slice. M2b cancellation,
M2c expiry and M3–M4 remain planned. Current results do not establish those later
capabilities.

For project constraints and the engineering workflow entry, start with
[repository instructions](AGENTS.md). The accepted boundary and M1–M4 sequence
are in [Design](docs/DESIGN.md); runnable checks and remaining environment gaps
are in [Testing](docs/TESTING.md).
