# Initial design and implementation scope

This is the implementation-facing summary of the research workspace's accepted
D-073 architecture and September 10 product split. The September 14 replanning
changes the implementation order to runnable behavior slices, preserving those
ownership boundaries. M0 established the build skeleton; M1 has been implemented
and merged. M2a implements failure closure; M2b/M2c add cancellation and expiry
with macOS and Ubuntu validation. M3a adds replacement within one live host/binding;
provider rebinding, restart recovery and M4 remain planned. See README and Testing for tested
status and validation limits.

## Source roles

This document records the accepted product ownership and recovery boundary for
implementation. README describes current product status; source describes what
exists; test and runtime output establish only the behavior actually observed.
Implementation status cannot relax the boundary defined here.

Original decision records and experiments remain in the research workspace:
`docs/DECISIONS.md` (D-068/D-073),
`docs/studies/PRODUCTION_ARCHITECTURE_SECOND_REVIEW.md` (section 4), and
`docs/experiments/PVE_B0_B1_H_ARCHITECTURE_REPORT.md`. These are research-relative
identifiers, not files shipped in this product repository. The host/task handoff
supplies their actual location when a change needs those sources. Do not invent
paths or treat an inaccessible historical result as current runtime evidence.

## Design principles

- Favor high cohesion: keep closely related responsibilities and invariants together.
- Keep coupling low through clear dependency directions and limited interfaces.
- Use modular boundaries that hide implementation details and allow local change.
- Make contracts, ownership, state transitions and error handling explicit.
- Prefer simple, testable designs; introduce abstractions for demonstrated needs.
- Evaluate these principles together with correctness, performance and maintenance
  costs. They guide tradeoffs rather than prescribe a fixed class or directory layout.

The shared engineering guides provide the design and review methods. The sections
below record this project's concrete boundaries and decisions.

## Product boundary

Robot Harness is independently usable by agents, behavior trees, and deterministic
applications. A separate Robot Agent project owns model selection, prompts,
context, goals, and task-level recovery. Dependency flows from Agent to Harness.

The small core is one component of the Harness product. Reusable capability
integration and feedback may grow around it when actual consumers need them.

P1 uses the `TRUSTED_EMBEDDED` profile: callers and adapters cooperate at the
declared boundary. An in-process library cannot isolate malicious code or prevent
direct native calls that bypass that boundary. An isolated gateway is later scope.

### Relationship to agent and mission runtimes

Here, Harness refers to reusable execution integration and operation governance.
An intelligence harness for model calls, context and action proposals belongs to
the Agent side. The current library supplies part of an execution runtime; it is
not a complete mission scheduler or durable task engine.

The caller owns goals, task steps and task-level recovery decisions. Core owns
operation authority and receipt composition, while adapters and native systems
supply execution and settlement observations within their declared scope. A task
summary does not replace those facts or establish domain success by itself.
Correlated task and operation states are therefore expected; they are not two
competing owners of the same fact.

These responsibilities do not prescribe one process or repository per layer.
Keep model and application dependencies outside this library; extract reusable
calling or feedback helpers when actual consumers expose repeated work. Existing
domain runtimes retain navigation, control and device responsibilities.

The local compute worker is a separate process, but its event handling, deadline
checks and stop escalation still depend on host progress. It does not provide
host-independent supervision. A future consumer that requires execution or stop
handling to survive a stalled model call or failed caller needs an explicitly
validated deployment for that requirement; adding a process alone is not evidence
that it is met. The current profile limits remain unchanged.

## Execution ownership

The core is a C++17 passive state machine driven by a host-owned single logical
writer. It does not create its own default threads. The host supplies monotonic
time and serializes control events; native runtimes retain payload execution.

The core owns operation identities, composition/provider generations, admission
to declared effect domains, stale-output rejection, settlement state composition,
and fresh recovery commits. Callers cannot mint execution authority themselves.

Adapters map native identities and outcomes, request native cancellation/stop,
and enforce authority at the actual declared submission boundary. They must
describe the scope they can enforce. An entry-point check alone cannot establish
that a later queued output remains authorized.

Native systems retain transport, scheduling, device access, continuous control,
model execution, and native safety enforcement. The core does not copy payloads
or replace these owners.

## Stop and resource requirements before expanded execution

The following requirements apply before admitting the first long-running,
resource-intensive or physical task beyond the bounded sample. They define the
next integration constraint; runtime capability matching and stop supervision are
not implemented by M2b/M2c. Current cooperative cancellation ends deferred sample
work before calculation begins. Synchronous calculation occupies the host and
cannot be interrupted by a caller cancel or idle poll. An empty result sink does
not establish that running work stopped, resources were released or motion ceased.

Each adapter must describe its enforced scope and provide observations for the
execution environment in which it will be used:

| Concern | Required declaration |
|---|---|
| Stop behavior | Whether stop is supported before submission, at execution checkpoints or through a native running-task stop; what may continue after a request |
| Response limits | Conditions and time origin for any claimed stop bound, cancellation-check frequency where applicable, and how an exceeded bound is observed; distinguish a measured result from an enforced guarantee |
| Remaining effects and resources | Which queued commands, callbacks, device work and resources remain outstanding; how their termination/disposal/release is established |
| Failure handling | The owner and supported action when stop is refused, unacknowledged, overdue or the host/worker is unavailable; explicit limits when isolation or escalation is unavailable |

Trusted application/deployment policy specifies the task's required stop behavior
and applicable resource budget. An Agent may request stricter limits but cannot
relax that policy or declare an adapter capable. Core admission must compare the
required guarantees with supported, currently valid adapter capabilities; the
host supplies the policy and observations, and the adapter enforces the declared
limits at actual native boundaries. Unsupported, unknown or no longer valid
required capabilities must reject admission before native work starts. If a
prerequisite changes after admission but before native dispatch, deny dispatch and
use truthful non-submission closure. If a required capability becomes invalid after
native work starts, revoke authority, use the declared failure handling and retain
unresolved effects until valid closure evidence arrives. No silent downgrade or
fallback is permitted. A bounded noninterruptible task can be admitted only when
its actual guarantees satisfy the task policy.

