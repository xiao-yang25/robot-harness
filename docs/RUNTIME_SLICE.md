# First Runtime / Agent slice

The optional [Python session](../bindings/python/README.md) implements both the
deterministic fixture and the [MuJoCo backend](../integrations/mujoco/README.md).
A research ACT worker now supplies real candidates to the same Core-backed host;
five original seeds match the saved native trajectories exactly. An existing Codex task consumer has completed two real local trials through a
research MCP bridge. The optional [MCP module](../integrations/mcp/README.md) now packages
the local tool path. The first external
[Robot Agent application](https://github.com/xiao-yang25/robot-agent/pull/1) owns
visual task decisions, budgets and a candidate worker, with a pinned Mac consumer
setup. That first delivery is under review; clean/Linux reproduction and broader
task/failure qualification remain separate work. This page owns the
implementation design; [Testing](TESTING.md#first-runtime-slice) owns verification
scope. Core's authority semantics remain unchanged.

The private protocol is now version2, with bounded JSON metadata and a separate
RGB byte payload. Version1 was unpublished and has no mixed-version support.
Concrete backend selection is a trusted local `Session(mujoco=...)` configuration;
there is no plugin registry or arbitrary backend factory. The reusable optional
backend is CMake-installed beside the session; the consumer supplies the model
worker and dependencies. Observation is latest-state pull, not a pushed video feed.
The following design includes reusable Agent integration and broader fault qualification.

## User-visible result

A caller opens one session, discovers the available cube-transfer trial, starts
it, observes real camera/joint feedback and progress, and receives separate native,
execution and task results. After explicit simulator reset, the same session and
loaded policy can run a second trial. A deterministic caller and a separate Agent
consume the same interface. Starting another task must not restart the entire stack.

The first available skill is `aloha.transfer_cube_trial`: attempt the pretrained
ACT task from its declared reset distribution, at most400 native steps. It is not
a language-conditioned general manipulation policy or a claim of complete handoff.
The qualified upstream environment terminates when the left finger touches the
cube off the table; observed runs still had right-gripper contact at termination.
The result must say `native_condition_reached`, `step_limit`, or an explicit error,
and report full release/stable holding as `unassessed`. A caller asking for a
verified complete handoff must receive the unsupported/unverified distinction,
not have its goal silently replaced by this trial.

This remains a stepping simulator profile. Physics advances only when the host
calls `env.step`; while waiting for inference it is paused. Host control progress
must continue during that wait. No physical stop deadline, continuously running
robot, stable grasp, force control or real-time scheduling is claimed. Establishing
full handoff requires a separate explicit task predicate and post-contact execution
experiment; it cannot be obtained by relabeling native reward4.

## Components and ownership

| Component | Owns | Does not own |
|---|---|---|
| Caller / separate Robot Agent | Goal, model/context, skill choice, interpretation and task-level follow-up | Authority, simulator access, model-generated raw action submission |
| Python session host, in Robot Harness | Single event loop; capability binding; actual C++ Core instance; backend and policy-worker lifecycle; admitted operation; candidate action queue; feedback and result delivery | Prompts, task strategy, policy weights or a second authority state machine |
| MuJoCo backend, in Robot Harness | Environment creation/reset/step/observation and simulator closure facts | Task planning, inference or generic physical safety |
| ACT worker, supplied by the skill/Agent side | Pinned model, pre/post-processing, one observation-to-chunk computation | Environment handle, `env.step`, execution authority or an action queue that can drive effects |
| Private Python extension | Type/lifetime conversion to the existing C++ `AuthorityGate` | New authority rules, robotics payloads or process supervision |

The caller, session host and ACT worker are three local processes for concrete
reasons: caller/LLM delays must not stall host progress; policy/GPU delays must not
stall it either. Use a fresh interpreter for the worker, not fork-after-MPS-init.
The host alone touches Core and MuJoCo. No shared mutable simulator or gate object
crosses processes. Host survival/progress is still required; this is not an
independent supervisor or recovery after host death.

Expose a small Python `Session` client for the Agent and deterministic caller.
Implement its owner loop and backend in an optional Python package. Bridge the
Core with a private CPython extension built against the existing C++ target and
Python development headers. Keep the extension's record conversion internal;
callers use session operations, not fabricated evidence. Core remains independently
installable C++17 without Python, ROS, MuJoCo or model dependencies. No pybind11,
network RPC service, public C ABI or cross-version Python binary ABI promise is
needed for the first bridge. The extension has local build and installed-consumer validation; binary ABI portability
is not promised.

Implemented locations are `bindings/python/` for the extension/package,
`integrations/mujoco/` for the optional backend. The ACT worker, model configuration
and task caller stay on the consumer side. The first worker originated in research
and is now bundled in the external Agent's candidate delivery. Reusable session
mechanics must not import LeRobot or prompt libraries. The candidate worker pins
its matching Harness private protocol; it is not a stable standalone plugin.
Packaging paths may be refined during implementation
without changing these ownership boundaries.

## Session interface and data

### Optional MCP consumer boundary

`integrations/mcp/robot_harness_mcp` adapts this Session for existing tool-using
agents. A single local stdio connection owns one Session client; its independently
progressing host and worker remain the existing Runtime processes. The operator
selects the fixture or ALOHA backend, worker/checkpoint/device and fresh recording
directory before launch. Tools cannot supply executable paths, model configuration,
raw actions or authority. Core and the direct Python client do not import MCP.
The source package is copied/installed with the optional Python build; the SDK
and image encoder are separately installed only by MCP consumers.

Tool calls serialize access to the non-thread-safe Session. A request cancelled
while waiting for that client does not start a new call. Once a synchronous call
starts, cancellation is shielded until it finishes and its result/error is retained;
then a cancellation checkpoint returns control to the SDK without a duplicate
response. MCP request cancellation does not revoke a robot operation: query its
request ID, then explicitly call the task-cancel tool if needed. EOF closes admission
and cleans up owned processes through the existing Session shutdown. A failed close
retains the handle for explicit retry and cannot reopen a replacement session.
Startup failure cannot silently create a replacement either.

The operator bounds tool calls and distinct supported submission IDs. Reserve an
ID before Runtime submission, including refused or uncertain outcomes; repeated
IDs reach Runtime deduplication. Close remains available after either budget is
exhausted. This is cooperating-local-client accounting, not a sandbox or a hard
wall-time/physical-stop guarantee. Actual camera samples become bounded PNG MCP
content with correlated epoch/sequence/age; no evaluator-only cube/contact truth
is exposed. Deterministic fixture observations contain measurements without a
fabricated robot image.

This adapter is deliberately separate from an embodied business Agent. That Agent
owns a concrete business goal, task state, observation interpretation and recovery;
it may consume Session directly and need not use a general-purpose coding agent or
MCP. The ACT worker is an action policy, neither of these task-level consumers.
An in-process business application and a tool consumer are both valid front ends
to the same Runtime. No universal Agent framework, backend plugin registry,
HTTP service or business-agent repository is introduced by the MCP module itself.

The initial client operations are `open`, `capabilities`, `observe`, `submit`,
`status`, `cancel`, `reset` and `close`. These operations are implemented by the experimental `Session` client. `submit` names a registered skill and supported arguments; the host
validates capability/profile/reset preconditions, then lets Core admit or refuse.
A client request ID correlates the response; only Core creates operation authority.
A request whose reply is lost is unknown, not safely resubmittable: query its ID;
the host retains its decision/result for the bounded session. Duplicate identical
IDs return that record, conflicting reuse is rejected. Do not persist a durable
workflow or claim reconnect/restart recovery in this slice.

Use local inherited socket pairs with a versioned, length-prefixed frame and
bounded payloads. JSON carries control metadata; RGB bytes use a separate bounded
payload, not base64 in growing logs. No pickle from the caller. The host uses
nonblocking reads/writes with bounded work per loop, including partial-frame state;
a full consumer buffer must not block stepping, cancellation or deadline checks.
Limit one active operation, one in-flight prediction and one candidate chunk of
at most100 actions. Coalesce preview/progress to the latest snapshot; retain final
results and receipts until consumed or session close, with a declared finite
operation-record limit that refuses new work rather than silently evicting it.
Oversize/malformed required fields fail the channel; optional unknown metadata is
ignored. A peer is a cooperating local component, not a sandboxed adversary.

Every prediction request/reply carries session and operation identity, binding
identity/generation, observation sequence and prediction request ID. The host
checks the entire correlation before accepting a chunk, validates shape/finiteness,
and never clips actions to make a bad response executable. The backend interprets
14 absolute joint/gripper targets using the qualified mapping; the upstream blanket
Box limits do not justify treating every arm joint as normalized to[-1,1].

`observe` returns camera/joint measurements, their sequence and simulation time,
plus host receive time/age and execution phase. Progress means completed step
count and current activity, not invented task percent. Full cube pose/contact
truth stays in evaluator evidence, not in Agent observations. A native terminal
reason is explicitly exposed as limited execution feedback; it is not independent
evidence of full goal completion. Core receipt, task assessment and observation
are separate fields. No task enum is added to Core's `DomainVerdict::kUnassessed`.

## One normal run and a second trial

1. Open initializes the backend and spawns the policy worker. The worker loads the
   exact policy/processor combination and reports actual readiness. The host
   observes idle environment, result sink and adapter readiness before advertising
   the capability. An absent worker or failed reset does not produce ready state.
2. The caller submits the supported trial. Core admits one operation with the
   declared effect domain/profile/work limit. The host associates immutable
   arguments and claims dispatch once for the operation, not once per joint action.
3. From an empty queue the host sends the current RGB/joint observation to the
   worker. The worker calls `predict_action_chunk`, denormalizes and returns up
   to100 candidate actions. Temporal ensembling is disabled for this fixed ACT
   configuration. The host owns the queue; the policy's `select_action` deque is
   not used in the integrated path. Qualification must compare both paths first.
4. On each loop, process control/revocation and wall-clock deadlines before the
   next native submission. Immediately before `env.step`, recheck the operation's
   current Core authority, binding and `supports_execution` for its required
   profile/work budget at the current time, then submit exactly one queued action.
   Invalidated/expired required capability during execution triggers host
   revocation; merely observing a new capability record does not revoke Core.
   There is no asynchronous bulk submission to the backend. Record action and
   post-step observation; preserve best-effort state if step raises after effects.
   Stream bounded feedback independently of the caller's polling frequency.
5. Repeat until native termination,400 steps, cancellation or error. New inference
   occurs only when the previous chunk is consumed; no speculative second chunk.
   While awaiting it, service control and status without advancing this stepped
   simulation. Each chunk is open-loop relative to its initial observation; new
   frames within those100 steps do not imply ACT replans every step.
6. On native terminal, stop scheduling steps, discard the unconsumed queue and
   close this operation's prediction lane. Seal an execution summary with native
   reason and assessed/unassessed task fields. Deliver a successful result payload
   only under Core result authority; otherwise retain diagnostics separately and
   record explicit non-delivery before settlement facts.
7. The settled episode remains visible but cannot execute another trial directly.
   An explicit reset checks no unresolved operation/worker obligations, restores
   the native reset distribution and obtains a new observation epoch. Refresh
   capability evidence only after reset succeeds. A second admitted trial reuses
   the same host and loaded policy; it never consumes actions from the first.

The bounded host result store is the declared result sink. Its actual acceptance,
not a client's socket acknowledgement or fabricated readiness flag, supplies Core
output evidence for a successfully terminated operation. Client retrieval reads
this retained result, or failure diagnostics, alongside the receipt;
it does not release the operation. A sink failure is explicit non-delivery. Cancelled
work still has a readable control receipt and diagnostic failure facts, without
publishing an unauthorized successful task payload. This store is memory-only and
does not survive host exit. Capability availability is withdrawn before closing an
episode; Core settlement alone must not advertise the next reset as ready.
This admission-readiness withdrawal after native termination is not cancellation:
it prevents another dispatch while preserving any still-authorized result delivery.
During active execution, losing a required capability instead revokes the current
operation and must not be treated as this normal terminal/reset transition.
Core currently retains only its current operation receipt. Before admitting the
next operation, the host stores the old final receipt by value with its result;
historical status reads use that immutable copy, not a query for a replaced Core
operation or a second computation of authority. If capacity cannot retain it,
refuse the next admission rather than losing the promised result.

The native identity names the host-owned episode lane, not each GPU inference.
Record native acceptance when that lane accepts the admitted trial, and native
start from actual backend execution. Reward4 terminates that lane successfully
with the explicitly limited result; a400-step limit is native failure with reason
`step_limit`. Core only permits successful native output delivery: for step-limit
failure or cancellation with no successful payload produced, record
`kNoOutputProduced` with an empty result reference. Their correlated execution
summaries/diagnostics remain readable outside that governed successful-output
channel and must never be labeled `output=accepted`. If a success payload was
already produced but authority was revoked before delivery, use the existing
`kAuthorityRevoked` non-delivery path instead. Cancellation becomes native-cancelled only after the lane can no
longer submit and its outstanding obligations are resolved. A transport exception
or timeout alone is not a terminal observation. Preserve pending/unknown state
when the required facts are missing; the Core's evidence requirements still apply.

The host must call Core time observation before checking authority; passive receipt
reads do not enforce expiry. `claim_dispatch` is an operation transition, not a
per-action permission API. Use the existing receipt authority/binding and owner
serialization for individual steps, rather than repurposing result-delivery checks.
One already-entered synchronous native step cannot be undone by a later cancel.
The enforceable cancellation boundary starts when the host processes revocation;
sending a client request alone is not proof that it has been processed.

## Termination, settlement and shutdown

The implemented profile is `mujoco-stepped-episode-v1`, scoped to one host-owned
simulation environment. Its work bound is at most400 submitted environment steps
per trial, not a wall-time stop bound. Full identity checks and a single native
writer fence future steps. Settlement requires the operation lane closed, queue
empty, no native step in flight, no outstanding inference/resource obligation,
and a recorded result disposition. Observation of task success is not required
for settlement; settlement does not establish grasp stability. A frozen simulator
is not a stopped real robot. The capability remains unavailable for new admission
until explicit successful reset even after this episode has settled.

Cancel/expiry first revoke authority and clear host candidates. An in-flight model
call may finish later; its reply is discarded. Do not claim worker resource release
until completion is observed or the process is terminated and reaped. If it hangs,
the host remains responsive, reports pending/unknown closure and permits no next
trial. A bounded worker termination policy is a later verified resource mechanism,
not implied by a timeout number. Native-step stalls still stall this host; no hard
reaction-time claim is made. Error/transport loss revokes and freezes this simulated
path rather than generating a success result or silently retrying actions.

Close and caller disconnect close admission, revoke active work, drain/discard
pending messages, finish or terminate/reap the owned worker according to the
implemented shutdown policy, close recording, and explicitly free MuJoCo physics
contexts (`gym.close()` alone is empty in the qualified version). Partial startup
and repeated close need the same ownership accounting. If required cleanup cannot
be confirmed, return/report an error and do not mark successful closure. Preserve
partial traces and videos. No restart recovery or persistent task replay is promised.

## Delivery stages

The current Runtime/Agent work is organized as M5, with separate delivery stages:

| Stage | Deliverable | Boundary |
|---|---|---|
| M5a | Core-backed session, independent host/worker, concrete ACT/MuJoCo connection and repeated trials | Existing foundation; Linux model/render qualification is separate |
| M5b | Optional MCP module, tutorial, protocol/install checks and an existing Codex consumer | Existing-agent tool integration; no owned embodied business Agent |
| M5c | First business task design: goal, initial conditions, skills, task predicate, observations, state, budget and allowed recovery | Task design and bounded native qualification completed; native-condition success cannot silently replace its goal |
| M5d | Implement that business Agent's normal observation/decision/skill/result loop | Implemented and locally qualified on Mac/MPS seed0; first Agent delivery under PR review, with execution facts and task evaluation distinct |
| M5e | Public consumer/skill preparation, fixed combination versions, fresh installation and actual Linux model/simulation reproduction | Candidate preparation and pinned versions provided; clean-consumer/Linux qualification pending, and fixture CI does not qualify model/render execution |
| M5f | Task-relevant faults, comparable native cost/behavior evidence and bounded M5 closeout | Planned; explicit limitations and negative results remain valid outcomes |

[Testing](TESTING.md#first-runtime-slice) owns actual verification coverage; the
stages are delivery scope, not new authority rules or proof of completion. M5a/b
were delivered independently; the first implemented business application now has
its own candidate delivery and qualification limits.
Packaging, Linux preparation and relevant fault/comparison work can start alongside
the normal application loop; expanding support claims requires their corresponding
evidence. M6 then selects one heterogeneous combination to test reuse. Hardware,
preview preparation and later improvement experiments retain their own prerequisites.
Public design/API, testing, installation examples and supported scope are updated
with each applicable implementation increment; private task history stays outside
this repository. The business application lives in its separate Agent repository;
stage organization alone does not call for more repositories, model services or
simulators.

## Continuous segments and the first business task

The M5c design selects one ALOHA/MuJoCo task: transfer the cube to the left arm,
release the right hand, and maintain the grasp for one simulated second. Task
evaluation requires bilateral left-finger contact without other support, bounded
relative cube/gripper pose change, table clearance, and actual simulation progress.
Missing measurements remain unknown. Native reward and Core settlement retain
their separate meanings. Bounded native qualification supports implementing this
task. The opt-in continuous profile and deterministic caller are now implemented;
the external business Agent also implements the normal visual loop. Runtime
qualification is scoped in [Testing](TESTING.md#continuous-handoff-profile);
application qualification belongs to the
[pinned Agent README](https://github.com/xiao-yang25/robot-agent/blob/7e302440408ddb3290de92d26b8a47d195164815/README.md#acceptance-and-delivery).

The explicitly selected `mujoco-stepped-handoff-v1` profile has two operations:

| Skill | Execution | Completion boundary |
|---|---|---|
| `aloha.transfer_cube_segment` | 400 ACT policy steps in a fresh episode | Returned steps and resolved execution/inference obligations; retain the episode for observation and a follow-up |
| `aloha.hold_current_target` | 50 steps repeating the Host's last validated, submitted target | Returned steps and resolved obligations; require explicit reset before a new task |

Select a skill with `Session.submit(..., skill=..., expected_observation=...)`;
the expected epoch/sequence is compared with the Host's current observation before
admission. This profile requires exactly400/50 steps, with those defaults when
steps are omitted. Existing trial termination, step limits, default Session calls
and explicit-reset behavior stay as documented. The separate profile owns native
dm_control continuation and never steps a terminated legacy trial. Core stays
payload-independent; its binding names the actual `mujoco-host` execution owner,
with one composite segment capability on the same simulator domain.

The implemented business application owns the goal, task stages, budgets and decision
records. It observes RGB/joints, chooses transfer or help, receives a fresh
post-transfer observation, then chooses hold or help before the final observation
and report. That intermediate observation must affect the follow-up decision.
The deterministic example exercises this interface; it does not implement a
model-backed application or make an observation-driven task choice.
Independent simulation evaluation cannot feed contact truth into those decisions
or into hold admission. Admission checks the declared stage, resources, target
provenance and resolved obligations; it still checks operation authority at every
submission. A mistaken visual decision must remain visible in the task outcome.

The initial design permits one transfer and one hold, with no automatic execution
retry. Decisions are tied to task, epoch, stage and observation sequence; stale or
cancelled decisions cannot start a follow-up. Simulation pauses while awaiting a
decision, so rereading the same frame proves no new physical progress. Cancellation
or incomplete execution does not grant hold readiness. Explicit reset prepares a
new episode; it is not physical recovery or a hard bound on blocked native calls.

M5d places the application, model/skill dependencies and task evaluation in an
independent Agent repository, depending on Harness through its public interfaces.
Reusable segment/profile mechanisms belong here. A fixed two-repository version
combination and public preparation instructions precede M5e's fresh Linux/model/render
reproduction. Agent PR1 provides the model-backed application and preparation at
7e30244, pinning Harness3acc9aa. The installed default-worker normal path passed
with a new skill environment and newly downloaded/migrated weights, reusing the
qualified Harness installation. Clean-consumer/Linux reproduction and selected
live failure/comparison cases remain unqualified; the first delivery is under review.
Continuous-profile acceptance is distinct from the earlier native feasibility
probe, business-Agent acceptance and Linux/model/render support.

## Alternatives and next implementation batches

An in-process policy loop is the smallest native baseline, but its measured first
inference pause already makes it unsuitable for host progress responsibilities.
A C++ owner service would keep all scheduling in C++, but introduces an extra
simulator submission round trip and richer protocol before a second consumer
requires it. A Python-owned loop with the existing compiled Core keeps the natural
MuJoCo API local and isolates actual model work. Revisit if measured GIL/native-step
blocking prevents the declared responsiveness. A Python rewrite of Core is rejected
because it would create another implementation of authority/recovery semantics.

The first implementation batch builds the optional bridge and session skeleton,
with a deterministic backend/worker to validate ownership, partial startup, bounded
messages, result correlation, duplicate requests and two operations in one host.
It must use the real C++ Core, not a mock authority implementation. Then connect
this exact MuJoCo/ACT combination and compare normal traces/task outcome, setup
work and overhead against the saved native baseline. Include the real task Agent
in that same normal-closure slice; do not wait for all fault cases or Isaac.

The Agent consumes the capability description and actual observation, submits a
trial when it matches the requested goal, follows progress, and reports the limited
outcome or unsupported goal honestly. First provide a deterministic caller through
the same client, then a live model goal/skill/observation loop. Model/provider,
endpoint availability and call budget must be selected before that live run; no
configured or funded endpoint is presumed. Agent prompts/model dependencies stay
outside Harness. Replay is a development aid, not evidence of a live Agent.

Before expanding support claims, test slow inference, nonempty-queue cancellation,
stale/malformed replies, caller loss, observation invalidation and cleanup failures.
Keep Gazebo/Humble unchanged; Linux and a later concrete Isaac combination need
separate qualification. Build the public interface from real consumer needs and
measured costs, not a generic plugin system or additional approval/hash layers.
