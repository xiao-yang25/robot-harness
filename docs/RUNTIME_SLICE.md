# First Runtime / Agent slice

This page owns the implemented first slice, not a universal task/Skill framework.
See [component roles](DESIGN.md#user-components-task-examples-and-internal-mechanisms)
and [evolution direction](DESIGN.md#evolution-toward-reusable-task-execution)
for the distinction between reusable entries, ALOHA-specific behavior and future
extraction. The current trial/transfer/hold and cancellation contracts remain unchanged.

The optional [Python session](../bindings/python/README.md) implements both the
deterministic fixture and the [MuJoCo backend](../integrations/mujoco/README.md).
A research ACT worker now supplies real candidates to the same Core-backed host;
five original seeds match the saved native trajectories exactly. An existing Codex task consumer has completed two real local trials through a
research MCP bridge. The optional [MCP module](../integrations/mcp/README.md) now packages
the local tool path. The first external
[Robot Agent application](https://github.com/xiao-yang25/robot-agent/pull/1) owns
visual task decisions, budgets and a candidate worker, with a pinned Mac consumer
setup. That first delivery is merged as06cbee4. A separate fresh Linux ARM/CPU
installation passed the deterministic ACT/MuJoCo path and a separate installed
visual business-Agent normal run. That selected consumer scope is now delivered;
broader task/failure qualification remains separate work. This page owns the
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
| M5d | Implement that business Agent's normal observation/decision/skill/result loop | Implemented and locally qualified on Mac/MPS seed0; first Agent delivery merged as06cbee4, with execution facts and task evaluation distinct |
| M5e | Public consumer/skill preparation, fixed combination versions, fresh installation and actual Linux model/simulation reproduction | Selected consumer scope delivered: fresh Linux ARM/CPU deterministic and installed visual normal paths qualified; Agent PR3 merged asf19531f and Harness PR30 asb3d93e7, with merged-tree/mainline-CI/documentation-deployment checks passed. Fixture CI does not qualify model/render execution |
| M5f | Task-relevant faults, comparable native cost/behavior evidence and bounded M5 closeout | Selected scope delivered: cube-disturbance and mixed-caller temporal boundaries, matched native comparison and the new pinned installed combination; [closeout](TESTING.md#m5-closeout) preserves version-specific evidence and remaining limits. No general performance or net engineering-time benefit established |

[Testing](TESTING.md#first-runtime-slice) owns actual verification coverage; the
stages are delivery scope, not new authority rules or proof of completion. M5a/b
were delivered independently; the first implemented business application now has
its own merged delivery and qualification limits.
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
[pinned Agent README](https://github.com/xiao-yang25/robot-agent/blob/45c94dea5ea15aece6175c253cdd6586dd359821/README.md#acceptance-and-delivery).

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
reproduction. Agent PR1 delivers the model-backed application and preparation at
06cbee4, pinning Harness3acc9aa. The installed default-worker normal path passed
with a new skill environment and newly downloaded/migrated weights, reusing the
qualified Harness installation. A later fresh Linux ARM/CPU environment and
Harness build/install passed the deterministic normal path, full recording,
bounded physical evaluation and native process reaping. A separate installed Linux
visual-Agent run then completed three actual decisions and the same bounded task,
with its own recording, physical evaluation and process-exit evidence. The Mac and selected Linux normal deliveries are merged, including the visual
tutorial. Selected task counterexamples, matched native comparison and the later
installed pin are recorded in [M5 closeout](TESTING.md#m5-closeout). Broader fault
coverage, reliability and heterogeneous reuse remain unqualified.
Continuous-profile acceptance is distinct from the earlier native feasibility
probe, business-Agent acceptance and Linux/model/render support.

## Navigation and shared execution increment (design only)

The full ROS navigation Session/Skill SDK remains a design. The prepared-owner
request client implemented below is a smaller public boundary.
The first private coordinator extraction below is implemented in the existing Host.
The bounded research navigation driver below now consumes that same coordinator;
its scope does not change the planned public interface into an available SDK.
The existing Session signatures, ALOHA profiles, direct and MCP consumers remain
the supported interfaces. A separate research caller has qualified normal native
two-stop navigation; that does not qualify continuous Runtime admission or stops.

**Selected boundary.** Add one site-visit Skill over Humble Nav2, alongside the
existing stepped ALOHA Skills. The application chooses A, confirms the visit from
new online feedback, then chooses B. It owns ordering, business retries and help
requests. Nav2 owns planning, local control and Action progression. The navigation
Skill translates a registered site into one `NavigateToPose` goal, associates its
feedback and terminal result, and interprets navigation-specific observations.
The shared Runtime owns admission, dispatch coordination, retained request records,
revocation, bounded progress and closure. It uses the existing C++ Core for authority;
neither the Skill nor the Agent implements another authority state machine.

Keep the first increment to one trusted backend per Session, one active operation
and one configured effect domain. The same coordinator should serve separate ALOHA
and navigation sessions; it need not switch live backends or coordinate a fleet.
Extract only mechanics both actually consume. ALOHA keeps its action queue and
step boundary; navigation keeps its ROS Action client and continuous outlet.
Simulator stepping, ROS callbacks and domain assessment do not become generic
coordinator responsibilities. Blocking model work remains outside its progress loop.

**Candidate consumer shape.** These are design requirements, not callable examples
or a frozen wire schema. Choose exact API changes with the two M6b consumers.

| Operation | Required navigation behavior |
|---|---|
| Discover | Advertise `navigation.visit_site` only for the configured map/site table and qualified continuous profile. Report supported parameters and readiness; registered sites resolve to finite map-frame targets and a heading. Callers cannot replace the map, frame, executable, controller or protection policy |
| Observe | Return permitted localization pose, frame/map identity, clock/observation epoch, sample/receive times, localization quality and sensor health. Include bounded native progress when available. Simulator ground truth remains evaluator-only |
| Submit | Associate an immutable request ID, Skill and `site` parameter with the observation reference used for the decision. Navigation checks epoch/map identity and profile-specific age/quality using current observations; advancing sensor sequences alone do not reject a valid proposal. Recheck readiness before dispatch. Stale proposals are refused, never silently updated into a different goal |
| Status | Retain request-to-Core-operation, native UUID and outlet scope/generation correlation. Report acceptance/dispatch uncertainty, execution or inference wait, native terminal reason, domain assessment, outlet closure, stop observations and resource closure separately. Native remaining distance is an estimate, not a task completion percentage |
| Cancel | Target that request's operation and immutable native/outlet identities. Return receipt of intent separately from applied revocation and closure. Repeated cancellation is idempotent; cancelling an old request must not cancel the current visit |
| Close | Stop admission and resolve owned operations, ROS clients and task producers. Report unresolved obligations rather than replacing the Session or claiming cleanup from channel EOF |

Identical request retries read the retained decision; conflicting reuse fails.
An uncertain submission must be queried by its ID, not submitted under a new ID.
Preserve the existing bounded record store and refuse work when it cannot retain
the previous receipt. Session-wide sensors/localization may remain resident, but
that lifetime is distinct from the visit's native handle and producer obligations.
Do not advertise readiness for B merely because the server is still running.

**Dispatch and late replies.** The owner progresses asynchronous ROS requests
with finite waits while continuing control, expiry and observation checks. Do not
call a blocking navigation convenience method from that loop. Existing example
closure helpers must also be adapted into progressing requests, rather than
copied as synchronous waits inside the shared coordinator. Reserve the native
UUID and producer identity before sending. A lost or delayed goal acknowledgement
after possible transmission means submission is unknown. Keep its outlet closed
after revocation, retain the pending request, and cancel the exact handle if it
arrives later. A timed-out future, cancelled Python wait or rejected late callback
does not prove native non-submission or terminal completion. Only proof that no
native goal/lane was created permits the existing non-submission path, for example
a pre-send failure or a correlated authoritative rejection. Closing an already
opened outlet remains required in either case. Late
feedback may complete that operation's diagnostics; it cannot authorize another
operation, deliver a revoked success payload or reopen its outlet.

**Continuous outlet prerequisite.** The initial profile is exclusive simulation
with a live, progressing owner. Reuse the scoped-drive mechanism as a candidate,
then qualify the actual new path. Bind each controller/smoother producer channel
immutably to the visit's scope/generation before starting it. A relay that stamps
all unscoped traffic with the currently open generation is prohibited: an old
producer would become new authorized work. If the selected native stack cannot
provide this distinction, it must not receive this profile. Initially use separate
task-bound producer contexts; keep shared map/localization resident and measure
startup/closure cost before promising native-stack reuse.

For the bounded two-visit profile, prepare both immutable native contexts while
the outlet is initially sealed, then require native readiness and current input
before the first opening attempt. Initial preparation has a finite budget; it
cannot be re-entered after any motion admission attempt to suppress input expiry
or restore execution authority. This does not prescribe unbounded prewarming or
prove per-visit process reclamation. Other profiles must qualify their own
preparation and resource costs.

The outlet starts closed. A valid Core dispatch and current profile observations
permit opening the matching scope before submitting the native goal. If opening
or dispatch becomes uncertain, retain the obligation to close it. Revocation first
invalidates execution/result authority and schedules closure of that exact outlet;
it also requests native cancellation without waiting for either response to start
the other. Continue processing both paths. Closing clears stored commands and
rejects late commands at the actual wheel-write boundary. A service response says
closure was accepted; a correlated simulation-update acknowledgement says zero
wheel targets were applied. Neither observation alone proves physical rest.

On native success, withdraw new admission and close the visit's outlet/producer
lane without fabricating cancellation or revoking an otherwise valid result.
Evaluate online arrival using relevant post-result localization; report success,
failure or unknown with its source. Deliver successful payloads only through the
existing Core result authority/sink path; failures and unknowns remain diagnostics.
Business arrival, fresh quiet-motion observations, native completion and producer
closure are distinct facts. Before conflicting reuse, require this profile's native
terminal/child-closure observations, outlet-applied acknowledgement and fresh quiet
motion interval. Match the acknowledgement to this scope/generation and its actual
applying simulation update; quiet samples must advance beyond both that update
and the newest odometry already cached when it was received. Replayed or merely
clock-aligned old samples cannot start the window. Keep the aggregate operation unresolved until all required output
and resource obligations are resolved. This increment does not add partial-resource
handoff or reuse the old producer by changing its identity.

Observation expiry, clock reset, localization invalidity or loss of a required
capability closes admission and revokes active work through the same closure path;
it never automatically advances to B. Loss of stop observations remains unknown.
If closure service/acknowledgement or native completion is unavailable, keep the
domain blocked and report the missing fact. An experiment watchdog may remove its
owned container for cleanup; removal is not evidence of a qualified robot stop.
This profile supplies no owner-death protection, hard stopping deadline, controller
lease, persistent recovery or real-device guarantee. Those requirements need a
different supported profile and native protection before admission.

**Verification before integration.** Qualify the actual producer/outlet route,
not only Action success: normal A/close/B; revoke during motion with late old
commands; stale cancel after a later generation opens; required observation loss;
and withheld closure feedback that must block reuse. Include cancellation while
the goal acknowledgement is pending. Use real sensor/physical observations and
correlated native closure facts where claimed. Set quiet thresholds and observation
windows before running; retain failures rather than loosening them. M6b then proves
actual shared coordinator use, retained ALOHA behavior and a live navigation business
Agent. MCP exposure, broader fault comparison and installed navigation delivery
remain separate decisions/work; the existing MCP tools are unchanged.

### First binding increment and shared-coordinator extraction

The first M6b support change is deliberately smaller than a navigation Session.
The private Python bridge now permits an explicit fourth native-identity argument
to its owner-only `Gate.native` call. The existing three-argument call continues
to synthesize `episode-<operation_id>`. The optional value uses the bridge's
existing non-empty UTF-8 text validation; it is not a ROS UUID parser or a public
extension API. A navigation owner must reserve its actual goal UUID before transmission,
retain it with the request and outlet scope, and supply it on every correlated
native event. Core already refuses a different identity once acceptance or
rejection has established it; this does not require changing Core or permitting
events from a different operation. An explicit identity is an observation from
the trusted owner, not caller authority or a substitute for ROS correlation.

Missing acknowledgement remains pending. After revocation, a late correlated
acceptance may establish the old native identity and support its cancellation;
it cannot restore result authority or settle the operation. The bridge must not
fill an unknown identity from the current visit, report synthetic acceptance, or
infer closure from a cancelled wait. Existing ALOHA/MCP calls retain their
episode identity and current behavior. This bridge support alone advertises no
navigation capability and supplies no new stop guarantee.

The next implementation slice extracts the request/operation retention and Core
admission, dispatch, expiry/cancel and result-disposition coordination currently
embedded in `_host.Host`. Keep the single writer and bounded retained records;
move actual callers to the shared code rather than adding a parallel coordinator
which they do not use. Host-specific worker prediction/queues and MuJoCo stepping
stay in the stepped driver. The navigation driver owns asynchronous goal/close
requests, callbacks, fresh input checks and profile-specific closure facts.
It must keep progressing both outlet closure and exact native cancellation.
Native success can precede complete closure; publish/retain domain feedback
separately and report Core settlement only after every required obligation is
observed. A diagnostic `closed` flag is not sufficient evidence.

The first extraction uses a private `ExecutionCoordinator` in the existing Python
package. It borrows the Host-owned Gate, retains the bounded request records and
one active record, and centralizes admission/dispatch, expiry/cancellation, native
facts, result disposition and explicit settlement. Core remains the authority
state machine. The Host still owns startup/readiness, backend-specific argument
and observation checks, prediction queues, physical submission, termination and
reaping. The coordinator creates no worker, ROS callback, thread or sensor loop;
its caller remains the existing single writer. Keep old Session signatures,
receipts, record capacity and default profiles unchanged.

Native terminal observation, result disposition and settlement are separate
coordinator calls. Drivers supply actual correlated native facts; only after
their profile's unresolved work and closure obligations are resolved may they
submit settlement evidence. Keep the active slot until the driver's existing
post-settlement handling completes. Retained old requests are readable, but
cancelling one cannot select the current operation. The coordinator does not
know how to stop a robot or how to turn an ambiguous future into non-submission.
ALOHA retains its current preparation-failure and pending-inference paths;
navigation must implement its own asynchronous cancellation/outlet closure.

Verify actual Host use through the existing Session, lightweight ALOHA and MCP
consumers, plus focused real-Core checks for retained/refused requests, old
cancellation, delayed native facts and expiry during result publication. This
first extraction is a single-consumer foundation; heterogeneous reuse is still
unproven until a real navigation driver uses it. Deferring extraction until that
driver is fully written would preserve duplicated ownership mechanics, while
copying the entire Host would also copy stepped execution assumptions. This
bounded extraction changes neither the public profile nor native protection.

The private module is now consumed by the existing Host. Local real-Core,
Session/backend/MCP and relocated installed-package checks pass; details and
limitations are in [Testing](TESTING.md#shared-execution-coordinator-extraction).
The first bounded research navigation consumer now uses the same module. The
prepared-owner request increment below adds installed client consumption; full
ROS driver/startup packaging remains a subsequent increment.

### First scoped navigation consumption

Use a bounded research driver to exercise the actual private coordinator and
compiled Core on the already qualified scoped Humble route before designing a
public navigation Session. The driver retains the send future and caller-reserved
UUID, records accepted/terminal facts from the matching Action handle, and checks
current Core authority immediately before the native send. All waits continue
single-writer Core time/input maintenance; neither a future timeout nor container
removal supplies non-submission or settlement evidence. This is a deterministic
consumer, without a new public API, Agent or externally routable cancel command.

The trusted bridge gains an optional settlement-scope configuration, retaining
the existing episode default. A scope name declares the owner's closure obligation;
it does not verify physical evidence. The navigation driver must actually observe
matching child/BT closure, manager PAUSE, outlet-applied acknowledgement and the
existing fresh quiet interval before publishing closure evidence. Preserve the
same budgets and physical evaluator. The existing reference route supplies these
observations; the coordinator does not contain ROS or motion predicates.

For this finite two-context profile, only A may settle/release, after its closure
and a fresh, still-ready B context are confirmed. B may publish its native result
and close physically, but stays active with settlement pending because there is
no prepared C. Reject further admission rather than inventing an unlimited pool
of contexts. Report B's terminal/closure and pending aggregate receipt separately.
Failure revokes Core authority, requests exact native cancellation and outlet
closure independently, retains possible submission and reports unresolved facts;
best-effort cleanup cannot qualify a stop. Pre-send expiry does not permit an
already opened outlet to be forgotten.

Local checks now cover these boundaries with the real Core and synthetic native
facts, while preserving old Session/backend/MCP behavior. A normal scoped A→B
simulation consumed the shared code: A settled before B admission, retained old
cancellation left B unchanged, and B closed with settlement pending. Review also
added checks after potentially blocking reservation/readiness logging, before
native submission or settlement. See [Testing](TESTING.md#scoped-navigation-research-consumption)
for the tested versions and retained failed run.
This proves only the bounded research consumption path. Public navigation transport,
general cancellation/late-future qualification, actual ALOHA/Nav2 business consumers,
installation/CI combinations and the live navigation Agent remain separate work.

### Navigation request entry increment

This implemented slice adds a local `NavigationSession` client for an already prepared,
trusted navigation owner. It connects to an exclusive local Unix endpoint; it
does not launch ROS, select executable paths or own the remote process. Closing
the client requests owner shutdown and reports unresolved closure separately from
connection cleanup. The existing stepped `Session` remains unchanged.

An owner-side `NavigationRequests` helper borrows the real shared coordinator and
the prepared driver's observation/readiness/context callbacks. It validates
`navigation.visit_site` proposals against a fixed map-frame site table, retains
request IDs and issued observation references, and admits one unit of native work.
It does not implement another authority state machine or a native execution loop.
Driver context identity is chosen by the trusted owner, never by the client.
The driver continues progressing asynchronous native work, input freshness and
Core time while servicing client requests. Models must not occupy that loop.

Capabilities report registered sites and current readiness. Observe supplies only
permitted localization/sensor observations with an owner-issued, bounded reference;
Gazebo ground truth is excluded. Submit takes a request ID, registered site and
that reference. Repeated identical submissions return the retained decision even
after the observation expires; conflicting reuse fails. New stale/foreign/unknown
references are refused. A newer sensor sequence alone does not invalidate a still
fresh reference, but a changed epoch/map/frame or invalid current input does.
The fixed reference age and existing driver freshness predicates both apply.

Status preserves request/Core/native/outlet association and pending settlement.
Cancel targets the retained request, first revokes current Core authority and then
schedules exact driver cancellation/outlet closure without awaiting either. A
receipt of cancellation intent is not a stop acknowledgement. Old-request cancel
cannot select current work. Close withdraws admission and requests driver closure;
EOF requests the same owner shutdown without asserting that native work stopped.
The endpoint is for one cooperating local caller, with bounded records/observations
and the existing framed transport; it is not a network or multi-tenant service.

This first request slice is consumed by the bounded research navigation
owner and an installed client outside the product source tree. Keep the native
driver/launch/physical evaluator in research until its production packaging and
new request-path protection qualifications are complete. This is a usable request
boundary over a prepared owner, not a turnkey ROS SDK or hardware profile.
Compare against simply extending stepped Session parameters (which would retain
step/reset and worker assumptions) and publishing the research script directly
(which would expose fixed paths and imperative task ordering). Verify real-Core
request replay/conflict, stale observations, busy/pending refusal and old cancel;
actual local client/owner normal A→B, retained A receipt, B pending and process
cleanup; retain existing Session/backend/MCP checks. Normal native success does
not qualify the new cancellation or disconnected-owner physical path.

Local normal consumption and focused request/connection checks passed, including
actual driver methods for EOF/close scheduling with synthetic ROS edges.
Independent design and final implementation review approved this limited slice.
See [Testing](TESTING.md#navigation-request-entry) for environment, failed startup
and the distinction between scheduling and physical stop qualifications.

Both installed deterministic business consumers now use the same current shared
coordinator source: retained ACT/ALOHA through `Session` / `HandoffTask`, and
scoped Nav2 through `NavigationSession`. See the [candidate consumption check](TESTING.md#m6b-shared-consumers).
This establishes finite cross-task consumption, with separate native builds and
profile-specific settlement. Installed navigation business-Agent normal consumption
has subsequently [passed locally](TESTING.md#m6b-navigation-business-agent); delivery
and applicable further failure qualifications remain separate.
First verify the bridge with real C++ Core identity/revocation checks and ordinary
Session regression. Subsequent extraction needs the affected continuous ALOHA,
direct/MCP and navigation integration checks. Unit tests of a future driver do
not replace actual producer/outlet qualification. Installed combination pins,
new model selection and navigation MCP exposure remain their own delivery work.

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
## Installed bounded Nav2 owner

This M6b increment packages the qualified two-context driver as optional
`robot_harness_nav2`, with an owner-only `python -m robot_harness_nav2` entry.
The external caller retains the existing `NavigationSession` request API. The
owner creates no Agent or client process. Its endpoint lives in a private local
directory; one client connection is consumed and EOF closes admission.

This profile requires the isolated Humble/Nav2 simulation, custom scoped native
workers and drive outlet, exclusive motion domain, fixed A/B sites and a live,
continuously polled owner. It does not accept arbitrary ROS installations or
hardware. A has a prepared B successor; B still cannot release without C.

The implementation separates ROS observations, scoped native closure, and the
Core/request owner. ROS imports are deferred until launch. No runtime imports
research files or consumes evaluator capture markers or Gazebo ground truth.
Actual goal identity is retained before submission and used for closure.
Normal arrival uses post-result TF; physical evaluation remains independent.

The simulation launcher owns the initial scene and the independent example
client; the owner owns B's launch process, Core, ROS clients and private endpoint.
Every wait pumps the same owner. Cancel, close and EOF revoke authority and
schedule native cancellation and exact outlet closure independently. Cleanup
is bounded, retains late acceptance, and reports unknown stop/pending settlement.
No cleanup path creates fresh authority or marks an aborted task settled.

Keeping research imports would prevent installed deployment; exposing a generic
ROS driver would exceed the qualified profile. Packaging the existing scoped
profile preserves the tested numeric predicates while removing those imports.
Revisit the finite context model when replenishment is actually implemented.

Acceptance requires an installed import without ROS, isolated startup rejection,
endpoint/EOF and cancellation scheduling checks, and a real installed owner plus
independent client A→B run with physical observations and exact container removal.
Moving cancellation/EOF through this installed route require separate physical
qualification before broader Agent consumption. Packaging alone does not
complete M6b.

The installed owner and independent public example passed one observed A→B run.
Final focused cleanup checks and independent implementation review passed.
The first public host-CLI run with bare DWB launched both processes but B's native controller
failed its progress check; abort kept authority revoked and settlement pending.
The explicit heading policy below subsequently completed standalone normal
reproduction. Keep the earlier failure. Do not hide it
with a task retry, a longer progress budget or a capture barrier in runtime.

### Native heading policy for the fixed navigation profile

With bare DWB, the first standalone run failed the native progress check: accepted B produced tiny
translation and alternating low angular commands until the unchanged 10-second
progress check failed. A diagnostic run with raw odometry/TF completed normally;
its success does not identify the exact DWB scoring cause or resolve reliability.
The specific critic/timing cause remains uncertain.

For the fixed differential-drive/NavFn scene, explicitly align to the new path
before DWB tracking, using the already installed Nav2 1.1.20 Rotation Shim. The
[upstream versioned description](https://github.com/ros-navigation/navigation2/blob/1.1.20/nav2_rotation_shim_controller/README.md)
identifies large initial heading changes as a use case. This is a native controller
policy choice, not an Agent decision or an additional Harness action/worker.
Use a new profile ID, `scoped-two-context-nav2-shim-v1`, and retain the original DWB
parameters and all authority, observation, goal, progress and closure limits.
Rotation is bounded by the existing controller/action waits; the shim's target
angular velocity is 0.8 rad/s with maximum angular acceleration 3.2 rad/s².
DWB's original 1.0 rad/s limit remains; 0.8 is not a physical or whole-route limit.
Native collision simulation is retained. On its normal path, the shim delegates
within 0.785 rad of the path heading; native collision/TF/path errors can cause
earlier fallback to DWB, as defined by the
[versioned implementation](https://github.com/ros-navigation/navigation2/blob/1.1.20/nav2_rotation_shim_controller/src/nav2_rotation_shim_controller.cpp). No dependency or image is added.

A pure profile module holds the ID and the parameter transformation consumed by
both launcher and owner. The module imports no ROS. Only the installed fixed
simulation route changes; sealed earlier DWB evidence remains scoped to its old
profile. Do not copy earlier moving-cancel or input-loss qualifications onto the
new controller policy.

Alternative: retain bare DWB and collect further scoring traces; this preserves
the original profile but leaves the known task unable to reproduce consistently.
Changing critics speculatively or extending progress time would not establish a
cause. The selected shim makes the intended initial heading behavior explicit;
it is a scoped policy adaptation, not a claim to fix every DWB failure.

Acceptance requires unchanged safety/observation predicates, actual new-profile
installation and native plugin load, physical normal A/B plus raw initial B
rotation-to-translation observations, standalone public CLI completion, exact
closure/settlement ordering and bounded process cleanup. Explicit heading control
must be observed on a nearly reversed B path. Without those results, this remains
an unqualified candidate. The implemented profile passed one independently observed
physical A/B run and a separate standalone public CLI run. Physical B started
2.92 rad from its target direction, rotated nearly in place, then translated over
0.5 m within 5.4 simulated seconds; both native contexts loaded the shim and DWB.
Original arrival and closure checks passed; A settled/released before B admission,
and final B remained pending. Separate public motion-time protection evidence is
described below; it does not establish a native reuse certificate.

### Public motion-time protection qualification

Qualify the installed shim profile using two external fault consumers: explicit
`NavigationSession.cancel` during B's initial rotation, and SIGKILL of the actual
connected client during B translation, with no preceding cancel/close and no
connection retained by its parent. Each first visits/settles A through the public API,
then submits B with a new issued observation. A separate read-only ROS observer
triggers the fault only after native B is accepted and actual motion is fresh
(rotation: angular speed over 0.6 rad/s with translation under 0.02 m/s;
translation: speed over 0.05 m/s and displacement over 0.5 m).

The external consumer supervisor retains the scene for the observation window;
it never keeps execution authority alive or delays owner cleanup. An independent
bounded producer sends nonzero old B commands (linear.x=0.2, angular.z=0) through the fixed smoothed topic after the
exact B outlet application ACK. External Gazebo snapshots and fresh odometry
must show a quiet window while the bridge forwards these commands and the drive
rejects them throughout both snapshots and the complete quiet interval. Existing 0.01 m/0.02 rad drift, 0.01 m/s/0.02 rad/s speed, 10 samples
over one simulated second, freshness and 0.25-second injection coverage limits
remain. No observer marker is consumed by the installed owner.

One offline evaluator correlates request/native UUID, B scope/generation, actual
motion, Core revocation, independent native cancellation request and applied
outlet seal, command rejection, physical quiet and no extra admission/reopen.
The host separately removes and checks the exact owned container, recording
that boundary result. The caller supervisor owns/reaps only its child;
the product launcher/container boundary owns final native OS release.
Preserve all failed attempts. Missing observations block qualification; do not
extend runtime cancellation waits or alter shutdown to obtain a pass.

Abort currently pumps cancellation and outlet closure for two seconds, then
reports stop unknown and keeps settlement pending. It does not assemble a native
closure/quiet certificate or permit reuse. External physical evidence cannot
upgrade that receipt to settled or confirmed stop. Native goal status and worker
logs may show later facts, but missing native termination remains unknown.
These probes qualify only the observed isolated live-owner protection path,
not owner death, lost outlet, a hard stop deadline or arbitrary robotics stacks.

Both finite probes completed with the installed owner and actual independent
public client processes. B rotation cancel had 33 fresh quiet samples over 3.196
simulated seconds; B translation EOF had 34 over 3.298 seconds. Fixed nonzero old
B input reached the bridge and was rejected through each complete physical window.
Core was revoked before exact native cancellation scheduling; correlated applied
outlet sealing and external quiet were observed. The installed owner correctly
retained result/output pending, settlement pending and stop unknown. No production
wait, cleanup or predicate changed to obtain these results.


The bounded navigation owner has an explicit caller idle-wait option:
`--caller-wait-seconds`, finite and at most 60 seconds. Unconfigured admission
waits remain 15 seconds and final-close wait remains 10 seconds. A model
consumer must configure compatible bounded waits; the local 30-second proposal
consumer uses 45 seconds including public RPC allowance. The owner continues
polling and handling EOF/cancel; expiry still aborts. This is caller think-time
configuration, not a larger execution deadline, freshness TTL or physical stop
budget. [Owner setup](../integrations/ros2/nav2_session/README.md) owns the command.


## Planned cancellation settlement for business revision

Design only; the installed navigation Owner still exits after current cancel,
close or EOF and leaves unresolved settlement pending. Its normal A close path
supports release before B, but the abort path does not qualify reuse.

The next bounded candidate is an explicit profile over the existing two scoped
Nav2 contexts, allowing one intentional cancellation of moving A and later B
admission. It would keep the existing public cancel/status/observe/submit shapes,
same coordinator and Core, and default profile behavior. Business intent belongs
to the caller, not to this profile or the model.

The Owner would revoke exactly A, schedule native cancellation and outlet sealing
independently, and keep progressing while collecting correlated native terminal,
child/BT/lane closure, drive ACK and fresh quiet. Aggregate release for reuse also
requires fresh B readiness. Cancel intent or external stillness alone cannot
supply settlement. A genuinely cancelled result remains cancelled/no-output;
a success racing cancellation retains its actual native and output disposition.
Missing or ambiguous facts keep A pending and prohibit B.

This requires separating intentional A closure from fault/EOF/global shutdown;
revocation may be tolerated only in that associated closure phase. Other faults
must retain the existing abort behavior. Closure would remain bounded by the
original operation/task deadline and normal 15-second close budget; the existing
two-second abort is not extended. Owner liveness remains a premise, without
claiming finite-permission, restart or hard-stop qualification.

First verify real Core disposition and actual Humble/Nav2 cancelled-A closure
with a private bounded probe. Only then implement and independently review the
explicit profile, installed tests and relevant failures. Agent consumption and
new installed/model/physics qualification follow that delivery. No dependency
pin, controller policy, Core contract or supported runtime behavior changes here.