For compute work, enforce applicable size/concurrency/memory/usage limits before
and during execution at the owner capable of enforcing them. Keep cancellation
handling responsive while native work is running, using checkpoints or an
appropriate worker boundary. A separate process may enable termination of local
work, but does not by itself stop remote requests or establish shared GPU resource
release. Required resource-release evidence must match the actual execution scope.

For hazardous physical work, required controller/device protection must operate
under the declared host-stall or communication-loss conditions. Ordinary Action
cancellation, suppressed result delivery and killing a host process are not proof
of a safe physical state. Select and validate the native stop behavior and its
observations for the device; no universal abrupt-stop action is assumed.

A stop-response budget is distinct from the operation deadline. Exceeding it must
invoke the adapter's declared failure handling and retain unresolved settlement;
it must not fabricate a terminal state or reopen conflicting admission. Any
escalation is a separate supported action with its own observations, not an
unbounded retry of the same cancel request. A bound required during host blockage
cannot be enforced solely by the blocked host's poll loop. Keep the passive Core
and native safety ownership; choose the concrete supervision mechanism with the
first adapter. Exact API fields, units, bounds and isolation choices follow that
adapter's requirements, rather than a new universal scheduler or safety framework.

Verification is specified in [expanded execution checks](TESTING.md#expanded-execution-checks).

### First compute adapter slice: design baseline

Status: implemented following M2b/M2c, with macOS/Ubuntu normal and sanitizer
validation complete and scoped independent review approved. This slice precedes M3; replacement and restart
recovery retain their existing scope.

The first task is a trusted, local CPU worker computing
`sum((i % 1000) * (i % 1000), i = 0 .. N-1)` in batches. The initial workload
ceiling is `N = 100,000,000`, with one in-flight worker per host instance, no request queue and a
single scalar result. This is a workload ceiling, not a CPU-time guarantee.
The worker checks cancellation between batches of at most 4096 iterations and
reports completed iterations. The result fits in an unsigned 64-bit integer;
normal tests use an independent arithmetic result check. No model, GPU, network,
user-provided executable or descendant process is involved.

**Selected boundary.** Use a dedicated child process owned by a new native adapter,
with a fixed executable and bounded control/event channels. Keep the existing
sample adapter and passive, single-writer Core. The host alone updates Core;
the worker neither holds execution authority nor calls Core. Compared with a
cooperative thread, the process permits a separate local termination action when
the trusted worker ignores the cooperative request. A thread would leave that
failure inseparable from the host; a container/cgroup manager is unnecessary for
this first task. The process is not an untrusted-code sandbox.

The initial supported policy covers bounded work units, one worker and bounded
application message storage, cooperative cancellation and a host-driven local
termination fallback. Require Core to compare trusted task requirements with
current adapter capability observations before allocating authority; repeat the
applicable check before native submission. This needs a focused extension of
Core admission and its refusal reason, not a caller-supplied `is_safe` flag or a
parallel admission gate. The concrete fields are defined in the
[local integration contract](#local-compute-integration-contract); they express
supported limits and observation validity without enumerating hypothetical devices. For this adapter, prelaunch
readiness means the adapter can launch the fixed executable with the required
policy, channels and ownership support; it is an adapter-level observation tied
to the current binding and rechecked at dispatch. It does not mean a child already
exists. Per-operation child readiness/start is observed after launch and cannot
retroactively justify admission. The M1 fixture readiness rule retains its own
meaning; this distinction must be represented explicitly in the Core integration.

Reject requirements for a hard wall-clock stopping bound, host-stall-independent
supervision, total-process memory/CPU quotas, GPU release or physical stopping.
A scalar workload and bounded application buffers do not establish a process RSS
limit. Unknown or unsupported required limits must not be silently omitted.
Linux cgroup limits are a later option if a concrete task needs them, not a
prerequisite for this bounded worker. This slice does not close every expanded
execution requirement for every adapter.

**Ownership and observations.** After admission, the adapter owns the child
handle and both channel lifetimes until exit is collected and buffered output is
accepted or disposed. Perform the dispatch permission check immediately before
launch; only a proven no-child-created failure or a prerequisite change before
launch uses non-submission closure. Once a child exists, even if executable startup
fails, retain its ownership and resolve exit/output/cleanup through the submitted
path.
A successful launch is submission, not proof that computation started: the worker
reports readiness/start and actual batch progress. Input is validated before
launch and again by the fixed worker. Use bounded nonblocking communication;
coalesce progress so a full event channel cannot prevent checking cancellation.
Retain terminal/result information until consumed; malformed, truncated or missing
messages never establish success. Preserve operation identity when translating
native observations into the existing event and receipt layers.

On cancellation or expiry, Core revokes authority and requests cooperative stop
once. The adapter separately tracks a stop-response interval on the host's
monotonic clock, in milliseconds, starting when the host acts on Core's first
stop decision for the owned child, before attempting the control send. Record the
send outcome separately; a failed or unwritable channel must not postpone this
anchor. Cancellation after launch but before child readiness follows this same
path: retain ownership, withdraw output permission, try the supported cooperative
request and escalate once if the channel is unavailable or grace expires. Do not
wait indefinitely for a readiness/ACK event before enabling escalation. The initial example interval is 100 ms, configurable by trusted host
policy; it is a grace interval, not a stopping SLA. If refusal or elapsed grace
requires escalation, the host requests termination of the still-owned child once.
The host must remain scheduled and poll to perform this escalation; stronger
requirements are refused. Neither successful signal delivery, pipe EOF nor a
stop ACK proves exit. Collect the child's exit status, resolve output and close
owned channels before reporting settlement. An escalated exit is reported with
its observed cause, never fabricated as cooperative cancellation or task success.
A reap/signal failure or uncertain exit keeps the domain blocked. Never target a
PID after collecting its child exit; preserve child ownership against PID reuse.

Shutdown follows the same stop/exit/output/channel ordering. It must not detach
live work or report cleanup merely because the host is being destroyed. The
nonblocking shutdown/status API and destructor fallback are defined in the local
integration contract; a destructor cannot promise bounded return and
confirmed termination under arbitrary OS failure. Host crashes and reconstruction
of ownership remain unimplemented M3/adapter work, not an implicit guarantee.

**First engineering step.** Validate the process boundary in a small isolated
probe before integrating it into Core. Observe real progress, cancellation,
ignored cancellation with escalation, exit collection and channel cleanup. Probe
execution must use a finite workload and an outer test timeout with cleanup of
only its own child; a test timeout is not product stopping evidence. Linux must
run these process checks before Linux support is accepted; Mac may provide an
earlier POSIX feasibility result. Resolve process API semantics, child identity,
channel bounds/closure and shutdown ownership through that probe; do not infer
feasibility from this design. See [compute slice checks](TESTING.md#first-compute-slice-checks).


### Local compute integration contract

The local compute process adapter is the first concrete backend, with a CPU-only
workload. Adapter boundaries follow native execution/ownership APIs, not a fixed
CPU/GPU hierarchy. A future GPU, remote-service or controller adapter supplies its
own supported profile and execution/release evidence. Core remains payload- and
backend-independent.

`ExecutionRequirements` carries an exact `profile_id` and consumed `work_units`.
`ExecutionCapabilities` binds that profile and its maximum work units to the
configured provider/binding, capability ID, trusted observer, sequence and an
exclusive `valid_until` time. A configured capability observer makes requirements
mandatory. Core rejects absent, mismatched, unavailable or expired observations,
retains admitted requirements, and checks them again in `claim_dispatch`.
Conflicting observations with the same sequence are rejected. This is a typed
profile match with a directly compared workload limit, not a caller's safety flag.

`host-polled-local-compute-v1` means the bounded local task described above: at
most one worker per host instance, no request queue, <=100,000,000 iterations, fixed-size progress
storage, cooperative checkpoints and host-driven local termination fallback.
It does not include a hard stopping deadline, whole-process memory/CPU quota,
host-independent supervision, remote/GPU release or physical stopping. A caller
requiring stronger guarantees must use a different profile; this adapter rejects
it. The trusted host fixes this minimum profile and may lower the workload ceiling;
request metadata cannot loosen it. Stop grace is a trusted host configuration,
not a guaranteed time to release resources.

`StartupEvidenceKind::kAdapterReady` explicitly observes prelaunch support. Gates
configured for it reject `kWorkerReady` as a substitute; existing sample gates
retain their worker readiness meaning. A spawned child's readiness becomes the
operation's native started observation. The host refreshes launch capability
observations before admission and dispatch, with a 1000ms observation validity;
this interval is neither a stop grace nor proof of future OS resource availability.
Loss of a required condition during execution revokes authority through the
existing cancellation path. Native launch may still fail after preflight.

This profile deliberately retains the same deployment conditions during execution:
each open-host poll with a launched child and unclosed channels rechecks the
executable path and SIGCHLD policy. Losing the
path or its execute permission therefore revokes authority even if the existing
child could continue. This is a conservative deployment policy, not evidence of
child failure or an OS resource reservation. Launch readiness and running health
are different concerns; separate them when a backend needs different conditions.
Changing the probe cadence would also change detection latency and must preserve
the declared freshness and cancellation semantics.

`claim_dispatch` reserves the actual submission boundary immediately before
`posix_spawn`. A spawn failure proven to have created no child may be reported
through `observe_non_submission`, including after that claim, only while there
are no native acceptance/start/terminal observations. The claim timestamp may
remain in the receipt as the attempted boundary time. After a child is created,
its ownership and effects cannot be rewritten as non-submission.

The control channel uses a nonblocking local socket pair so a closed peer is
handled as a per-send error without changing the embedding process's SIGPIPE
policy. Events use a nonblocking pipe with fixed 32-byte frames and at most 128
frames consumed per poll. Control-send errors are separate from event/protocol
errors; a failed late cancellation cannot erase a valid successful exit.
The adapter requires default SIGCHLD disposition and sole ownership of child
reaping; callers must not change that policy or reap the child while it is owned.
Unrelated inherited descriptors are the embedding host's CLOEXEC responsibility.
Each owned descriptor is closed once and its numeric handle immediately discarded;
close failure remains unresolved rather than retrying a potentially reused handle.

The host maps spawn success to native accepted and worker readiness to started.
It postpones terminal adjudication until collected OS exit and event EOF: valid
completed data plus exit 0 must pass the independent arithmetic result check;
valid cancelled data plus exit 20 requires a stop request and incomplete work.
A signaled/nonzero exit or invalid/missing protocol result is an adapter execution
failure, with raw exit diagnostics retained, never fabricated cooperative success.
A lost child ownership observation cannot establish closure. Successful native
work after revocation is retained as native success, while result delivery is
denied at the actual sink boundary. Result storage retains at most one value.
Output disposition and confirmed channel closure precede settlement.

Each host owns a distinct result sink/effect domain and receives a process-local
unique binding generation on construction. Its work limit applies to that host,
not to the sum of multiple hosts. An authority from one instance cannot control
another. These identities are not persistent or transferable across processes;
restart/reconstruction remains outside this slice. The embedding application
serializes process creation with channel setup and owns the stated SIGCHLD/reaping
preconditions; concurrent external fork/exec during setup is outside this profile.
Setup descriptors obey the same one-close/error propagation rule as persistent
channels. A no-child launch failure with uncertain setup cleanup records
non-submission but cannot report settlement. After spawn, setup cleanup failure
retains child ownership and triggers containment rather than rewriting submission.

`ComputeExecutionHost::begin_shutdown()` closes admission and requests stopping
once; `poll()` advances currently available observations. `shutdown_status()`
reports open, closing, closed or unresolved. Closed requires no live owned child
and resolved Core settlement. Destroying a live host invokes the process owner's
synchronous terminate/reap fallback, with no bounded-return promise or fabricated
Core settlement; callers needing observable completion use explicit shutdown.
Irrecoverably lost child ownership or uncertain descriptor cleanup in this
fallback reports the failure and terminates the embedding process. This explicit
fail-fast limitation is not crash recovery or a general-purpose shutdown policy.
The host is single-threaded and must keep polling. No background controller,
host-crash recovery or M3 replacement is delivered by this slice.

This `poll()` is a host progress method, not the OS `poll`/`epoll` readiness API.
It handles expiry, capability checks, process observations, receipt updates and
closure as applicable to the current operation. For an active child, process
observations are mapped before completion and shutdown status are evaluated;
expiry and cancellation can also update receipts earlier in the call.
Event reads and exit collection do not wait for future
events, and event consumption has a per-call frame limit. The whole call has no
hard runtime bound: capability probes include filesystem operations, and receipt
processing may allocate or copy storage. The caller must schedule progress even
when no worker event arrives, so deadlines and stop escalation can advance.
A delayed call delays escalation; stop grace does not bound total stopping time.
The example's 1ms sleep is a demonstration cadence, not a scheduling guarantee.

## Results and recovery

Keep request acceptance, cancellation acknowledgement, native termination,
required effect settlement, and task/domain verdict distinct. A successful tool
return or process exit is not automatically proof of physical completion.

An unsettled operation blocks conflicting effects. Revoke old authority and
request native stop as appropriate; admit a conflicting replacement only when
the declared settlement and fresh-binding conditions are met. Missing or
conflicting evidence remains unknown. Core/host restart begins in
`RECOVERY_REQUIRED`, not in an assumed clean state.

Task-specific evaluators provide scoped evidence. Agents choose what to do next;
their success estimates do not override execution authority or native safety.

## First consumer and normal execution

The first consumer is a small deterministic application submitting a sample to a
native worker and receiving its result at a managed output sink. This follows the
existing F0 workload's source / processing boundary / worker / sink roles. They
are fixture responsibilities, not four new Core modules or a general pipeline.
Sample payload and result storage belong to the adapter/native fixture; Core
sees only opaque references and control metadata. The fixture performs a small,
deterministic operation whose actual output the caller can inspect. It does not
implement navigation, inference or a physical controller.

This replaces the earlier illustrative `move_to(target)` choice. The historical
Python worker and F0 scenario provide behavior and integration references; their
wire protocol, fixed payload size, process layout and artifact system are not
product requirements. A robot action can later exercise the same boundary.

The first deliverable is the entire path below, in one executable:

```text
application submits sample A
  → adapter validates domain arguments and retains the request payload
  → Core admits a current binding and allocates operation authority
  → host dispatches through the adapter's declared submission boundary
  → native worker executes and reports correlated events and a result reference
  → host stages and serializes evidence into Core for correlation/state checks
  → adapter uses Core's current permission at the actual result acceptance boundary
  → sink accepts the result; adapter reports observed disposition and settlement
  → Core updates a layered receipt from those observations
  → application observes native completion and declared settlement
  → application inspects the actual result and may submit sample B
```

The adapter checks authority at actual submission using Core's decision. Admission
alone does not establish native acceptance or execution. A delayed submission must
be checked again at its actual boundary. Fixture execution steps remain native;
Core is not invoked to schedule each worker step. The declared M1 effect is
acceptance of the managed result, not physical motion. Checks apply at native
submission and at result acceptance; neither is a check on every payload byte.
The host keeps the permission check and managed result acceptance in its serialized
control sequence. Native terminal evidence alone does not release the domain while
result delivery or required cleanup is still pending. A rejected or uncorrelated
completion cannot reach the sink and then be corrected only in the receipt.

Start with one statically wired provider, one conservative effect domain and at
most one active operation. A conflicting request is explicitly rejected/blocked;
there is no implicit queue, retry, preemption, or task planner. Sequential actions
are chosen by the example application after inspecting the previous receipt.

Every Core instance starts with affected domains in `RECOVERY_REQUIRED`, including
the first example startup. The fresh fixture reports its observed idle/settled
state and current binding before admission opens. Worker readiness and availability
of the declared result boundary must also be observed: host activity or process
existence alone is insufficient. M1 implements this narrow
initialization path; full restart and replacement scenarios arrive in M3. An empty
in-memory operation table alone is never evidence of a clean native system.

## Commands, evidence and receipts

These are semantic roles, not frozen C++ names or a generic event framework:

| Item | Producer and meaning | Consumer / ownership |
|---|---|---|
| Action request | Application selects a capability and adapter-owned arguments | Adapter validates arguments; Core decides admission and allocates authority |
| Control command | Host requests dispatch, later cancellation or recovery | Adapter/native owner performs execution or stop; Core owns permission/state decisions |
| Native event | Adapter reports native acceptance, observed start/progress when available, terminal outcome, or settlement evidence | Host delivers it serially; Core validates correlation and updates the receipt |
| Operation receipt | Core reports accepted status, native outcome, settlement and authority disposition separately; optional domain verdict retains its source/scope | Application decides what to do next |
| Trace/export | Host exports transitions and diagnostics for people or tools | Best-effort observation; dropped logs cannot change authority or block progress |

For state-changing evidence, retain operation correlation, binding/generation,
evidence source and observation time in the host's monotonic clock domain. Native
identity mapping is adapter-owned. The fixture can supply an event sequence to
identify duplicate observations; a universal wire format is not required. Evidence
with an unknown, stale or wrong operation identity cannot update the current
operation. Duplicate evidence is idempotent. Conflicting valid terminal evidence
for the same operation preserves the observed facts but marks the outcome unknown
and keeps conflicting admission closed; an earlier success cannot win by arrival
order. Duplicate evidence cannot dispatch another action or regress a terminal
operation.
Progress is optional information and cannot reopen admission or imply success.

The host owns the event loop, time, adapter lifetime and delivery of native control
evidence. Core returns state/decisions without blocking on native work or calling
back into itself. Synchronous native callbacks must be staged by the host and
applied after the current transition. M1 exercises both completion during native
submission and deferred completion under explicit host control, without requiring
a new worker thread. Correctness-bearing native evidence is not
the best-effort trace stream; missing required evidence leaves the result unknown
and conflicting admission blocked. No event bus, database, default worker thread
or persistent replay is introduced.

Request payloads and callback targets remain alive under host/adapter ownership
until native use has ended. On shutdown the host closes admission and follows the
adapter's declared drain-or-stop policy, requesting native stop when that policy
requires it. It drains the required callbacks and releases native resources before
destroying their adapter targets. The finite M1/M2 sample defaults to draining;
explicitly revoked output stays revoked during shutdown. A provider that cannot establish cleanup must
report that limit rather than emit settlement. M1 proves this only for its finite,
deterministic in-process fixture; other shutdown paths are added with their adapter.

A receipt is a set of distinct facts, not one `success` boolean. In the normal M1
example, Core admission, native acceptance, native completion and fixture-scoped
settlement are separately visible. `started` is only emitted if actually observed;
acceptance must not be relabeled as execution start. The caller/test checks the
actual fixture result, but no domain/physical verifier means the task verdict remains
unassessed. Native success cannot manufacture physical or task success.

## Implementation sequence

The milestone labels below replace the previous layer-first ordering (M1 Core,
M2 native adapter, M3 ROS, M4 deferred regression). D-073 ownership and total P1
scope remain unchanged. Each milestone extends the same runnable example and adds
its own focused regression immediately.

| Milestone | Runnable behavior and deliverable | Exit evidence |
|---|---|---|
| **M0 — build skeleton** | Original placeholder library and smoke executable | Historical build/link baseline; M1 replaces the placeholder and smoke test |
| **M1 — normal action and events** | Minimal Core, deterministic native adapter, host and example together; observe fresh initialization, admission, native execution, terminal and settlement; then run a second action | Actual fixture submissions/state plus caller-visible receipts agree; no submission before all declared prerequisites are ready or on invalid/rejected input; synchronous and deferred completion; duplicate/wrong-operation evidence cannot corrupt the result; actual sink delivery and clean shutdown; macOS and Ubuntu tests |
| **M2 — failure and cancellation** | Extend the same action with native rejection/failure, cancel request/ACK, delayed settlement and deadline expiry | No implicit retry; cancel ACK and timeout do not imply stopped/settled; conflicting action stays blocked until required evidence; missing or partial-effect evidence stays unknown |
| **M3 — replacement and recovery** | Caller changes A to B; hold old output, restart provider or Core, re-observe and commit fresh authority; support declared conflict sets/composite availability as required by these cases | Stale output cannot commit at the actual simulated sink; unsettled conflicts remain blocked; invalid/expired/conflicting recovery evidence cannot reopen; fresh binding/revision/generation replaces old authority |
| **M4 — ROS integration and reuse** | ROS 2 Humble lifecycle/action-facing adapter reuses the same Core; map the applicable M1–M3 scenarios to actual ROS boundaries | Ubuntu native events and effects agree with receipts; shared admission/fencing/recovery logic is not reimplemented in ROS adapter; capability limits are explicit |

M1 is deliberately a restricted usable slice, not an assertion that all P1 safety
and recovery behavior exists. Identity and initial binding/generation are present
from the first slice; replacement, expiry and failure transitions grow in the
milestones that exercise them. Until implemented, such operations are unsupported
and cannot silently grant fresh authority. Dynamic provider recovery is not needed
to demonstrate normal execution in a freshly observed deterministic fixture.

M2/M3 retain the original P1 counterexamples: exception after partial effect,
aborted execution followed by a late effect before retry, unavailable dependencies,
provider withdrawal/rebind and host restart. Tests are drawn from historical
F0–F7 behavior; no fixed assertion registry or historical artifact pipeline is copied.
M4 validates each adapter's actual declared scope. A ROS adapter unable to stop or
fence a downstream effect must expose that limitation and block unsupported strong
claims, not mark every scenario as an unconditional pass.

Concrete C++ types stay repository-local while the examples settle. Add only the
source/header/example/test directories used by the current slice; no empty module
scaffold or new production dependency is needed. The example is an actual caller
of the library and adapter, not a script that prints expected events.

## M3a operation replacement boundary

M3a exercises a caller changing operation A to B within one live host and the same
provider binding. The caller retains B's intent, requests cancellation of A and
resubmits B only after A's declared settlement. Core continues to allocate a new
operation authority and enforce the occupied domain. A new replacement command,
implicit queue, automatic retry or provider generation change is unnecessary for
this slice. Provider rebinding and host/Core restart belong to later M3 work.

The example and regressions must distinguish these boundaries:

- Native work still pending, or terminal/output observed but settlement missing:
  B is rejected without another native submission.
- A settled: B obtains a different operation ID and can execute in the same binding.
- An A result callback replayed while B is prepared, running or ready to deliver:
  it reaches the managed sink's current permission check and is denied without
  attributing A's output to B's receipt or accepted results. Old dispatch/cancel
  requests cannot control B; the host still observes time for B's own deadline.
- B's actual result is accepted once; a replay cannot duplicate it. Shutdown must
  drain staged callbacks under closed admission and release replay storage.

The deterministic fixture optionally retains the first generated result callback
after `set_result_replay_enabled(true)`. Calling that setter clears the previous
slot; later native results do not overwrite it. `replay_retained_result()` stages
a copy as the next event, with the original authority, identity and data, through
the existing host event path. This explicit fixture ordering lets A arrive when
B has native success but has not yet delivered its result. Replay never recomputes
the result or creates authority. Disabling replay or closing clears the slot.
It is one retained fixture copy for testing duplicate delivery after original work
and output disposal, not an outstanding native effect or a recovery log. The
original result and cleanup must still be observed before B can be admitted.
No arbitrary callback/authority injection is exposed. A sink permission-denial
counter, alongside actual result storage, makes rejection at that boundary visible.

The compute regression separately checks cancellation, OS exit/reaping and cleanup
before a new request uses the same host. The child has exited and its channels are
closed before replacement, so synthetic old-result replay belongs to the fixture,
not the compute protocol. Both paths reuse unchanged Core authority rules; neither
establishes crash recovery, external shared-domain isolation or physical stopping.

## Planned M3b and M3c boundaries

These slices allocate the existing M3 requirements; they are not implemented by
M3a. The current public gate has a configured binding and no rebind or recovery
commit API. Initial generation fields and fresh initialization do not establish
restart recovery. Concrete API and deployment choices remain implementation work.

**M3b — provider withdrawal and rebinding within a live host.** Close affected
admission when a required provider/dependency is unavailable. Retain the old
operation's effect and cleanup obligations while withdrawing its authority; a
replacement provider being ready cannot settle the old provider's work. Commit
a fresh binding only when the declared old-scope settlement and new dependency
conditions are satisfied. Old controls, observations and outputs cannot become
evidence for the new binding. Recheck required availability at actual dispatch.

M3b also owns the conflict/composite-availability requirement needed by these
cases: account for every declared prerequisite and block conflicting effects
while any required old obligation remains unresolved. Start with the declared
conservative domain and actual worker/adapter/sink dependencies; this is not a
requirement to build a general conflict graph or scheduler. If that representation
cannot express the selected case, resolve the concrete gap before implementation.

**M3c — host/Core restart and recovery from unknown state.** Begin closed in
recovery-required state and reconstruct permission from fresh observations of
the relevant native execution, outputs, settlement and binding. Cover old work
still active, partially completed effects and contradictory terminal evidence;
neither a new process nor saved task status establishes a clean execution scope.
Invalid, stale, expired or conflicting recovery evidence must not reopen it.
Interrupted or failed recovery must not leave partially usable new authority.

Before implementing M3c, specify who observes or controls surviving native work,
how old identities remain distinguishable across restart, and how observation
freshness is established in the new host's time/sequence context. Do not assume
process-local counters or monotonic timestamps survive restart. If the adapter
cannot establish the required facts, report unresolved recovery and keep conflict
admission closed; requesting operator attention is not permission to clear it.
Core recovery does not automatically resume a caller's mission or retry an effect.

Provider replacement, host failure and task continuation are distinct cases. A
fixture may exercise adverse event ordering, but a claim about process restart
or surviving work also needs observations at that actual process boundary.
Focused acceptance is maintained in [M3b/M3c checks](TESTING.md#planned-m3b-and-m3c-checks).

## M1 caller and integration surface

The provisional [AuthorityGate](../include/robot_harness/authority_gate.hpp) is the
passive Core. The [SampleExecutionHost](../include/robot_harness/sample_execution.hpp)
is a concrete fixture host that integrates a deterministic adapter, worker, and
result sink. A different integration supplies its own host and adapter around the
Core; it does not inherit the sample payload or transform as a product protocol.

The sample request contains an application reference and integer values. The
worker computes squared values and their sum; these remain outside the Core. The
fixture accepts 1–16 values in the range -10000 through 10000, keeping its arithmetic
bounded. The host accepts a nonthrowing monotonic clock whose target outlives the
host. Its native callbacks are staged so completion during submission cannot
re-enter a Core transition. The caller can
explicitly complete a deferred operation, inspect the actual result and current
receipt, and then submit another request. Both Core and fixture host are
noncopyable to preserve their operation identity and callback ownership.

In synchronous mode, `submit()` computes the sample and drains the staged events
before returning. In deferred mode, `submit()` reports native acceptance and
retains the request; `complete_deferred()` computes it and stages the remaining
events. The caller then uses `process_next_staged_event()` or
`process_all_staged_events()` to update the receipt and deliver the result.
Native completion alone therefore does not imply that Core has received its
evidence or that the sink has accepted its result.

The sample's `started` observation is emitted when its native execution routine
begins, before calculation or a controlled execution failure. The example clock
advances an integer on each observation, so neither the event times nor their
differences establish execution latency. `shutdown()` closes admission
and drains finite pending work and callbacks while the sink is available, then
closes the adapter. This is normal draining, not cancellation.

For the current receipt, a newly correlated, source-valid conflicting terminal
observation remains a conflict even after settlement, until another operation is
admitted. After that next admission, evidence for the old operation is stale and
cannot modify the current operation. This does not implement durable receipt
history or recovery across restart; callers retain any historical receipt copies
they need.

## M2a caller and integration surface

M2a extends the same provisional Core and sample host. Implementation and tested
platform status are reported separately in README and Testing.

`observe_non_submission()` records a definitive adapter refusal before native
submission. The receipt reports `DispatchStatus::kNotSubmitted` and
`NativeOutcome::kNotExecuted`; it does not invent native acceptance or a terminal
success. Its source is the configured `settlement_evidence_source`, which already
identifies the adapter responsible for the fixture's cleanup observations.

`NativeEventKind::kRejected` records a native submission attempt that was not
accepted, using the native source and attempted native identity. Acceptance and
rejection are mutually exclusive in this fixture protocol: an incompatible later
acceptance/rejection observation is rejected without replacing the earlier fact.
Conflicting native success/failure facts still produce an unknown blocked outcome.
This assumes truthful, definitive native rejection; a missing response is not one.

`observe_output_not_delivered()` records `OutputDisposition::kNotDelivered` and a
reason. `kNoOutputProduced` comes from the adapter after non-submission, rejection
or native failure. `kSinkRejected` comes from the managed sink after native success
and an actual failed acceptance attempt, with the remaining result discarded.
Identity and current permission are checked before classifying sink availability;
an invalid result cannot manufacture delivery-failure evidence for another action.
In either path, settlement remains a separate observation with matching identity,
source, scope and a time no earlier than the prerequisite facts.

For one operation, new output non-delivery and settlement observations must have
strictly greater sequence numbers than previously recorded facts from the same
source. Equal observation times are valid; a later time does not excuse a reused
or regressing sequence. Different sources have independent sequences, and exact
replays of a recorded fact remain idempotent even after later closure facts.
The host preserves the adapter's settlement callback sequence instead of assigning
a new constant. An operation whose output is definitively not delivered cannot
subsequently deliver that output.

The sample host offers `reject_next_native_submission()` and
`fail_next_native_execution()` for deterministic failure scenarios. The first
applies to the next native submission attempt; the second applies when native
execution next begins. A pre-dispatch refusal does not consume either control,
and a native rejection does not consume the pending execution-failure control.
These controls do not retry work or represent production failure-detection policy.
`worker_submission_count()` counts attempts after the actual dispatch permission
check, including native rejection; `worker_execution_count()` counts executions
that began, including controlled failure. Inspect these alongside actual sink
results and the layered receipt.

In synchronous mode `submit()` drains the observations for its request before
returning, including definitive dispatch refusal. In deferred mode it applies
the initial non-submission/acceptance/rejection observation and leaves subsequent
output disposition and settlement staged for explicit host processing. Thus a
rejected native attempt can still occupy the domain while cleanup evidence is
pending. The normal success flow is retained, and shutdown defaults to finite
draining of retained native work and its callbacks.

## M2b caller and integration surface

`request_cancel(authority, observed_at)` on Core revokes future dispatch and output
before returning a `ControlDecision`. The host consumes `should_request_native_stop`
once. Repeated cancellation returns `kAlreadyRequested`; a stale/unknown identity
or released operation returns `kNoActiveOperation`. A backdated control is rejected.
`kRevoked` means the operation still occupies its domain; `kReleased` requires the
same terminal/output/settlement closure as the other paths. Neither disposition
erases the recorded cancellation reason.

`observe_stop_acknowledgement()` accepts one definitive acknowledged/refused/
unavailable response, correlated by the full authority. A missing response leaves
`kPending`. The configured `stop_evidence_source` (default `native-stop`) has its
own response sequence, separate from native execution, sink and settlement sources.
It must differ from those three source identities. This permits a synchronous
stop response while older native callbacks are staged: a response never orders or
relabels those callbacks as termination. The request decision is the host's
instruction to attempt a stop; only the acknowledgement evidence records a
response. Settlement is ordered after its actual native/output prerequisites;
a later independently observed control/ACK does not invalidate earlier completed
cleanup delivered afterwards. New admission and control timestamps still respect
all recorded observations. `kTerminalCancelled` is a native terminal observation; conflicting
success, failure or cancellation terminal facts produce `kUnknown`.

The host's `prepare()` retains a request without submission and returns `kPrepared`;
`dispatch_prepared()` checks the full identity and performs the real boundary
check. `submit()` composes them. Cancellation of a prepared request discards that
payload before staging non-submission, no-output and settlement observations.
Shutdown also disposes an undispatched request without claiming cancellation.
A failed/stale dispatch call does not consume another operation's prepared payload.

`set_cancellation_mode()` controls the deterministic fixture: cooperative work
acknowledges but retains its payload until explicit completion/draining, then
reports native cancellation without computing the sample. Ignore mode acknowledges
and later completes naturally; refuse/unavailable/no-acknowledgement modes also
continue naturally. `native_stop_request_count()` observes actual adapter stop
attempts. These controls are fixture behaviors, not guarantees of a robot API.

At the actual result sink, revoked permission causes disposal and
`kAuthorityRevoked` output non-delivery evidence. Native success remains success.
Already accepted results remain in the sink. The fixture's
`set_settlement_reporting_enabled(false)` suppresses the cleanup report even when
resources are drained, allowing tests to verify that shutdown cannot manufacture
settlement. It does not implement M3 recovery or replay a suppressed report.

## M2c caller and integration surface

`OperationRequest::deadline` and `SampleRequest::deadline` are optional absolute
times in the supplied monotonic clock domain. Admission at or after the deadline
returns `AdmissionStatus::kExpired` without allocating an operation. An admitted
receipt retains that deadline and, when observed, `expiry_observed_at`. Expiry and
`cancellation_requested_at` are independent reasons for the same revoked permission;
both can remain present without issuing a second native stop.

Core's `observe_time(authority, now)` is passive: it returns `kNotDue` before the
deadline, `kApplied` on first observed expiry, `kAlreadyRequested` for repeated
expiry, and `kNoActiveOperation` for stale/released authority. Invalid causal times
are rejected. It uses the same `ControlDecision` stop instruction as cancellation.
A host must serialize time checks with effects and consume that instruction; a
stored receipt or `can_deliver_result()` query never reads a clock. Dispatch and
output evidence also reject timestamps at/after the deadline as boundary backstops;
a rejected attempt does not replace the host's explicit expiry observation.

The sample host calls `observe_time()` before new admission, actual dispatch, event
processing, actual sink acceptance, settlement, native completion and shutdown.
`poll()` supplies an explicit idle entry point. Boundary checks take fresh clock
samples even if the function-entry check has not expired. Sink acceptance and its
evidence use the same boundary observation, avoiding a second clock read that
could misdate an already accepted result. Callback observation times remain the
original native facts. Synchronous native work is finite but cannot be preempted:
the following boundary denies any late output, then drains the actual callbacks.

Deadline covers closure, including pending output/cleanup. An operation released
before expiry stays released. If settlement evidence is still pending at expiry,
the receipt records expiry and can later close from valid native/output/cleanup
facts. Already accepted output remains accepted. ACK and control delivery order
do not impose new causal requirements on previously observed cleanup, as described
in M2b. This is cooperative host-driven authorization, not a physical stopping or
real-time scheduling guarantee.

## M2 planned behavior: failure, cancellation and expiry

This section defines the accepted M2 behavior and slice boundaries. The caller
surfaces above now implement M2a/M2b/M2c; see Testing for actual evidence and its
scope limitations. Keep the M1 sample calculation, static binding,
single effect domain and single active operation. Add failure controls to the fixture,
not a generic fault engine or another Core scheduler. M3 still owns provider
replacement, restart and recovery from contradictory evidence.

The caller must be able to distinguish a failed request from an operation whose
effects may still continue. Stopping future result acceptance and releasing the
domain are separate decisions: revoked permission stays revoked even while the
old operation occupies the domain.

### Increment order

| Slice | Behavior to implement together with its example and tests |
|---|---|
| M2a: rejection and failure | Close an admitted operation that never reached native execution; report explicit native rejection or failure after acceptance; resolve undelivered output and require actual cleanup before another request |
| M2b: cancellation | Request cancellation by operation identity, revoke future dispatch/output permission, observe native ACK separately from terminal outcome, and keep the domain occupied until settlement |
| M2c: deadline | Add an optional absolute deadline in the host clock domain; expiry uses the same revocation and native-stop path as cancellation, with a distinct recorded reason |

Each slice preserves M1 behavior and gets its own focused review and macOS/Ubuntu
validation when implemented. Failure closure comes first because cancel and
expiry also need a truthful way to finish without a successful delivered result.
This avoids adding one generic `done` flag or a separate release rule per command.

### Facts required for closure

Extend the receipt only with facts these paths need: definitive non-submission or
native rejection; a cancellation request and its ACK disposition; deadline and
observed expiry; native cancellation when actually reported; resolved output
without acceptance; and revocation distinct from domain release. Exact C++ names
remain provisional. Preserve known native outcomes and already accepted results.

Invalid arguments remain a pre-admission rejection with no allocated operation.
A local dispatch refusal after admission needs correlated adapter evidence that
no work was submitted and no retained request can be dispatched later. Explicit
native rejection must identify the attempted submission and establish that the
native owner did not accept work; a lost response does not establish rejection.
Neither case may invent a native success or cancellation event to release a slot.

For an accepted operation, release requires a consistent native terminal fact,
resolved result disposition, and matching settlement evidence. A result may be
accepted, or definitively not delivered after the adapter has discarded/fenced
all remaining delivery paths. Failed sink acceptance must be recorded truthfully;
it does not turn native success into native failure. A flag saying permission was
revoked is not evidence that cleanup or output disposal occurred.

For a proven non-submission or rejection, equivalent evidence must establish no
remaining native work, deferred dispatch or output obligation. In both paths,
settlement covers the existing `worker-callbacks-and-managed-result` scope:
native payload use has ended and remaining callbacks/results have been handled
so no old managed result can subsequently commit. Check source, operation,
binding, sequence and causal observation times against the relevant prerequisites.
The adapter supplies observations; Core makes the release decision. New work
still requires an open host and available worker/sink at actual dispatch.

Missing terminal or cleanup evidence leaves the relevant facts pending/unknown
and conflicting admission blocked. Later valid evidence for the same operation
can finish delayed closure; it cannot re-enable revoked output. Contradictory
valid terminal facts retain the M1 unknown/block behavior and require later M3
recovery, not an M2 reset or a synthetic settlement. Task verdict remains
unassessed on every path.

### Cancellation and ordering

The host serializes a valid cancellation transition before asking the adapter to
stop native work. That transition prevents any later dispatch or managed result
acceptance for the operation. The adapter attempts native cancellation at most
once for repeated requests; ACK, refusal and unavailable acknowledgement remain
separate observations and none releases the domain. A native terminal fact plus
actual cleanup may establish settlement even if no ACK was received.

Cancellation before actual dispatch prevents submission and uses proven
non-submission closure. Cancellation after acceptance may complete cooperatively
or the worker may ignore it and finish naturally. In the latter case record the
real native success/failure, reject its later result at the sink, and wait for
cleanup. Native success after a cancel request is not a contradictory terminal
fact: the request never claimed the native work had stopped.

If a result is staged but not accepted, cancellation wins when its transition
occurs first in the host sequence; check current permission at actual sink
acceptance. If the sink accepted the result first, retain that fact and payload:
cancellation cannot roll it back. If cleanup is still pending, the domain stays
occupied. Unknown/stale operation commands do not affect the current operation;
commands after release report that no active cancellation is needed. Repeated
commands do not dispatch work or issue duplicate native stops.

`shutdown()` retains normal finite draining as its default, without implicitly
requesting cancellation. An explicitly revoked operation remains revoked during
draining: discard its pending result, retain targets until native use and callbacks
end, then close. Destruction alone cannot manufacture missing settlement evidence.

### Deadline meaning

A deadline bounds authorization through operation closure, including result
acceptance and settlement; it is not a real-time stopping guarantee. With no
deadline, existing M1 behavior is unchanged. A request already expired at admission
is rejected without native submission. At `now >= deadline`, an active operation
expires; an already released operation is not retroactively expired.

Core stays passive. The host explicitly supplies current time and evaluates expiry
before actual dispatch, result acceptance, event processing and new admission;
an explicit time-advance/poll entry lets an idle host observe expiry. Reading an
old receipt alone does not run a timer. Tests use a manually controlled monotonic
clock, not sleeping or wall-clock timing assumptions.

Once expiry is observed, the host uses the same one-time native-stop path as
cancellation. Preserve both reasons if cancellation and expiry occur, without
repeated stop requests. A completion staged before the deadline but delivered
after it cannot restore output permission. Output accepted before expiry remains
accepted, while expiry during pending cleanup does not erase native success or
release the domain. If synchronous computation occupies the host across the
deadline, it cannot be preempted by this design; the subsequent boundary check
must reject late delivery and drain cleanup truthfully.

The implementation and verification entry points are the existing Core and
fixture headers/sources, normal example and tests. The
[M2 planned checks](TESTING.md#m2-planned-checks) define observable acceptance;
no thread pool, retry policy, transport, ROS dependency or new evidence archive is
needed for these slices.

## Product feedback and later expansion

A normal action loop proves usability but does not establish the differentiating
value of Harness. M2/M3 must show why a caller benefits from truthful cancellation,
unknown outcomes and safe handoff at the declared boundary. M4 checks whether that
logic is actually reusable outside the first fixture.

A separate Robot Agent can consume M1's invocation/events/results for restricted
normal tasks and M2's cancellation feedback when available. It need not wait for
M4. Model selection, prompts, task sequencing, re-observation and retry policy
remain in that project; Harness must also work with the deterministic caller.
Select any language bridge from the actual consumer need, not from a speculative
SDK matrix. Adding a model loop or real physical verifier is separate task scope.

Verification and feedback grow from this execution loop:
request → execution → evidence → next decision. Argument checking belongs to the
adapter, system/authority checks to Core, native safety to the controller, and
progress/task judgments to scoped evaluators and the caller. A physical verifier
is not required for M1, and Core must not implement all five proposed verifier
layers. Offline policy search requires a useful task loop and an independently
specified outcome first; it is not a prerequisite for P1.

After a real consumer and target are available, evaluate one complete task using
fixed tasks/skills/models and an equivalent-guarantee baseline. Observe task
completion, erroneous effects, unnecessary blocks, recovery and integration work.
The first demo is not proof of task benefit, performance or cost reduction. Revisit
the SDK scope if native integration already gives the same behavior just as simply.

## Verification discipline

Use the [testing environment split](TESTING.md): local macOS plus Ubuntu CI for
Core correctness, Ubuntu for ROS integration, and target deployment hardware for
timing and physical effects. CI coverage grows with M1–M3 tests; the historical M0
result only checks the build and placeholder library call.

Use focused checks for meaningful state branches and adapter boundaries. Do not
copy the historical experiment artifact system into product tests. No new
project-computed digest is needed for ordinary local development. The applicable
repository standard is the [D-068 product application](standards/MINIMAL_INTEGRITY_STANDARD.md).
Historical qualification procedures remain in the research workspace.
