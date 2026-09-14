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
ctest --test-dir build --output-on-failure
```

The smoke test checks the initial build/link setup only. It does not test robot
execution semantics.

## Development host build note

On the development host, the default macOS 27 SDK failed to link because the
installed linker did not recognize its `arm64e.x1` entries. M0 built and passed
its smoke test with the installed macOS 26.5 SDK:

```sh
cmake -S . -B build-macos-sdk26 -DBUILD_TESTING=ON -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
cmake --build build-macos-sdk26
ctest --test-dir build-macos-sdk26 --output-on-failure
```

This is a host-specific workaround, not a project SDK requirement.

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

M1 implements the passive authority state machine. A deterministic native adapter
then exercises interruption, late output, settlement, and recovery. A ROS 2
Humble adapter follows, while a separate Robot Agent project can begin consuming
the minimal interfaces as soon as they are usable.

See [design and implementation scope](docs/DESIGN.md) and
[repository instructions](AGENTS.md).
