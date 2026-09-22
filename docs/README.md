# Robot Harness documentation

Choose a path based on what you want to do. Current capabilities are summarized
in the [project README](../README.md#current-scope); the documents below retain
the detailed contracts and evidence.

## Run your first example

1. [Build the project](../README.md#build) and run normal execution.
2. Follow [local compute and two-step tasks](../examples/README.md#local-compute-and-two-step-tasks)
   to run a real worker process and a dependent task.
3. Explore [cancellation and deadlines](../examples/README.md#failure-cancellation-and-deadlines),
   then [replacement and rebinding](../examples/README.md#replacement-and-rebinding).

These software paths need no model or robot. For a failed command, start with
[example troubleshooting](../examples/README.md#if-a-command-fails).

## Understand the architecture

Read the design in this order:

1. [Product boundary](DESIGN.md#product-boundary): responsibilities of callers,
   Harness, ROS and native control.
2. [Execution ownership](DESIGN.md#execution-ownership): who retains work and
   observes its completion.
3. [Commands, evidence and receipts](DESIGN.md#commands-evidence-and-receipts):
   distinguish intent, native facts, result delivery and settlement.
4. [Source modules and public headers](DESIGN.md#source-modules-and-public-headers):
   find the implementation after understanding the boundary.

[Results and recovery](DESIGN.md#results-and-recovery) and the
[Linux recovery example](../examples/README.md#host-recovery-on-linux) explain the
limited surviving-Owner prototype. Recovery does not restore a missing result
or survive Owner/machine restart.

## Connect a backend

Start with the existing [local compute integration contract](DESIGN.md#local-compute-integration-contract)
and its [usage and verification](TESTING.md#local-compute-usage-and-verification).
Use it to identify a backend's submission, result and cleanup boundaries, rather
than assuming native completion alone permits another task.

For ROS 2, read the [navigation integration scope](ROS2_INTEGRATION.md), then the
[optional Humble example](../integrations/ros2/nav2_observation/README.md).
Its two binaries have different settlement contracts. Compilation is available
from this repository; full simulation execution still needs the research native
fixture. No general ROS adapter SDK or physical-robot integration guide is
available yet. Discuss a concrete backend through an
[integration proposal](https://github.com/xiao-yang25/robot-harness/issues/new?template=proposal.md).

## Verify or contribute a change

- [Contributing](../CONTRIBUTING.md): participation, licensing status and patch workflow.
- [Testing](TESTING.md#current-validation-baseline): tested revisions, environments,
  manual observations and CI scope.
- [C++ style](CODING_STYLE.md): names, headers and formatting.
- [PR review and evidence](TESTING.md#pr-review-and-evidence): record what was
  checked and which findings remain.
- [Integrity standard](standards/MINIMAL_INTEGRITY_STANDARD.md): proportional
  evidence checks and preservation of historical results.

[AGENTS.md](../AGENTS.md) contains the agent-specific project entry. It complements
the contributor documents; personal agent tools are not required to participate.

## Source map

| Location | Contents |
|---|---|
| [include/robot_harness](../include/robot_harness) | Current caller-facing headers |
| [src/core](../src/core) | ROS- and payload-independent Core |
| [src/compute](../src/compute), [src/sample](../src/sample) | Native process adapter and deterministic fixture |
| [examples](../examples/README.md) | Runnable callers, task controller and recovery demonstrations |
| [integrations/ros2](../integrations/ros2) | Optional navigation builds and experimental fixture messages |
| [tests](../tests) | Software regressions registered with CTest |

For planned behavior, use the [implementation sequence](DESIGN.md#implementation-sequence)
and [M4 delivery increments](ROS2_INTEGRATION.md#delivery-increments). A planned
check or interface is not evidence that it has been implemented.
