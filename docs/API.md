# API entry points

Start with [installing Core](../examples/installed_core/README.md). The installed
CMake target is `RobotHarness::core`; the public header is
[`robot_harness/authority_gate.hpp`](../include/robot_harness/authority_gate.hpp).
These are evolving C++17 interfaces, not a stable SDK or a physical safety layer.

Use the [component classification](DESIGN.md#user-components-task-examples-and-internal-mechanisms)
to distinguish reusable user entry points, profile-specific integrations,
application examples and private mechanisms. Installing Python source does not
make its underscored modules a public extension API.

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
fixture or the optional [MuJoCo profiles](../integrations/mujoco/README.md). It has a
separate opt-in build/install path and does not
change the installed C++ Core. Its private `_core` wrapper is not a public binding
for all Core APIs. The ACT worker is supplied by the consumer. Existing agents
can use the optional [local MCP tools](../integrations/mcp/README.md); embodied
business applications may consume Session directly. The MCP package is installed
as source with the optional Python build, with separately installed SDK dependencies.
Neither path brings business task logic or model dependencies into Core.
The [opt-in continuous skills](../bindings/python/README.md#continuous-skills)
add explicit skill selection and an expected observation reference. The default
fixture/trial calls and the MCP trial configuration retain their behavior.

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

## Navigation requests to a prepared owner

The optional Python install now includes `robot_harness.NavigationSession`. It
connects to a private Unix endpoint supplied by a prepared, trusted navigation
owner. It does not start ROS, launch a simulator or own the remote process. The
existing stepped `Session` and MCP tools retain their interfaces.

| Call | Meaning |
| --- | --- |
| `capabilities()` | Registered sites, fixed map/frame/profile and current availability |
| `observe()` | Permitted owner observations and a bounded issued reference |
| `submit(id, site=..., expected_observation=...)` | Request a registered site using the reference, with a bounded deadline; an identical retry reads the retained decision |
| `status(id)` | Retained request, native UUID, scope/generation, output and Core settlement |
| `cancel(id)` | Revoke that current request and schedule driver closure; old cancellation cannot select newer work |
| `close()` | Request owner shutdown and close the local channel; return intent/closure unknown, without claiming owner reaping or robot stop |

Pass `observe()['reference']` as `expected_observation`. References are owner-issued,
session/epoch/map/frame bound, valid for at most one second and retained up to 64
observations. Current invalid input still refuses admission. New observations evict
the oldest reference; already retained requests remain queryable and replayable.
Reusing an ID with different arguments fails. Capacity is 64 retained requests;
unsettled active work blocks admission. A request timeout is an unknown outcome:
query that ID before resubmitting and retain the original proposal for an exact retry.
The default deadline is 147000 ms; a prepared profile may advertise a lower maximum.

The [caller example](../examples/navigation_requests.py) demonstrates fixed A→B
requests for a prepared owner that registers those two sites; A must settle before
B, while final B may stay pending. Owner-side `robot_harness.navigation.NavigationRequests` borrows the shared
coordinator, fixed sites and trusted driver callbacks. Its `command(message)`
returns a result; the owner transports RPC replies, continuously progresses native
work/Core time, and handles EOF by requesting the same shutdown. Stop scheduling
must return promptly; failure must reach the owner's closure path. Only the driver
can provide native and physical closure facts. This is not a plugin-loading API.

Local installed-client normal A→B consumption is verified with the optional
[installed bounded owner](../integrations/ros2/nav2_session/README.md). The fixed
simulation profile and standalone host-launcher normal route are locally verified;
finite cancellation during B rotation and disconnection during B translation
have local physical protection evidence. Abort still reports stop unknown and
settlement pending. Installed navigation business-Agent normal consumption has
[local candidate evidence](TESTING.md#m6b-navigation-business-agent); paired public
version delivery remains pending.
See [the request boundary](RUNTIME_SLICE.md#navigation-request-entry-increment)
and [validation](TESTING.md#navigation-request-entry).
