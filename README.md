# Robot Harness

A C++17 runtime for execution authority, interruption, handoff, and recovery.

Robot Harness is a Physical AI Harness/Runtime project for agents, behavior trees,
and ordinary applications. It separates permission to execute from what actually
happened: native acceptance, completion, result delivery, and resource cleanup.
The current implementation runs deterministic examples and bounded local compute
processes. M4 now includes an experimental normal Nav2 navigation observation
in simulation; physical robot integration remains unimplemented.

[Build and run](#build) · [Examples](examples/README.md) ·
[Design](docs/DESIGN.md) · [Testing](docs/TESTING.md) ·
[Contributing](CONTRIBUTING.md)

## Current scope

| Available now | What it demonstrates |
|---|---|
| Host-driven Core and deterministic sample adapter | Normal execution, failures, cancellation and deadlines with separate receipt facts |
| Local compute adapter | Bounded work in a child process, cancellation, exit collection and channel cleanup |
| Operation replacement and live-Host provider rebinding | Conflicting new work waits for old-work settlement; stale results do not acquire new authority |
| Two-step task example | Dependent work, goal changes and uncertain results using the same task controller |
| [Optional Humble/Nav2 observation](integrations/ros2/nav2_observation/README.md) | One Core-admitted simulated navigation goal, correlated feedback/result, protected output and finite post-result motion observation; settlement stays pending and a second goal is refused |
| Private Linux recovery prototype | A surviving Owner retains native work across Host/Core restart; fresh activation permits explicit new work without replaying the interrupted goal |

M3c is merged through [PR #7](https://github.com/xiao-yang25/robot-harness/pull/7).
Its [main-branch Ubuntu CI](https://github.com/xiao-yang25/robot-harness/actions/runs/35453807474)
passed all 57 tests in Debug and ASan/UBSan. Current platforms and evidence are
summarized in [Testing](docs/TESTING.md#current-validation-baseline).

Cancellation acknowledgement does not mean the work has stopped. Completion does
not mean its result was accepted or its resources were released. The recovery
prototype requires the Owner to survive and continue polling; it does not recover
persistent tasks or survive Owner/machine restart. These examples do not establish
physical stopping, hard stop deadlines, or production safety.

## Build

Start with Git, CMake 3.16 or newer, a C++17 compiler, and a build tool such as Make
or Ninja. On macOS, install the Xcode Command Line Tools; on Ubuntu, use a working
C++ development toolchain. ROS, a model, and robot hardware are not required.
The current [licensing status](#license) applies to this source checkout.

```sh
git clone https://github.com/xiao-yang25/robot-harness.git
cd robot-harness
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
./build/robot_harness_normal_execution
```

The final command runs two samples in each of two completion modes. Look for sums
`29` and `61`, with native success, accepted output and completed settlement. The
application/domain verdict remains unassessed. The program exits nonzero if its
checks fail. This is a deterministic software example, not a robot command.

Linux currently registers 57 tests; macOS registers 47 because the recovery
prototype is Linux-only. Use the [Ubuntu container recipe](docs/TESTING.md#ubuntu-development-container)
for Linux checks on a Mac. If a local compiler and SDK do not match, see
[macOS SDK selection](docs/TESTING.md#macos-sdk-selection).

## Choose an example

Run commands from the repository root after building. The
[example guide](examples/README.md) provides commands, expected observations and
source links for each path.

| Start here | Next question to explore |
|---|---|
| [Normal execution](examples/README.md#normal-execution) | How do submission, result delivery and cleanup differ? |
| [Failure, cancellation and deadlines](examples/README.md#failure-cancellation-and-deadlines) | Why can a stopped or expired request still have unfinished native work? |
| [Replacement and rebinding](examples/README.md#replacement-and-rebinding) | When can another operation or provider take over? |
| [Local compute and two-step tasks](examples/README.md#local-compute-and-two-step-tasks) | How does a real child process feed a dependent task? |
| [Host recovery on Linux](examples/README.md#host-recovery-on-linux) | What survives a Host crash, and why is a new goal explicit? |

## Interfaces and compatibility

This is an experimental source-tree project. Headers under
`include/robot_harness/` are its current caller interfaces, with no stable API or
ABI promise. The CMake project version `0.1.0` is not a declaration of a published
release. There is no installed SDK, package export or supported `find_package`
workflow yet.

Within this build, `robot_harness_core` supplies the payload-independent Core;
`robot_harness_sample_fixture` adds the deterministic host; `robot_harness_compute`
adds the local process adapter on macOS/Linux. The task controller and recovery
bridge are example support. Headers under `src/compute/`, including the Linux
recovery protocol, are private implementation details.

Core is host-driven. Adapters enforce authority at their submission boundaries;
applications choose goals and recovery strategy. Existing robot runtimes retain
communication, scheduling, device control and native safety protection. See
[product boundaries](docs/DESIGN.md#product-boundary).

## Repository guide

| Location | Purpose |
|---|---|
| [include/robot_harness](include/robot_harness) | Current caller-facing headers |
| [src/core](src/core), [src/compute](src/compute), [src/sample](src/sample) | Module implementations and private process/protocol details |
| [examples](examples/README.md) | Runnable callers and the example task controller |
| [tests](tests) | Core, adapter and task regressions registered with CTest |
| [Design](docs/DESIGN.md) | Accepted behavior, ownership, module boundaries and milestone scope |
| [Testing](docs/TESTING.md) | Platform evidence, Docker/sanitizer commands, manual checks and review requirements |
| [Coding style](docs/CODING_STYLE.md) | Naming, headers and formatting |
| [Contributing](CONTRIBUTING.md) | Reporting issues, preparing changes and review workflow |
| [AGENTS.md](AGENTS.md) | Agent-specific project constraints and knowledge entry points |

## Next steps

M1/M2 provide normal and failure/cancel/deadline paths. M3 adds replacement,
provider rebinding and the limited Linux recovery path above. **M4 is in progress**
on Ubuntu 22.04 / ROS 2 Humble. Its first optional navigation increment reuses Core
for admission, dispatch and result delivery. Local simulation observations show
actual movement; they do not establish native settlement or safe replacement.
See the [ROS integration status](docs/ROS2_INTEGRATION.md) and
[validation scope](docs/TESTING.md#optional-humble-nav2-observation).

Next, combine navigation-specific native work closure, applied drive stop, fresh
motion and a usable native context in the same Owner, then validate Core
settlement and admitted A to B. Motion-time cancellation, revised goals and loss
cases follow. The existing Core build stays ROS-independent. A
reproducible external-user simulator package and integration tutorial remain
follow-ups, not capabilities supplied by the current C++ example alone.

Adapter integration instructions and packaging will grow from actual integration
feedback. Stable API/version policies and release tooling belong to a later
release-readiness step; there is no release-date commitment.

## License

A project license has not yet been selected, and this repository does not include
a `LICENSE` file. Do not treat source visibility or the CMake version as an
open-source licensing grant. Contribution terms remain pending; see
[Contributing](CONTRIBUTING.md#licensing-status).
