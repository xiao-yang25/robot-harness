# Robot Harness

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/brand/kingfisher-dark.svg">
  <img src="docs/assets/brand/kingfisher-light.svg" width="112" height="112" alt="Robot Harness kingfisher preview mark">
</picture>

[Kingfisher preview identity](docs/assets/brand/README.md) · In trial use; may change.

**Execution authority and feedback for robot applications.**

[![Core on Ubuntu](https://github.com/xiao-yang25/robot-harness/actions/workflows/core.yml/badge.svg?branch=master)](https://github.com/xiao-yang25/robot-harness/actions/workflows/core.yml)
[![Optional Nav2 on Humble](https://github.com/xiao-yang25/robot-harness/actions/workflows/ros2.yml/badge.svg?branch=master)](https://github.com/xiao-yang25/robot-harness/actions/workflows/ros2.yml)

Robot Harness is an experimental C++17 Physical AI Harness/Runtime. It tracks
when work may run, when a result may be used, and when another operation may
begin. Applications, behavior trees and agents choose goals; the Core and its
adapters keep execution permission separate from native outcomes and cleanup.

[Build and run](#build) · [Documentation](docs/README.md) ·
[Examples](examples/README.md) · [ROS integration](docs/ROS2_INTEGRATION.md) ·
[Contributing](CONTRIBUTING.md)

The source tree includes software execution examples, a local process adapter
and optional Ubuntu 22.04 / ROS 2 Humble navigation examples. The sequential
Nav2 experiment now settles A through Core before admitting and executing B.
It requires a separate native simulation fixture; an end-to-end simulator package
and physical robot integration are not available yet.

## One task, two steps

The runnable local-compute example asks a worker to sum three squared terms,
accepts `5`, and derives a second request of six terms, producing `55`:

```text
Request 3 terms → accept result 5 → observe native work settled
                                         ↓
                              Request 6 terms → result 55
```

The next operation waits for both the result and settlement. If work is cancelled,
its acknowledgement does not prove it stopped; even eventual native success
cannot authorize a result whose permission was revoked. The
[examples](examples/README.md) make these distinctions observable without a
model, ROS installation or robot.

## Build

Use Git, CMake 3.16 or newer, a working C++17 compiler and Make or Ninja.
On macOS, use a compatible Xcode Command Line Tools/SDK pair; on Ubuntu, use a
C++ development toolchain. The [licensing status](#license) applies to this checkout.

```sh
git clone https://github.com/xiao-yang25/robot-harness.git
cd robot-harness
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
./build/robot_harness_normal_execution
```

The final command reports sums `29` and `61` in two completion modes, with native
success, accepted output and settlement. Its application/domain verdict remains
unassessed. This deterministic example exits nonzero if its checks fail.

To run the two-step story with real child processes on macOS or Linux:

```sh
./build/robot_harness_two_step_compute normal "$(pwd)/build/robot_harness_compute_worker"
```

Expect first result `5`, second request `6` and final result `55`.
See the [example guide](examples/README.md#local-compute-and-two-step-tasks) for
output interpretation and goal revision/recovery variants.

Linux registers 57 tests; macOS registers 47 because the recovery prototype is
Linux-only. Use the [Ubuntu container recipe](docs/TESTING.md#ubuntu-development-container)
for Linux checks on a Mac. For compiler errors, see
[macOS SDK selection](docs/TESTING.md#macos-sdk-selection) and
[example troubleshooting](examples/README.md#if-a-command-fails).

## Current scope

| Path | Available behavior | Boundary |
|---|---|---|
| Core and deterministic adapter | Normal execution, failure, cancellation, deadlines and replacement | Controlled software scenarios; no stop-time guarantee |
| Local process adapter and task examples | Real child execution/cleanup, provider rebinding and dependent tasks | macOS/Linux; caller supplies goals and retry policy |
| Linux Host recovery prototype | A surviving Owner retains native work across Host restart | Owner must stay alive and be polled; no durable task or Owner/machine restart recovery |
| Optional Nav2 observation | One admitted goal, correlated feedback, guarded result and finite motion observation | Receipt stays pending; second admission refused |
| Optional sequential Nav2 settlement | Same Owner closes A, verifies B readiness, settles A and admits/navigates B | Trusted exclusive simulation fixture; B remains pending without a C context |

The [validation guide](docs/TESTING.md#current-validation-baseline) links tested
revisions and environments. Core CI covers software behavior; the Humble job
compiles the optional examples and checks local predicates. Neither workflow
runs Gazebo or establishes physical safety. Actual simulation evidence and its
limits are recorded [separately](docs/TESTING.md#optional-sequential-nav2-settlement).

## Where it fits

| Component | Responsibility |
|---|---|
| Application, behavior tree or Agent | Goals, sequencing, interpretation and retry policy |
| Robot Harness Core + adapter | Execution authority, native observations, guarded results and declared settlement |
| ROS, controller or device runtime | Communication, scheduling, native execution and device protection |

Core is host-driven and ROS-independent. Adapters check authority at their
submission and result boundaries. Packaged Agent integrations and reusable
behavior-tree/ROS Host interfaces remain future work. See the accepted
[product boundary](docs/DESIGN.md#product-boundary).

## Interfaces and compatibility

This is a source-tree project with no stable API/ABI commitment, installed SDK,
package export or supported `find_package` workflow. CMake version `0.1.0` does
not designate a published release. Current caller headers live under
[include/robot_harness](include/robot_harness); private protocol and adapter
headers are implementation details.

Within this build, `robot_harness_core` provides the payload-independent Core,
`robot_harness_sample_fixture` the deterministic host, and `robot_harness_compute`
the local process adapter. Task-controller and recovery-bridge code supports the
examples. Follow the [architecture reading path](docs/README.md#understand-the-architecture)
before extending these boundaries.

## Next steps

M4 ROS integration is in progress. The next functional increment covers
movement-time cancellation, replacement and loss/unknown behavior in simulation.
Reproducible external-user simulation packaging, a complete demonstration and
integration tutorials follow the validated behavior. See the
[delivery increments](docs/ROS2_INTEGRATION.md#delivery-increments).

Documentation and project presentation ship in separate increments alongside
functional work. Formal releases, compatibility policies and ecosystem projects
will grow from actual integration experience.

## Contributing

[Report a bug](https://github.com/xiao-yang25/robot-harness/issues/new?template=bug_report.md),
[discuss an integration](https://github.com/xiao-yang25/robot-harness/issues/new?template=proposal.md),
or start with the [contribution guide](CONTRIBUTING.md). Include the revision,
environment and a minimal reproduction. License and patch contribution terms
remain pending; confirm them with the maintainer before submitting patches.

## License

A project license has not yet been selected, and the repository has no `LICENSE`
file. Source visibility and the CMake version do not grant an open-source license.
See [licensing status](CONTRIBUTING.md#licensing-status).
