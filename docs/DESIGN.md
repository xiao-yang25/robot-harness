# Initial design and implementation scope

This is the implementation-facing summary of the research workspace's accepted
D-073 architecture and September 10 product split. The September 14 replanning
changes the implementation order to runnable behavior slices, preserving those
ownership boundaries. M0 established the build skeleton; M1 has been implemented
and merged. M2a implements failure closure; M2b/M2c add cancellation and expiry
with macOS and Ubuntu validation. M3a adds replacement within one live host/binding;
M3b adds provider rebinding within a live Host and a finite task caller.
M3c adds a private Linux Host-recovery prototype with the same task caller;
M4 remains planned. See README and Testing for tested status
and validation limits.

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

### Source modules and public headers

Implementation directories follow the existing build targets and responsibilities:

| Location | Responsibility / dependency |
|---|---|
| `src/core/` | Payload-independent authority and receipt state; no compute, fixture or OS dependency |
| `src/compute/` | Local compute host, private native process owner, private worker protocol and worker executable; the host depends on Core |
| `src/sample/` | Deterministic sample fixture and its host; depends on Core, not on compute |
| `examples/` | Applications consuming public APIs, including the finite task caller |
| `tests/` | Behavioral checks and controlled workers; only process/protocol tests receive the private compute include directory |

The three public entry headers stay directly under `include/robot_harness/`:
`authority_gate.hpp`, `compute_execution.hpp`, and `sample_execution.hpp`.
The host headers depend on the Core header; Core does not depend on either host.
Private process/protocol headers stay with their implementation and are not
propagated to library consumers. Public include paths, symbols, CMake target names
and executable names are unchanged by this source organization.

This grouping makes the existing module boundaries visible without adding runtime
layers. There is no current need for a directory per public header, forwarding
headers, one CMake file per source directory, or a common adapter base class.
Revisit public subdirectories when a module acquires several independently useful
headers or an actual consumer requires a distinct packaging boundary. Splitting
the authority implementation itself should follow a separate responsibility or
invariant boundary, not file length alone.

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

These slices allocate the existing M3 requirements. M3a alone did not implement
them; M3b now adds live-gate rebinding in the sample and compute hosts.
The example task caller now exercises those public interfaces. M3c has a separate
private Linux prototype below, connected through an example-local execution port.
Initial generation fields and fresh initialization do not establish restart recovery.

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

### M3c first recovery slice: design and experiment boundary

Status: a private Linux recovery prototype is implemented alongside the existing
adapter; it is not a stable recovery API or production supervisor. M3b is merged
at `eb2ae06`. The first case remains finite local computation, with one result
domain and at most one outstanding worker.

The failure under study is abrupt loss of the process containing Host and Core
while a worker is running or has emitted its terminal result before native cleanup. A new
Host must not treat its empty memory as proof that the old execution scope is
clear. Recovery of permission does not recover the caller's goal or prove its
old result was durably consumed.

The current `ComputeExecutionHost` deliberately owns an isolated, process-local
scope. Its normal worker exits on control EOF or failed required output, but its
Host owns the child wait relationship, result memory and channel endpoints.
Constructing and initializing another current Host creates another isolated
scope; it is not an existing recovery API or a defect that it can initialize.
Process-local identity counters and the diagnostic child PID are not recovery
credentials. A dead Host's destructor does not run. Current `finish()` publishes a result only
after collected exit and event EOF; worker emission is not public result acceptance.

**Selected prototype.** Separate the finite compute native owner from the
restartable Host/Core process. The owner directly launches, stops and reaps its
workers, retains bounded operation/output-disposition facts, and owns the worker
channels for one scope. It stays alive across the tested Host crash. The new
Host/Core receives observations through a newly established local session;
Core retains the execution-permission rules. The owner enforces the current
session at the native submission and result boundaries, rather than duplicating
Core's receipt/state machine. This is an adapter process boundary, not a generic
scheduler, fleet service or second task planner. The current in-process adapter
continues to expose its existing weaker profile.

