# API entry points

Start with [installing Core](../examples/installed_core/README.md). The installed
CMake target is `RobotHarness::core`; the public header is
[`robot_harness/authority_gate.hpp`](../include/robot_harness/authority_gate.hpp).
These are evolving C++17 interfaces, not a stable SDK or a physical safety layer.

## Core: authority and evidence

`AuthorityGate` is a passive state machine driven by one host-owned logical
writer. The host supplies observations and monotonic time. Core neither launches
a backend nor invents missing evidence.

| Task | Entry points | Contract to read |
|---|---|---|
| Establish readiness | `observe_startup`, `startup_status` | [Startup and ownership](DESIGN.md#execution-ownership) |
| Admit and dispatch | `admit`, `claim_dispatch` | [Commands and evidence](DESIGN.md#commands-evidence-and-receipts) |
| Cancel or enforce a deadline | `request_cancel`, `observe_time` | [Cancellation and deadlines](../examples/README.md#failure-cancellation-and-deadlines) |
| Record native outcomes and result disposition | `observe_native`, `can_deliver_result`, `observe_output_accepted`, `observe_output_not_delivered` | [Results and recovery](DESIGN.md#results-and-recovery) |
| Record closure and inspect progress | `observe_settlement`, `receipt` | [Execution ownership](DESIGN.md#execution-ownership) |
| Replace a provider | `withdraw_binding`, `begin_rebind`, `observe_rebind_readiness`, `commit_rebind` | [Replacement and rebinding](../examples/README.md#replacement-and-rebinding) |

`OperationAuthority` identifies an admitted operation and its binding.
`OperationReceipt` keeps native outcome, output disposition and settlement
separate. A receipt read does not advance a deadline, perform cleanup or grant
fresh permission. The [header](../include/robot_harness/authority_gate.hpp) is the
signature reference; [behavioral checks](../tests/authority_gate_tests.cpp) provide
concrete examples. This page is a reading map, not a second implementation contract
or exhaustive generated symbol reference.

## Source-tree hosts and examples

Python callers can use the optional experimental
[`Session`](../bindings/python/README.md) package for the deterministic local
fixture or the optional [MuJoCo trial](../integrations/mujoco/README.md). It has a
separate opt-in build/install path and does not
change the installed C++ Core. Its private `_core` wrapper is not a public binding
for all Core APIs. The ACT worker is supplied by the consumer. Existing agents
can use the optional [local MCP tools](../integrations/mcp/README.md); embodied
business applications may consume Session directly. The MCP package is installed
as source with the optional Python build, with separately installed SDK dependencies.
Neither path brings business task logic or model dependencies into Core.

| Interface | Purpose | Availability |
|---|---|---|
| [`SampleExecutionHost`](../include/robot_harness/sample_execution.hpp) | Deterministic examples for learning the execution flow | Source tree; not installed |
| [`ComputeExecutionHost`](../include/robot_harness/compute_execution.hpp) | Local worker process integration | Source tree; worker deployment required |
| [Nav2 observation and settlement examples](../integrations/ros2/nav2_observation/README.md) | Inspect the optional ROS boundary | Separate Ubuntu/Humble build |

Read [backend integration](README.md#connect-a-backend) before connecting a real
backend. The first independent consumer checks packaging and refusal without
readiness; it is not a complete adapter. Private recovery and ROS fixtures are
not installed APIs. Version, licensing and deployment limits remain in the
[project scope](../README.md#interfaces-and-compatibility).
