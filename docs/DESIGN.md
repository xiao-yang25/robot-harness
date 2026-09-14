# Initial design and implementation scope

This is the implementation-facing summary of the research workspace's accepted
D-073 architecture and September 10 product split. The September 14 replanning
changes the implementation order to runnable behavior slices, preserving those
ownership boundaries. M0 established the build skeleton; M1 is now the active
implementation slice. M2–M4 remain planned. See README and Testing for implemented
status and the limits of actual validation.

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

## Product boundary

Robot Harness is independently usable by agents, behavior trees, and deterministic
applications. A separate Robot Agent project owns model selection, prompts,
context, goals, and task-level recovery. Dependency flows from Agent to Harness.

The small core is one component of the Harness product. Reusable capability
integration and feedback may grow around it when actual consumers need them.

P1 uses the `TRUSTED_EMBEDDED` profile: callers and adapters cooperate at the
declared boundary. An in-process library cannot isolate malicious code or prevent
direct native calls that bypass that boundary. An isolated gateway is later scope.

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
until native use has ended. On shutdown the host closes admission, requests any
supported stop, drains the required callbacks and releases native resources before
destroying their adapter targets. A provider that cannot establish cleanup must
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

The sample's `started` observation is emitted after its deterministic computation;
it records that execution occurred, not a precise execution-start timestamp. The
example clock advances an integer on each observation, so neither the event times
nor their differences establish execution latency. `shutdown()` closes admission
and drains finite pending work and callbacks while the sink is available, then
closes the adapter. This is normal draining, not cancellation.

For the current receipt, a newly correlated, source-valid conflicting terminal
observation remains a conflict even after settlement, until another operation is
admitted. After that next admission, evidence for the old operation is stale and
cannot modify the current operation. This does not implement durable receipt
history or recovery across restart; callers retain any historical receipt copies
they need.

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