A trusted local test launcher provisions one owner and serial Host sessions for
that scope. The first prototype uses dedicated local channels with no externally
reachable service or reconnect-by-PID interface. Replacing the Host requires
confirmed death/closure of the previous session; two live Hosts are not allowed
to share authority. Parent/child ownership and channel installation, rather than
a user-supplied identifier, establish which owner is being contacted.

| Concern | Required behavior of the prototype |
|---|---|
| Session loss | Owner fences the lost session's submissions and future result delivery, requests stop, and continues native observation/cleanup independently of Host polling. This adds no hard stopping-time guarantee |
| Restart identity | Owner allocates a never-reused session epoch during its lifetime; bind the new Gate's identity and all commands/replies to this epoch and dedicated channel. Old operation IDs, authority copies and buffered old replies cannot acquire the new session's permission |
| Old work | Keep the old worker identity and native closure/output obligations until actually resolved. A worker exit is distinct from required output disposal and channel cleanup; missing or contradictory facts keep recovery blocked |
| Fresh observations | Request a fresh, correlated snapshot after session loss/fencing, with owner ordering and a new-session request identifier. Use new-Host local observation/expiry bounds; never compare a stored old-Host monotonic timestamp to a new time origin. Recheck the owner's current session and closed scope at activation |
| Activation | Begin new Core in recovery-required state. Prepare new identity/readiness and complete old-scope validation before enabling execution. Interrupted recovery cannot expose partial authority. Owner checks session validity at the real dispatch boundary even if a prior ready reply was delayed |
| Old result | Preserve native outcome separately from delivery. Discard output still owned by the lost session; output already handed to the dead Host does not prove caller consumption. Report unrecoverable task outcome truthfully and do not replay or retry automatically |
| Owner failure | No recovery if the owner/session connection is lost or its identity is inconsistent. A fresh owner is not a substitute for closure of the previous scope; owner restart, OS reboot and durable recovery are outside this first slice |

The owner session epoch is scoped to that owner lifetime, not a globally unique
or durable identity. Supported recovery never replaces the owner. If the owner
is lost, the launcher must refuse to start a replacement for the same scope;
only separately established teardown of all old work could justify reprovisioning,
which this slice does not implement. No fallback to today's isolated Host may
be advertised as recovery of that scope.

**Alternatives and cost.** Relying only on worker EOF is smaller but leaves no
surviving collector of old closure and output facts. A PID file or saved receipt
cannot restore wait ownership and may refer to stale work. Restarting an entire
externally supervised execution unit is a valid alternative if that supervisor
can prove the whole scope has been torn down; it does not support observing a
surviving worker during Host-only recovery. The selected owner process costs one
extra process and a bounded local protocol, and permits that observation directly.
Do not implement both strategies in this slice. Revisit the choice if a target
runtime already supplies an adequate owner or deployment only needs unit teardown.

The concrete protocol and activation ordering are specified below. Current
`AuthorityGate::observe_startup` becomes Ready once its three positive facts are
present; feeding it recovery observations one by one is not a recovery commit.
The prototype uses existing staged rebinding checks instead. Public API names,
serialization across versions, a durable journal and receipt replay are not frozen
by this design. No parallel semantic evaluator or digest registry is introduced.

