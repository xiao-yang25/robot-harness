# Robot Harness

A runtime for reliable robot execution, interruption, handoff, and recovery.

Robot Harness is a Physical AI Harness/Runtime project for integrating robot
capabilities with agents, behavior trees, and ordinary applications. Its first
implementation is a small, embeddable C++17 core for execution authority and
truthful operation feedback.

## Status

**M0: build skeleton.** The core is currently a placeholder. Execution authority,
cancellation, stale-output rejection, settlement, and recovery are planned
behavior, not implemented guarantees.

Earlier research experiments support the shared-core architecture; they do not
establish production performance, physical safety, or end-to-end task success.

## Build

Requires CMake 3.16 or newer and a C++17 compiler. ROS is not required for the core.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
(cd build && ctest --output-on-failure)
```

The smoke test links and calls the placeholder Core library. It does not test
robot execution semantics.

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

## Next implementation

M1 delivers one normal sample operation through a native worker to a managed
result sink, with correlated events and a layered receipt. The minimal Core and
deterministic adapter are delivered together. M2 adds
failure and cancellation, M3 adds replacement and recovery, and M4 maps the same
behavior to ROS 2 Humble. Each increment includes its regression tests. A separate
Robot Agent can start using the available interfaces before all milestones finish.
These are planned milestones; the current implementation remains M0.

For project constraints and the engineering workflow entry, start with
[repository instructions](AGENTS.md). The accepted boundary and M1–M4 sequence
are in [Design](docs/DESIGN.md); runnable checks and remaining environment gaps
are in [Testing](docs/TESTING.md).
