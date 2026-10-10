<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/brand/kingfisher-dark.svg">
    <img src="docs/assets/brand/kingfisher-light.svg" width="128" height="128" alt="Robot Harness kingfisher">
  </picture>
</p>
<h1 align="center">Robot Harness</h1>
<p align="center"><strong>A harness for embodied intelligence.</strong><br>From intent to action. Back with evidence.</p>
<p align="center">
  <a href="https://xiao-yang25.github.io/robot-harness/">Website</a> ·
  <a href="#build">Quick start</a> ·
  <a href="docs/README.md">Documentation</a> ·
  <a href="examples/README.md">Examples</a> ·
  <a href="CONTRIBUTING.md">Contribute</a>
</p>
<p align="center">
  <a href="https://github.com/xiao-yang25/robot-harness/actions/workflows/core.yml"><img src="https://github.com/xiao-yang25/robot-harness/actions/workflows/core.yml/badge.svg?branch=master" alt="Core on Ubuntu"></a>
  <a href="https://github.com/xiao-yang25/robot-harness/actions/workflows/ros2.yml"><img src="https://github.com/xiao-yang25/robot-harness/actions/workflows/ros2.yml/badge.svg?branch=master" alt="Nav2 on Humble"></a>
</p>

Robot Harness is building the execution foundation between **agents, robot skills
and the physical world**. The aim is to make actions observable, interruptible and
composable—and make their outcomes useful for the next decision and future improvement.

Its experimental C++17 Core starts with three questions: **May this action run?
May this result be used? Is the previous work settled enough to proceed?**
Applications choose goals; robot stacks retain control and device protection.

## See it move

**Observe → decide → act → observe again.** Robot Agent makes three actual model
proposals to visit A, visit B and assess completion through Harness and Nav2.

https://github.com/user-attachments/assets/91a164f6-653c-41e2-ae12-b494489ea3b4

*Local M6 candidate · Gazebo simulation · 70.9 seconds at original speed.*
A releases before B starts; B settlement remains pending at the end.

[Shared coordination: navigation, ACT manipulation and decision withdrawal](https://xiao-yang25.github.io/robot-harness/#coordination-demos)
· [In-motion instructions: stop or redirect to B](https://xiao-yang25.github.io/robot-harness/#revision-demos)
· [Checkpoint: finish at A or continue to B](https://xiao-yang25.github.io/robot-harness/#checkpoint-demos)
· [More demos: manipulation, cancellation, handoff and Owner loss](https://xiao-yang25.github.io/robot-harness/#more-demos)
· [Recording notes and same-run evidence](docs/assets/demo/README.md)

## Build

Requires a C++17 compiler and CMake 3.16+. No model or robot is needed for this first run.

```sh
git clone https://github.com/xiao-yang25/robot-harness.git
cd robot-harness
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
./build/robot_harness_normal_execution
```

Expect sums **29** and **61**, with accepted results and settled work.
Continue with [two-step tasks](examples/README.md#local-compute-and-two-step-tasks),
[cancellation](examples/README.md#failure-cancellation-and-deadlines), or the
[Ubuntu container](docs/TESTING.md#ubuntu-development-container).
[Build troubleshooting](examples/README.md#if-a-command-fails).
For robot motion, follow the [Humble/Gazebo simulation tutorial](integrations/ros2/simulation/README.md),
or use the [local live viewer](integrations/ros2/simulation/README.md#watch-a-live-run)
to watch navigation, cancellation and replacement.

## Current scope

| Available today | Entry point |
|---|---|
| Execution authority, guarded results, cancellation and deadlines | [C++ Core](examples/installed_core/README.md) |
| Shared task execution with ALOHA/ACT and Nav2 consumers | [Python Runtime](bindings/python/README.md) |
| Simulated navigation, cancellation, replacement and bounded loss isolation | [Humble/Gazebo](integrations/ros2/simulation/README.md) |
| Existing-agent tools and embodied business applications | [MCP](integrations/mcp/README.md) · [Robot Agent](https://github.com/xiao-yang25/robot-agent) |

Run the [navigation business tutorial](https://github.com/xiao-yang25/robot-agent/blob/master/examples/navigation/README.md)
with controlled or host model proposals. The [M6 delivery scope](docs/TESTING.md#m6-closeout)
covers the two-consumer slice and its recorded qualifications.

**Next:** broaden task/skill consumers and qualify a third backend combination. Broader skills,
learning and self-improvement remain future directions.

**Early-stage software, validated in limited simulation profiles.** No physical-robot
integration or hard stop guarantee yet. Recovery requires a surviving,
polled Owner; the [optional finite motion profile](integrations/ros2/nav2_session/README.md#explicit-finite-motion-profile)
can expire outlet permission after Owner loss; cancellation does not by itself prove native stop or settlement.
See [validation and supported combinations](docs/TESTING.md#m6b-delivery-ci-coverage)
and [architecture and limits](docs/DESIGN.md#product-boundary).

## Interfaces and compatibility

C++17 · ROS-independent Core · [CMake installation](examples/installed_core/README.md)
and source-tree adapters/examples. APIs are evolving; there is
no stable SDK/API/ABI or published release yet. Start with the
[architecture guide](docs/README.md#understand-the-architecture) or
[backend integration](docs/README.md#connect-a-backend).

## Contributing

[Report a bug](https://github.com/xiao-yang25/robot-harness/issues/new?template=bug_report.md),
[discuss an integration](https://github.com/xiao-yang25/robot-harness/issues/new?template=proposal.md),
or read [CONTRIBUTING](CONTRIBUTING.md). The [kingfisher](docs/assets/brand/README.md)
is our preview identity and may evolve.

## License

Except for third-party material with its own notices, Robot Harness is licensed
under [MIT](LICENSE-MIT) OR [Apache-2.0](LICENSE-APACHE), at your option.
See the [license declaration](LICENSE),
[contribution terms](CONTRIBUTING.md#licensing-status) and
[simulation third-party notices](integrations/ros2/simulation/THIRD_PARTY_NOTICES.md).