The first research experiment kills a real Host with a live worker suspended
after partial progress was observed, and with a fixture that confirms terminal
emission before suspension. The first case does not establish the exact iteration
or absence of a terminal frame at the instant of suspension. A Linux test subreaper collects orphaned workers
so the experiment can finish cleanly; that instrumentation does not implement the
selected direct-owner design. Controlled suspension can hold the cut point; it
must not be described as a spontaneous leak or measured stop latency. The product
prototype independently exercises the [M3c acceptance path](TESTING.md#m3c-first-slice-validation)
with its own actual owner, restarted Host and real worker.

### M3c prototype session and activation ordering

The first implementation is a private Linux-only prototype, not a stable public
adapter. A trusted launcher creates one owner and serial Host processes. Dedicated
Unix sequenced-packet socket pairs carry fixed-size, same-build messages; no
listener, PID-based attachment, cross-machine protocol or persistent journal is
introduced. The launcher duplicates both child channel sources above the fixed
child destinations before mapping, so inherited descriptor numbers cannot cause
one mapping to overwrite the other source. Untransferred endpoints have scoped
ownership through setup/spawn failure. Both ends process bounded messages without
blocking. Each end holds
at most one unsent message; outbound backpressure expires and closes the session.
While polled, Client also expires incomplete handshakes and submission acceptance.
Owner does not time out a silent connected Host after flushing its reply; there
is no heartbeat or hung-Host detector. Native execution itself has no protocol
completion deadline.
The owner continues worker cleanup while the Host is absent. Its launcher must
keep polling; owner failure or a stalled launcher is not independently supervised.

The owner allocates session epochs starting at 2, never reusing one during its
lifetime; 1 is the initial empty-scope marker. A new Host creates a fresh Gate
configured with the preceding session identity in that owner's scope and withdraws
it before any startup evidence. This does not restore an old receipt: the owner
must independently observe closure of all work left in the scope, including work
from earlier abandoned sessions. Old task outcome remains unavailable.

The exchange is Hello → Query → Snapshot → Activate → Activated. Every response
matches the dedicated channel, current session and a monotonically increasing
request ID. A clear Snapshot requires collected native exit, event EOF and closed
worker channels, no unresolved ownership/protocol/cleanup fault, and current
launch prerequisites. The snapshot request ID is a one-use activation token.
Owner checks its local 1000ms expiry, current session and scope again at Activate;
Host independently checks freshness from its own Query issuance time. No timestamp
is transferred between clocks. A blocked snapshot grants nothing and may be
queried again. Malformed, contradictory, wrong-session or expired activation closes
the session; it does not silently restart recovery or allocate another owner.

Only after a complete clear Snapshot does Host call existing `begin_rebind` with
the owner-attested old-scope evidence. Core remains Preparing. Activated reattests
current scope, session, launch prerequisites and the new result channel after
candidate preparation. Only that matching fresh response supplies candidate
readiness and permits `commit_rebind`;
ordinary `observe_startup` is not used. New generation equals the owner epoch.
This reuses Core's staged validation rather than adding a reset or assuming an
empty new Gate proves old quiescence. An activation interrupted at either end
cannot generate a valid new submission. Disconnect permanently closes that Host's
Gate and fences its session at the owner.

After activation, Core admits and claims dispatch. Owner accepts Submit only for
the active session, a fresh request/operation ID, a bounded workload and a clear
native slot. Accepted means successful spawn. Result is sent only after native
exit, event EOF and channel cleanup; Host correlates it and verifies the expected
sum before recording native outcome, output and settlement. A native launch
failure is reported separately and cannot masquerade as a running worker.

Session loss cancels pending output delivery, requests stop and continues polling
the real owned child. A terminal result retained for a dead session is discarded,
never delivered to a replacement Host. Owner-side output facts establish disposal
or transport handoff, not caller consumption. Prototype commands do not yet offer
a general task API, explicit caller cancellation/deadlines or durable result replay;
the existing adapter retains its implemented API and profile. Owner or launcher
loss is outside supported recovery and cannot authorize reprovisioning the scope.

### M3c task-caller integration

The same example-owned `FiniteComputeTask` uses two execution paths: the
existing public compute Host and the private Linux recovery Client. An
example-local `TaskExecution` port is used for the operations actually consumed by
that controller. The existing Host constructor remains usable through a local
bridge; the Linux bridge lives in the example layer and consumes the private
prototype. No Core or installed public API changes, capability-profile equivalence,
new scheduler, durable task journal or automatic retry are implied.

A trusted surviving launcher remembers that a prior goal was interrupted. It
supplies that intent to the restarted task via `note_interrupted_goal`, which
records NeedsAttention with no invented operation receipt or recovered result.
This controller cannot accept goal revisions or advance either step, even after
execution authority recovers. Only a separate explicit caller request starts a
fresh controller/goal after old-scope closure. The old task's outcome remains
unknown. Remembering intent in this launcher is not durable recovery or proof
that the previous result was or was not consumed.

The recovery bridge correlates the current session's authority and result with
local goal/step metadata. The second input still comes from the accepted and
settled first result and the existing independent application arithmetic check.
Client loss stops progression. This prototype has no per-operation cancellation
message: interruption closes the session, allowing Owner to stop/clean up, but
reports no operation cancel acknowledgement or confirmed local cleanup. The
launcher must keep driving Owner. The original Host keeps its existing richer
cancellation and provider-rebinding behavior.

An alternative was a second recovery-only task state machine, which would copy
the dependency and unknown-outcome policy; the port instead keeps that policy in
one controller. Acceptance includes original caller regressions, a normal
recovery-backed two-step task, actual Host loss during old execution and between
steps, no replay while awaiting explicit new intent, and owner-channel loss.

### M3b implementation design: live-host binding handoff

Status: Core transitions and the sample and compute host handoffs are implemented.
The two-step task caller is described below. Keep
one passive Core, one host writer, one conservative effect domain and at most one
operation. M3b changes the provider incarnation within that domain, not the host
process, effect scope, capability semantics or trusted policy. Host/Core crash,
unresolved contradictory outcomes and cross-process identity reconstruction remain
M3c work. A missing old closure report therefore blocks M3b indefinitely rather
than being repaired by rebinding.

**Keep the old owner until closure.** `close_admission()` remains irreversible
shutdown. Withdrawal is a separate reversible binding lifecycle, but withdrawal
of an individual operation's permission is irreversible. Preserve the old receipt,
native owner, event correlation and cleanup path until their existing closure
predicate is satisfied. Never replace the gate or call initial `initialize()` to
forget those obligations. With no admitted operation, the trusted adapter must
still establish old-scope quiescence before preparing a replacement.

The repository-local control surface is deliberately small. Return types
distinguish applied, already in that phase, stale identity, unresolved
old work, invalid evidence and permanently closed without granting authority on
failure:

| Gate operation | Preconditions and effect |
|---|---|
| `withdraw_binding(expected_binding, now)` | In Active, match the active identity, withdraw permission and return any one-time stop action with its original operation authority. In Preparing, match only the candidate identity, abort that candidate and return to Withdrawn without another native stop. In Withdrawn, the old active identity is an idempotent no-op. Reject all other identities and backward times; Closed never reopens. No receipt means no synthetic operation or stop action. |
| `begin_rebind(expected_old_binding, new_provider_id, old_scope_evidence, now)` | Require withdrawn phase, old closure, fresh old-scope quiescence evidence and no candidate. Reserve a fresh candidate identity, clear only candidate readiness/capabilities, and return the candidate. It grants no execution permission. |
| `observe_rebind_readiness(evidence)` | Accept observations only for that candidate, its configured sources and selected readiness kind. Record the latest per-kind value, ordering and validity; never update the old receipt from candidate evidence. |
| `commit_rebind(expected_candidate, now)` | Recheck old closure and all candidate facts, then activate the candidate in one serialized transition. Invalid or incomplete evidence leaves admission closed and the old obligations intact. No operation is automatically submitted. |

Expose the binding phase, active identity, optional candidate identity and readiness
through a small status query. Keep an old receipt queryable until the next operation
is admitted, consistent with the current single-receipt surface; receipt copies
remain the caller's responsibility. The phase view is distinct from the old
operation's native outcome and settlement. Active means an installed binding, not
permission by itself: initial startup readiness and execution requirements still
apply. After a rebind commits, the retained old receipt is read-only; old-binding
callbacks cannot mutate it or the new binding, even before a new operation exists.

| Phase/event | Result |
|---|---|
| Active → withdrawal | Withdrawn; old work may still run, with its output permission revoked |
| Withdrawn + unresolved old work | Remain withdrawn; accept valid old terminal/output-disposal/settlement observations only for their original operation |
| Withdrawn + old closure → begin | Preparing; candidate readiness starts empty, admission remains blocked |
| Preparing + valid fresh observations → commit | Active with new identity; a later caller submission receives a new operation authority |
| Preparing → withdraw the expected candidate | Discard candidate readiness, retire its identity, return to withdrawn; no return to old execution authority |
| Any phase → shutdown | Permanently closed; continue required old cleanup, reject begin/commit forever |

An old-binding withdrawal cannot abort a newer candidate or a committed new
binding. Repeated begin while preparing returns the existing phase without
replacing the candidate or resetting its time origin; a different target requires
explicitly discarding the current candidate. Repeated successful commit must not
allocate another identity or reset readiness. Rejected control requests must not
implicitly change the active/candidate selection.

**Identity and evidence.** Core reserves each candidate's provider generation
from a monotonic high-water mark within the live gate, including discarded
candidates. Rebinding back to a previously used provider name still receives a
new generation. Keep the same effect domain and composition revision in this
slice, because its dependency topology is unchanged; changing the topology or
enforcement scope needs a separate design. Reject generation exhaustion instead
of wrapping. Operation IDs also continue across rebinding; none are reset.

Candidate readiness uses the existing binding-idle/settled, configured worker-or-
adapter-ready, and result-sink-ready concerns. Add validity to the candidate
observations: candidate identity, concern, boolean value, configured-source
`EvidenceRecord`, and exclusive `valid_until`. Observations must be generated
after candidate preparation, ordered within each concern and satisfy
`prepared_at <= observed_at <= now < valid_until` at commit. Equal-sequence
identical duplicates are harmless; a conflicting duplicate cannot retain a usable
positive fact. Missing/false/expired or contradictory concerns block commit;
only an appropriately newer valid observation can resolve that concern. No fixed
TTL is prescribed by Core. Old-binding observations, or those for an abandoned
candidate, cannot populate the new candidate even if their source names match.

Old-scope settlement is a separate prerequisite: a candidate's idle observation
does not close old work. `old_scope_evidence` is a positive old-binding idle/settled
observation from the configured binding observer, with its record and exclusive
validity end. It is required even with no prior operation. Core validates its
identity/source, order against earlier same-concern facts, and observation time
at or after withdrawal and any required receipt closure, no later than begin.
Its validity must cover both begin and commit; refresh it by discarding the
candidate and beginning again if it expires. A current old receipt must separately
satisfy its existing release predicate. An initial startup-idle fact cannot stand
in for this post-withdrawal observation. The host keeps the old scope quiescent
and reports any new outstanding work by aborting preparation; candidate setup
has no execution authority and cannot introduce task effects into that scope.

Where the existing capability profile is required, extend
`observe_execution_capabilities` to route by binding identity: while Preparing,
candidate observations populate a separate candidate slot; any valid old-binding
update stays separate and cannot make the candidate ready. Keep the existing
profile/work-limit fields, configured observer and sequence checks. For candidate
capabilities, a same-source/same-sequence conflicting value poisons the slot for
commit instead of merely rejecting the update and preserving a positive. Only a
strictly newer consistent observation can resolve it. Wrong-source/identity or
older observations are rejected without poisoning a valid slot. Require capability
observations from the candidate's preparation context and valid supported profile
at commit, then install the candidate slot as the active capabilities together
with the binding. Never copy old capabilities, use a task request to loosen policy,
or alter active-profile duplicate semantics as an incidental refactor.
Keep observer identities, capability ID, profile policy and settlement scope fixed
by the trusted host. A caller supplies neither a safety boolean nor a replacement
policy. Apply the existing time/sequence validation and reject backward control
times; do not restart a monotonic clock or receipt evidence stream during M3b.

The validity interval above gates the rebind commit; it is not a new independent
execution lease or stopping-time guarantee. After commit the host still checks
native prerequisites at prepare/dispatch and while driving work, maps an observed
required dependency loss to withdrawal, and rechecks execution capabilities at
the existing boundaries. Recovery of a dependency alone cannot restore a withdrawn
binding. Core observes reported facts; it does not detect native failure itself.

**Host integration.** Withdrawal revocation precedes the native stop request. Add
a distinct binding-withdrawal reason to operation feedback rather than reporting
it as user cancellation; preserve cancellation and expiry when they also occur.
Reuse the one-time stop path, non-submission closure and native cleanup logic.
Prepared-but-undispatched requests close as proven non-submissions; accepted work
continues to be observed until closure. Already delivered results remain facts.
Include the withdrawal reason consistently in permission, output non-delivery,
stop-once and causal-time checks; updating only the visible receipt is insufficient.

Core prepares the returned stop authority before mutating withdrawal or receipt
state. If that copy cannot allocate, the gate remains unchanged and the caller
can retry; it must not record a stop that was never returned to the host. Once
withdrawal succeeds, repeated withdrawal/cancellation does not emit a second stop.

Prepare replacement adapter resources and all fallible setup before committing
Core; install the prepared owner using a non-throwing host transition before any
new submit/result processing. Setup failure leaves the binding blocked and cleans
only the candidate's resources. Old callbacks keep captured old identities and
never read a mutable current-binding field to identify their originating work.

The sample host replaces a quiescent provider fixture, retaining only explicitly
scoped completed-result copies for stale-delivery tests. The compute host replaces
its provider binding/configuration only after the old child is reaped and channels
are closed; trusted executable selection stays outside task requests. Its result
domain stays stable within the host. This needs a real rebind of the same gate,
not construction of a second independent compute host/domain. Do not persist
process-local generations or claim host-crash recovery.

The sample exposes `withdraw_binding(expected_binding)` and
`rebind_provider(expected_old_binding, provider_id, completion_mode)`. The latter
performs the begin/observe/commit sequence synchronously from trusted native fixture
observations, after old work, staged callbacks and the receipt have all closed.
Its facts are valid for that single serialized handoff time; no task executes while
preparing. It creates a new adapter, then swaps ownership without throwing after
Core commits. An invalid provider or failed preparation leaves admission blocked.
The managed result history, cumulative counters and explicitly retained completed
callback copy move to the new fixture; pending work and failure/stop controls do
not. References returned by `results()` must be reacquired after rebinding.

After initial readiness, a sample `poll()` observes worker/sink loss and withdraws
the binding. It is also called at prepare, dispatch and event-processing boundaries.
Restored availability or `initialize()` alone cannot undo that withdrawal. Initial
startup observations before first readiness still use the existing M1 flow.
The failure tests now prepare before inducing dependency loss to retain their
non-submission checks; they explicitly rebind before submitting the next request.
Observed sink loss revokes result authority, so the sample records
`kAuthorityRevoked` while preserving any native success. Core still supports an
actual sink refusal without withdrawal as `kSinkRejected`.

The compute host exposes the same binding query/withdrawal controls and
`rebind_provider(expected_old_binding, provider_id, worker_executable)`. The worker
path is trusted same-build deployment configuration; a request cannot supply it.
The host's work ceiling, stop grace, profile, observers and effect domain stay fixed.
Preparation checks both the released Core receipt and native ownership: a launched
old child must have a recorded reaped exit, event EOF and closed channels, without
lost ownership. A failed launch needs known setup cleanup. Mapping/cleanup gaps
keep rebinding blocked. Withdrawal before dispatch records non-submission through
the existing closure path; running withdrawal requests the existing one-time stop.

Compute rebinding performs a bounded single-candidate begin/observe/commit sequence,
with fresh adapter/capability observations from `can_launch()` and an old-scope
quiescence observation. Their 1000 ms validity uses the existing capability horizon;
commit reads the current clock and rejects expired evidence. This is an observation
validity rule, not a runtime or stopping-time bound. Readiness failure aborts the
candidate and retires its identity without changing the active configuration.
The new path is allocated before commit and installed by a nonthrowing swap.
No child is spawned by rebind: the next admitted operation launches the selected
worker, and actual launch may still fail after a successful preflight. Rebinding
retains the previous result, receipt and process observation until the next
admitted request, which releases the already closed process object. `observation()`
therefore describes that historical child, not a newly launched candidate.

Compute checks active deployment capability at initialize, prepare, dispatch and
poll, including idle polling after first readiness. Observed loss withdraws the
binding and retains a distinct receipt reason; restoring the executable path does
not reopen it. Initial failed startup can still be retried before first readiness.
Checks remain host-driven and do not identify or authenticate executable bytes;
trusted deployment and same-build protocol compatibility remain requirements.

Core, sample and compute handoffs are implemented. The example task caller below
uses the public interfaces without a mission framework or new default threads.

Rejected alternatives: rebuilding a gate loses old obligations and can reuse
identities; reopening `close_admission()` breaks shutdown; replacing a provider
by editing generation fields bypasses readiness; accepting a one-shot readiness
bundle without ordered observations can forget a newer negative fact. One bounded
candidate is sufficient here; no general registry, queue or transaction framework
is needed. Revisit this design if multiple in-flight domains, mutable trust policy
or survival across host failure becomes necessary.

### Finite two-step compute caller

The example-owned [FiniteComputeTask](../examples/finite_compute_task.hpp) borrows
one initialized compute host for exclusive, serialized use. The host outlives it.
The [driver](../examples/two_step_compute.cpp) keeps ticking throughout operation,
failure, uncertain feedback and shutdown. No model call may block that responsibility.
This is application logic, not a new public Harness API or a durable mission engine.

Each goal has an increasing revision and a bounded first input. Each submitted
operation retains its goal revision, step, request reference and authority.
The first input `3` gives `5`; the second input is `first_sum % 8 + 1`, giving
`6` and then `55`. Every result is checked against a direct bounded sum of one
1000-value period, independently of the worker accumulation and host formula.
A dependent step requires native success, accepted/correlated output and released
authority. The application reports task success only after both steps pass;
Core's domain verdict stays unassessed.

Changing the goal retains only the latest intent, cancels the old operation and
waits for its authority release before submitting. Old results never become new
goal inputs. Repeated revisions do not extend the outstanding observation window.
The deployment driver may explicitly withdraw and rebind the provider while the
caller waits; provider selection and trusted launch configuration stay outside
task strategy. No automatic retry follows execution failure or renewed readiness.
An explicit newer goal may replace a known failed goal after old cleanup.

While a goal is pending, unresolved result/cleanup past the caller's observation
window, a missing receipt,
unknown native outcome, blocked-unknown authority, or ownership loss stops task
progression as `NeedsAttention`. This state rejects further goal revisions and
keeps polling/cancelling the retained operation. The window is measured by an
injected nondecreasing caller clock; it neither changes the host clock nor promises
native stopping by that time. The example's controlled unknown case advances this
clock while a real fixture child delays exit. It proves conservative caller behavior,
not a native-unknown, lost-reaper or host-crash recovery path. Shutdown separately
cancels pending intent and drives the host until its native obligations close.

The caller directly uses the existing host because this slice has one capability
and no demonstrated need for an abstraction shared across backends. A generic
scheduler, copied Core state machine, implicit retry policy, persistence layer,
and new threads would add unsupported behavior here. Revisit only when a concrete
consumer requires them; full Agent strategy remains a separate project.

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
