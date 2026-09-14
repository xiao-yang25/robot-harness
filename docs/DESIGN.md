# Initial design and implementation scope

This is the implementation-facing summary of the research workspace's accepted
D-073 architecture and September 10 product split. It does not introduce another
acceptance contract. M0 is a build skeleton; the behavior below is to be implemented.

## Product boundary

Robot Harness is independently usable by agents, behavior trees, and deterministic
applications. A separate Robot Agent project owns model selection, prompts,
context, goals, and task-level recovery. Dependency flows from Agent to Harness.

The small core is one component of the Harness product. Reusable capability
integration and feedback may grow around it when actual consumers need them.

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

## Implementation sequence

1. **M0:** standalone CMake/C++17 skeleton and smoke test.
2. **M1:** passive state transitions for clean admission, stale generation,
   expired deadline, conflicting domains, terminal/settlement separation, and
   restart recovery.
3. **M2:** deterministic native adapter with held late outputs, cancellation
   acknowledgement, settlement evidence, partial effects, and fresh recovery.
4. **M3:** ROS 2 Humble lifecycle/action adapter; keep the core ROS-independent
   and avoid duplicating its authority decisions.
5. **M4:** focused regression across the two adapter paths, covering normal
   execution, unavailable providers, old completion, restart, cancellation,
   partial effects, and recovery.

Concrete C++ types remain internal while implementation settles. Agent integration
can start when the minimal invocation/cancellation/result interface is usable;
it need not wait for every milestone or a performance campaign.

Production performance, real physical task outcomes, and integration economics
require their own relevant evidence. The initial prototype does not claim them.

## Verification discipline

Use focused checks for meaningful state branches and adapter boundaries. Do not
copy the historical experiment artifact system into product tests. No new
project-computed digest is needed for ordinary local development. The applicable
repository standard is [D-068](standards/MINIMAL_INTEGRITY_STANDARD.md); its
historical qualification section documents a prior experiment, not product setup.
