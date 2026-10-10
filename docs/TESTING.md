# Testing environments

Use macOS for fast local Core development and Ubuntu for Linux build and
correctness coverage. Robot integration and deployment measurements require
their relevant target environments.

## Current validation baseline

The closeout runtime baseline is `1a80291`, merged through
[PR #24](https://github.com/xiao-yang25/robot-harness/pull/24). Its post-merge
[Core CI](https://github.com/xiao-yang25/robot-harness/actions/runs/36297967494)
passed 57 Linux tests in Debug and ASan/UBSan, plus the installed, relocated Core
consumer. Its [Humble CI](https://github.com/xiao-yang25/robot-harness/actions/runs/36297967493)
built both optional binaries and passed six Owner checks and 40 Python tests.
[Documentation CI](https://github.com/xiao-yang25/robot-harness/actions/runs/36297967511)
passed six tooling tests, checked 20 generated pages and deployed the site.
Automatic Humble CI does not run Gazebo.

The separate [clean-source visual handoff](https://github.com/xiao-yang25/robot-harness/actions/runs/36297578136)
validated the reviewed `27dd259` tree, identical to the merged runtime baseline:
A moved about 0.553 m and B 1.199 m; 56 old-A commands were rejected while B moved.
A settled before B admission; B remained pending and third admission was blocked.
The run removed its container and decoded the complete MP4. The earlier clean
build failed on an omitted CMake template; the final revision includes both the
Docker COPY and build-context fix. It does not turn earlier failed runs into passes.

The sections below retain older dated evidence, including M3c at `dabad9b`
([PR #7](https://github.com/xiao-yang25/robot-harness/pull/7)). Historical counts
and results do not replace checks for the revision being changed.

| Environment | Current coverage | Limits |
|---|---|---|
| GitHub-hosted Ubuntu 22.04 x86_64, GCC | All 57 CTests in Debug and ASan/UBSan | No ROS, hardware or deployment-timing validation |
| Ubuntu 22.04 ARM64 in local Docker, GCC | All 57 Core CTests in Debug for the M4 second slice; earlier recovery-specific sanitizer results retained below | Runs in a Linux VM; not a target-device performance result |
| macOS ARM64, AppleClang | All 47 portable Debug CTests on `56dd7ef`; earlier ASan/UBSan results predate the Linux-only launcher correction | Compatible SDK selected explicitly; Linux recovery targets are not built; no hosted macOS CI job |
| Optional Ubuntu 22.04/Humble build | Both Nav2 binaries, six Owner checks and 40 Python checks at `1a80291`; separate Gazebo evidence below | Automatic CI compiles/tests predicates and launcher boundaries; full movement uses the separate manual simulation workflow |
| Other platforms/toolchains | No verified support claim | CMake platform branches alone are not platform validation |

For a first run use [Build](../README.md#build) and the
[example guide](../examples/README.md). The normal CTest command discovers all
tests built for that platform. Later revisions must carry their own CI results;
the links here establish only the recorded revision.

## M4 closeout

**Complete for the experimental `nav2-fixed-context-sequence-v1` simulation
scope, assessed on 2026-09-27.** The accepted [M4 exit](DESIGN.md#implementation-sequence)
is ROS/Core reuse with native observations agreeing with receipts, explicit
capability limits and a reproducible [robot demonstration](ROS2_INTEGRATION.md#user-facing-m4-outcome).
The applicability mapping below remains the detailed scope; completion does not
extend local-worker guarantees to unsupported ROS paths.

| Delivered requirement | Evidence and boundary |
|---|---|
| Normal motion and Core reuse | [Observation and sequencing checks](#m1-m3-applicability-to-the-ros-profile) cover submission guards, correlated native events, settlement and second admission. ROS uses the same Core gate. |
| Moving cancellation, replacement and unknown evidence | The mapping covers exact-goal cancellation, native closure, fresh quiet, old-generation rejection, missing-evidence denial, observation loss and reachable-drive isolation with an unresponsive navigator. Cancel-only remains unsettled. |
| Environment-driven waiting and bounded recovery | [PR #20](https://github.com/xiao-yang25/robot-harness/pull/20) adds obstacle/frozen-scan waiting; [PR #21](https://github.com/xiao-yang25/robot-harness/pull/21) adds fresh-clearance resume. [Normal resume](https://github.com/xiao-yang25/robot-harness/actions/runs/36287186100), [frozen scan](https://github.com/xiao-yang25/robot-harness/actions/runs/36287187762), [missing controller](https://github.com/xiao-yang25/robot-harness/actions/runs/36287189350), [pre-dispatch loss](https://github.com/xiao-yang25/robot-harness/actions/runs/36287190777) and [B observation loss](https://github.com/xiao-yang25/robot-harness/actions/runs/36287558508) passed from source on Ubuntu/Humble at `71d4812`. Later install/docs/recording changes do not replace those scenario-specific results. |
| Reproduction and presentation | The [source-built tutorial](../integrations/ros2/simulation/README.md), live viewer and [recordings with same-run receipts](assets/demo/README.md) are delivered. The current-baseline hosted run verifies clean-source visual replacement; it is not a fresh run of every scenario. |

Retained limits and follow-up triggers:

- Historical TF/query and B-context response failures remain unresolved. The
  [first hosted B-loss attempt](https://github.com/xiao-yang25/robot-harness/actions/runs/36287192390)
  failed during B readiness, before fault injection; it is not a loss-test pass.
  One unchanged-source repeat reached the intended boundary. These failures limit
  availability/reliability claims; observed refusal retained pending settlement.
  Further diagnosis needs a new failure or discriminating request/response evidence.
  They do not block the declared experimental behavior, but do block a reliability guarantee.
- Owner survival and polling, reachable drive control, a fresh exclusive deployment
  and the matching native fixture remain prerequisites. Owner death, full drive/link
  loss, hard stop bounds, durable recovery, arbitrary task loops and physical safety
  are not established. Missing evidence must continue to prevent fresh authority.
- Actual unfamiliar-contributor feedback, release
  packaging and stable API/ABI are separate adoption work. This milestone is not a
  versioned release or a claim of mature-project readiness.

Next integrations follow the [incremental embodiment/backend plan](DESIGN.md#incremental-embodiment-and-backend-coverage).
The [system integration review](DESIGN.md#system-integration-review-after-m4)
recommends qualifying a complete native task and the consumer/host boundary first;
it does not add implemented runtime capabilities or expand M4's validation claim.
The [Agent consumer](DESIGN.md#agent-consumer-and-repository-organization) needs
a real model/task run alongside deterministic tests. Existing Codex has local
research evidence below; cross-repository jobs apply once an independent Agent
project exists. Live model/simulator runs are not part of the current CI baseline.
Validate each declared robot/task/backend/controller combination separately;
simulation results do not transfer automatically to another simulator or hardware.
Physical integration starts with read-only interface checks and native protection
validation before controlled motion. The simulator drive fixture is not an
implemented hardware driver. Device availability does not block simulation work.

<a id="first-runtime-slice"></a>
## First Runtime slice: validation and remaining checks

The optional [Python session](../bindings/python/README.md) implements the bridge
and host loop in the [slice design](RUNTIME_SLICE.md). On macOS/Python3.12,
23 focused session/transport/Core tests pass: repeated operations and old receipts,
caller delay, queued cancellation, slow-worker expiry, worker loss/cleanup,
partial Core/socket/worker startup, reset failure, deadline-crossing output,
recording failure without settlement, and bounded JSON/RGB framing.
Three optional backend tests cover native float32 representability, failed-reset
observation invalidation, and recording failure while still freeing physics.
These backend tests require NumPy but no model, renderer or MuJoCo installation.
The previous bridge batch's47 existing macOS CTest cases and Core-only build passed;
this increment changes Python/integration paths, not Core source or headers.

Local ACT/MuJoCo integration used original seeds0–4, one surviving host/model,
explicit reset, actual RGB/joint transmission and retained old receipts. All five
runs reached the same limited native condition in258/260/272/262/306 steps;
action/qpos/qvel/control/contact traces matched the original baseline exactly.
All five640×480 videos decoded completely. Actual simulator checks also passed
for a3-step budget with failed native outcome/no successful output, cancellation
with queued actions and no steps after host revocation, and an exception after
an actual native effect with a retained partial trace/video. Host and worker
process reaping was observed. This is bounded local evidence, not reliability,
complete handoff, live Agent or Linux model/renderer qualification.

A later Linux consumer qualification used Ubuntu22.04/aarch64, Python3.12.14,
CPU ACT and OSMesa, Agent3a5a4d4 (including its Linux platform-lock extension),
and Harness3acc9aa. It used independent source copies, a new skill environment,
a fresh Harness build/install and read-only public originals migrated in Linux;
234 learned tensors were preserved. The public Agent Docker recipe passed the
seed0 deterministic400+50-step caller from outside source working directories.
It retained two correlated accepted/settled receipts, observations0/400/450 in
epoch0, stale-reference rejection, explicit reset and both process exits.
The same episode's451640×480 video frames fully decoded and the single fixed
physical evaluator passed its one-second holding window. This is one normal
Linux model/render/execution result; the visual Codex backend, complete business
Agent in Linux, CUDA/amd64, live counterexamples and task reliability remain
unqualified. Lightweight hosted CI is still separate. The
[pinned Linux consumer tutorial](https://github.com/xiao-yang25/robot-agent/blob/3a5a4d44d8404cbd5ffa47776219b5c7c197f090/skills/aloha/README.md#linux-consumer-path)
records the setup at that commit; a qualified local run does not imply a merged
Linux increment or a complete visual Agent qualification.

A separate installed Linux visual business-Agent run then used the same selected
CPU/OSMesa environment, Agent code delivered through PR2 (merged ascd2e763),
Harness3acc9aa and Codex0.159.0. Three real camera/joint proposals selected transfer,
hold and `observed_success`, bound to sequences0/400/450 and the same epoch0.
The task completed400+50 steps in75.476 seconds with two correlated delivered,
settled receipts. All451640×480 frames decoded; Host, worker and three decision
processes exited, with container process observations corroborating cleanup.
The single fixed evaluator on this run independently passed its one-second
hold. Agent/Core task verdict remains `unassessed`; physical truth never entered
model decisions. A final-decision model-list refresh timeout remains in diagnostic
logs; an unavailable optional Code Mode host failed closed in CLI events, with
no tool calls. The actual structured decisions completed, without an application retry.
This qualifies one normal Linux visual task, not live counterexamples, reliability,
CUDA/amd64, physical robots or a released support matrix. The selected normal
consumer scope is delivered through Agent PR3 (f19531f) and Harness PR30 (b3d93e7),
with merged trees, mainline CI and published documentation checked. The
[pinned visual consumer tutorial](https://github.com/xiao-yang25/robot-agent/blob/45c94dea5ea15aece6175c253cdd6586dd359821/skills/aloha/README.md#real-visual-agent-in-linux)
provides the setup at that documentation commit.

A later seed0 research counterexample changed only the cube free-joint pose and
velocity immediately before native step 400, relocating it to the tabletop.
A trusted Host-only fixture loaded the installed backend and recorded this
explicit discontinuous disturbance; it is not a product plugin, a public scene
mutation API or evidence that the ACT policy naturally dropped the object.
Robot joint state, controls and simulation time were preserved at the disturbance;
the normal native step then generated the new camera/joint observation. Neither
the disturbance label nor simulator truth entered the model input. With the same
Codex 0.159.0 and requested gpt-6-sol/high backend, the installed Agent proposed
transfer at sequence 0, then help at sequence 400, bound to the same task and epoch 0.
The task returned `needs_help` in 48.657 seconds; one 400-step transfer was delivered,
settled and released, with no hold submission or native hold steps. All 401 frames at 640×480 decoded, the final recorded frame matched the model image within codec
loss, and Host/worker/two decision processes plus all six sampled related
processes exited. The fixed task evaluator remained `unknown` because the
one-second hold window was not executed; Agent/Core task verdict stayed
`unassessed`. CLI exit1 reports incomplete application work here, not a failed
model request or missing cleanup.

Two earlier fixture attempts are retained as setup failures: the installed module
initially took precedence over the research override, then a physics reset helper
reset arm state and simulation time as well as the cube. Neither is counted as the
cube-only counterexample. This one explicit disturbance supports bounded visual
abstention and no subsequent hold effects, not broad perception reliability,
continuously evolving scenes, physical safety or recovery. Selected temporal boundaries have separate checks below; broader model faults
and a comparable native baseline remain M5f work.

Two Linux CPU/OSMesa seed0 temporal cases used a declared deterministic initial
transfer proposal and real ACT/MuJoCo execution, then the decision at sequence 400.
One started an actual Codex0.159.0 client, observed `turn.started`, requested
cancellation and reaped the client; the task reported `cancelled` in 36.628 seconds.
Its events retained the optional Code Mode host error and no completed model turn.
This proves the local client path, not remote model acceptance or termination.
The other used a controlled CLI with a one-second decision budget; its termination
handler still produced a valid bound `hold` answer after that deadline and exited
normally. The task rejected that answer and reported `needs_help` in 35.574 seconds.
Each retained one 400-step delivered/settled/released transfer, no hold submission
or native hold steps, 401 decoded frames and no remaining sampled related processes.
Both task verdicts stayed `unassessed`; with no executed hold window, no full-task
physical evaluation or success is claimed. These mixed-caller cases are distinct
from the normal real visual task and the actual-image disturbance counterexample.

The Agent's eight additional real subprocess tests cover prelaunch cancellation
and expiry, late answers during termination, process-exit races with cancellation
or expiry, and a child that ignores terminate and requires kill/reap. Two focused
checks reject the pre-fix adapter; the final complete application suite has 30
passing tests. Agent CI installs its existing `vision` extra and discovers these
checks in the existing Ubuntu job; no Core, Host or Harness workflow change is
needed. The checks do not qualify remote service cancellation, all transient
processes, physical hard-stop latency, continuously running scenes or recovery.
Comparable native results/costs and bounded M5 closeout remain pending.

A separate Codex CLI consumer was locally exercised through a research stdio MCP
bridge. Two real trials used one session/host/model, seed0 then explicit reset to
seed1,258/260 steps, four actual camera samples, retained prior receipts and
observed process cleanup. Its response distinguished the native condition from
unverified complete handoff/stable holding. Two pre-effect tool errors were
rejected and corrected; this is not a zero-error or general planning result.
Seven focused bridge/verifier tests and an official-SDK stdio run passed; the
latter also exercised queued cancellation and EOF cleanup. Request-cancellation
shielding has unit/SDK-source evidence, not an end-to-end MCP cancellation-notice
test. A disabled tool-dispatch configuration produced no calls and was retained
as a failed experiment; CLI exit zero alone is never execution evidence. The
research bridge is not an installed public MCP module or a security boundary.
That earlier experiment's product-code change routes child stdout to stderr, keeping
model/library diagnostics out of protocol output;23 session tests passed again.

The reusable [local MCP module](../integrations/mcp/README.md) now packages this
tool boundary independently of the research paths/model configuration. Its15 focused
tests passed on macOS/Python3.12 and locally in Ubuntu22.04/Python3.10. They exercise
real stdio/Core tasks, same-owner reset/retained receipts, schema
rejection, task cancellation and EOF during active work. Actual MCP cancellation
notifications are also checked with a controlled startup barrier: a started call
finishes, a cancelled queued submit never executes, subsequent queries work and
owned processes are reaped. An initial protocol run exposed SDK1.30's duplicate
response after shielded cancellation; a checkpoint after the completed client
call fixes that path. Initial failure evidence remains in the research record.
Trace failure must not prevent owned cleanup, and failed close retains its handle.
Local build and installed/relocated fixture examples completed with values7/4.
The configured ACT/MuJoCo SDK example also completed two trials at258/260 steps,
four real camera samples and confirmed close. A deterministic SDK caller does
not prove a live Agent or an embodied business Agent has run.

A fresh live Codex consumer also passed against the product module:14 actual tool
calls, two trials at258/260 steps, four camera samples, one session/host/worker,
retained prior receipt, explicit close and observed process absence. Its answer
kept full handoff/stable holding unassessed. This used CLI0.159.0, requested
`gpt-6-sol/high`; the service-resolved model snapshot is unknown. The first attempt
completed two native trials but timed out after network retries before final
consumer verification; it remains a failed run. A bounded same-budget proxy-enabled
retry passed. Independent review covered the module, protocol/installed consumer
and final live traces/images. These are existing-Agent access results, not a
business Agent application or reliability/performance estimates.

The separate Ubuntu22.04 optional-Python job builds the extension, runs session
tests, runs the three lightweight backend checks with distro NumPy, and consumes
the installed package after relocation. It additionally installs the optional MCP
requirements, runs its protocol tests and consumes the relocated MCP package.
Hosted results are reported in PR checks; these jobs do not run ACT or MuJoCo.
The prior Session/backend build/test/install sequence passed
locally on2026-09-30 in an amd64 Ubuntu22.04 container, using Python3.10.12,
GCC11.4.0, CMake3.22.1 and distro NumPy1.21.5. The source was mounted read-only;
build/install directories stayed in the disposable container. The relocated
consumer completed two operations with values7/4 and retained the first result.
This is local container evidence. Hosted workflow results for delivery are tracked
separately in the pull request checks.

The same amd64-emulated container's additional default-Core run passed56/57;
`compute_prelaunch` failed its invalid-executable spawn expectation. A focused
probe returned spawn success followed by child exit127 for an executable text
file without a valid format. On native ARM64 Ubuntu22.04, the same probe returned
`ENOEXEC` without a child, and all57 default-Core tests passed. Keep the failed
emulated result distinct; do not weaken that test or infer native x86 CI failure.
No Core implementation or behavioral test was changed for this difference.
Default Core/sanitizer jobs remain Python-free. It does not install models or
run MuJoCo. Research evidence and the reproducible consumer remain in the host's
existing experiment record; no weights or machine paths are shipped here.

Before delivering each remaining increment, exercise its actual boundaries:

| Increment | Required evidence | What it does not establish |
|---|---|---|
| Optional Core bridge and session | Real C++ Core from an independent Python consumer; clean Core-only build; partial startup/close; bounded framing and duplicate request handling; two operations in one host; caller/worker delays do not block host control progress | MuJoCo support, live Agent or host-death recovery |
| MuJoCo/ACT normal closure | Fixed native baseline versus host-owned chunk execution; same supported input and result condition; all selected runs/videos retained; explicit reset then second trial with the same host and loaded model; actual resource cleanup and result retention | Complete release/stable grasp from reward4, continuous physical stopping or general reliability |
| Separate Agent consumer | Fixed tested counterpart versions; live goal, capability choice, real observation, skill call and grounded result in one run; unsupported full-handoff goals remain explicit; model/version/cost recorded | A pretrained ACT worker or replay alone is not a task Agent |
| Reusable MCP integration | Real SDK/stdio, installed/relocated consumer, MCP request cancellation versus explicit task cancellation, queued-call rejection, EOF/partial startup/failed close and trace-failure cleanup | Existing-agent tool access is not an embodied business Agent, adversarial sandbox or real-device support |
| Embodied business Agent application | Concrete business goal/state, changing observations, skill decisions and bounded recovery, evaluated end to end on its selected task/backend | MCP packaging or successful Codex tool calls do not supply this task logic |
| Cancellation and faults before broader support | Nonempty queue cleared; no native submission after host revocation; slow/late/malformed prediction rejected; no new admission on unresolved cleanup; caller loss and partial recording preserve uncertainty | Request-send time is not revocation time; one in-flight native step and host stalls have no hard stop bound |

The native qualification's contact criterion and full-task assessment must remain
separate. Do not change evaluator meaning to make an integrated run appear equal.
Linux simulation/CI and the later Isaac combination need their own setup and
runtime evidence; the optional job covers the deterministic session and lightweight
backend checks only.

## M1-M3 applicability to the ROS profile

This maps existing semantics to `nav2-fixed-context-sequence-v1`; it does not
transfer every local-worker guarantee to ROS. Simulation evidence assumes an
exclusive fresh deployment, a surviving and polled Owner, and a reachable drive
channel. The historical sections below retain the revision and scope of each
observation. Unit tests, simulation runs and hardware evidence are distinct.

| Concern | Applicable ROS evidence | Remaining limit |
|---|---|---|
| M1 admission, dispatch and usable results | [Observation](#optional-humble-nav2-observation): absent binding and pre-dispatch cancellation prevent send; accepted UUID, feedback and result flow through the Owner/Core boundary | The original observation binary leaves settlement pending and blocks a second admission |
| M1 settlement before the next action | [Sequential run](#optional-sequential-nav2-settlement): matching worker/BT closure, drive generation, fresh quiet motion and B readiness precede Core settlement, admission and dispatch | B remains pending without a C candidate; no reusable arbitrary-length ROS Host |
| M2 failure and unusable evidence | Sequential and [cancellation](#optional-movement-time-nav2-cancellation) negatives withhold planner/drive/odometry facts; missing evidence blocks continuation | Native failure, cancel acknowledgement, native terminal and settlement remain separate |
| M2 moving cancellation and late output | ROS simulation observes exact-goal cancellation and separate closure; Core tests cover rejecting success after revocation | The finite ROS test requires a Cancelled result and does not itself force a late-success race; no hard physical stop deadline |
| M2 operation deadlines | Core deadline tests remain in the Core suite; simulation launcher deadlines bound the whole experiment | An outer process timeout is not a demonstrated ROS operation-deadline/settlement contract |
| M3 replacement and stale work | [Replacement](#optional-movement-time-nav2-replacement) closes A, checks fresh B context, settles A and opens generation 2 before sending B; retained old producer probes test isolation | Fixed A/B scenario, not an arbitrary-goal/preemption API |
| M3 observation loss and independent stopping | [Runtime loss](#optional-runtime-observation-loss) withdraws authority; [consumer isolation](#optional-independent-consumer-isolation) tests the declared stop path with an unresponsive navigator | Full network/drive loss, Owner death and hard stop bounds are not validated |
| M3 provider rebinding and recovery | Core/Linux Host checks cover their documented local execution domains | Generic ROS rebinding, Owner restart and durable task recovery are unsupported by this profile |
| Environment-driven waiting | [Obstacle waiting](ROS2_INTEGRATION.md#obstacle-driven-waiting) uses a real Gazebo box, fresh laser/costmap evidence and the existing cancel/closure path; frozen scan must yield unknown | Finite fixed-corridor scenario; [one bounded resume](ROS2_INTEGRATION.md#resuming-after-observed-clearance) uses fresh post-stop clearance and a new context; goal revision and general obstacle avoidance remain unimplemented |

For an unfamiliar checkout, follow [Build](../README.md#build), then the
[simulation tutorial](../integrations/ros2/simulation/README.md). Check the
scenario verdict, Owner events and container removal separately. A picture of a
stationary robot, native terminal result or container teardown alone cannot
establish settlement. Independent reproduction is useful evidence; only an
actual new user's attempt establishes contributor feedback.

On 2026-09-25, an independent public checkout of `56dd7ef` on macOS ARM64
passed 47 portable Debug CTests, produced the documented 29/61 example results,
and passed all 26 Python simulation tests. The default SDK failed the compiler
probe before project compilation; the documented [SDK selection](#macos-sdk-selection)
path succeeded with a compatible installed SDK. The HTTP test required loopback
binding permission. This was an internal tutorial audit, not outside feedback,
a new sanitizer run, or a new full simulation build. Linux movement evidence
remains the separately linked hosted run.

## Optional Humble Nav2 observation

The source-tree-only [Nav2 example](../integrations/ros2/nav2_observation/README.md)
is built separately from the default Core/compute targets. Its README supplies
Ubuntu 22.04 / Humble commands and the trusted fresh-deployment requirement.
The new [ROS workflow](../.github/workflows/ros2.yml) compiles it and runs
`nav2_requires_fresh_deployment` and `nav2_motion_window`; it does not launch a
simulator. The latter includes finite-window counterexamples for deceleration,
clock/odometry ordering, stale/duplicate data, drift and invalid/excessive velocity.
Local execution
of the original build/guard commands passed (1/1 guard); the new motion predicate
was checked in a final optional CTest run that passed both registered checks (2/2)
after the bounded acquisition fix. Those were pre-submission local checks; the first slice subsequently merged
through PR #9, with Core and Humble CI passing on master `2ed8cb2`.

Local amd64 Docker on Apple Silicon, using Humble and Gazebo Classic, observed:

| Case | Required observation and result |
|---|---|
| Normal, final browser path | One native send; accepted UUID retained through feedback/result; Core-protected output accepted; Gazebo displacement 2.473 m and goal error 0.230 m; settlement pending; actual second admission refused |
| Missing startup binding evidence | Core refuses admission; zero native sends and negligible Gazebo movement |
| Cancellation before dispatch | Revoked Core authority cannot claim dispatch; zero native sends and negligible Gazebo movement |
| Deployment guard | Missing fresh isolated deployment premise exits with rejection before ROS startup |
| Same-owner post-result motion | Final normal run: 31 samples over 1.02 simulation seconds, maximum speed 0.000103031 m/s; Gazebo displacement 2.496 m and goal error 0.216 m; output accepted, settlement pending, second admission blocked |
| Post-result odometry loss | Final fault run: navigation and output accepted; removing the owner subscription yields zero motion samples and no motion confirmation; second admission remains blocked |

The research workspace retains raw observations under
`experiments/ros2_navigation/results/`: `viewer-harness-check.log`, session
`m4-live-0ce9951f22fd`, `harness-deny-startup-01`,
`harness-cancel-before-dispatch-01` and `harness-final-build.log`. The
same-owner increment retains `motion-owner-final-build.log`,
`motion-owner-normal-03/native-goal.jsonl` and
`motion-owner-withhold-01/native-goal.jsonl`. The first two motion runs remain
failed evidence: native success arrived during deceleration. A regression failed
before the bounded acquisition fix, whose final normal and fault runs passed.
The updated browser path also passed (`viewer-motion-check.log`, session
`m4-live-bb3d0a6ee867`): 2.498 m displacement, finite motion observed, settlement
still pending, no JavaScript errors and owned-container cleanup checked.
Independent review found no remaining blockers for this increment; it does not
accept native closure or a persistent Host. These observations concern the
uncommitted worktree based on `b0929ab`, not a released revision. The
research launcher/recorder and browser viewer were not packaged at that revision;
the current [repository package](#repository-simulation-package) supplies a
public source build/run path. Earlier startup-query failures are retained as incomplete runs;
waiting for actual clock/odometry publications precedes the successful runs.

This demonstrates normal submission and observation under a fresh/exclusive
simulator premise. It does not prove motion-time cancellation, native quiescence,
stop deadlines, restart recovery, target timing or physical robot safety. No
settlement evidence is fabricated from arrival or container teardown. Existing
Core suite results below remain tied to their recorded versions; this increment
does not claim a new full-stack sanitizer run.

The independently extracted normal-observation slice was rebuilt locally on
Ubuntu 22.04/Humble amd64 without the research drive interface or JSON observer
dependency. Both registered checks passed (2/2). A fresh headless Nav2/Gazebo run
of the final binary moved 2.495 m with goal error 0.220 m, one native send, accepted
output, a confirmed finite motion window, pending settlement and refused second
admission. The research workspace retains `observation-slice-final-build.log` and
`observation-slice-normal-02/native-goal.jsonl`. Earlier build/normal-01 records
precede cleanup of constant closure flags; the observation-only C++ summary now
omits `native_closed` because this profile does not observe native closure.
That local slice subsequently merged through PR #9; its master Humble CI passed
2/2 and Core Debug/sanitizer checks each passed 57/57 on `2ed8cb2`. Earlier denial/fault results above
remain evidence for the unchanged default behavior, not additional reruns.

## Optional sequential Nav2 settlement

The optional Humble workflow now builds both navigation binaries and the
message-only fixture interfaces. It runs the existing motion/deployment checks
and `nav2_closure_observations`: matching child/worker identities, missing facts,
BT/drive ordering, malformed observations and retained/wrong drive generations.
These unit checks do not run a simulator or establish physical stopping.

The second slice must pass a real normal A-to-B run and separate withheld
planner acknowledgement, drive acknowledgement, odometry, B map and B controller
cases. The Owner must emit the actual Core settlement/admission boundary; the
fixture's independent Gazebo pose check establishes displacement only. Normal
requires two actual goals, A released, B native closed with fresh quiet motion,
B still pending and a third admission blocked. Every fault requires A pending,
no B admission and one native send. Movement-time cancellation/replacement are
separate increments documented below, as is bounded clock/odometry loss;
an unresponsive navigator with reachable drive control is covered separately below. A separate `withhold-feedback` regression must reach native success, reject the
missing pose observation and exit 1 with `incomplete`, without a promise exception
or B admission. Local final behavior checks used the callback-error fix:

| Scenario | Recorded result |
|---|---|
| Normal A-to-B (`owner-handoff-normal-05`) | Gazebo displacement A 2.495 m, B 1.859 m; actual Core A settlement/B admission; three quiet observations each 31 samples/1.02 s; B native closed but unsettled; third admission blocked |
| Withheld planner/drive ACK (`owner-handoff-withhold-planner-ack-06`, `owner-handoff-withhold-drive-ack-06`) | Native-closure evidence incomplete; A pending, one send, no B admission or generation-2 open |
| Withheld odometry (`owner-handoff-withhold-odometry-06`) | Native closure observed; fresh motion unavailable; A pending, one send, no B |
| Missing B map/controller (`owner-handoff-missing-map-05`, `owner-handoff-missing-controller-05`) | B readiness unavailable; A pending, one send, no B generation open or goal |
| Default observation regression (`owner-handoff-default-regression-01`) | Standard Nav2/Gazebo displacement 2.476 m; one send, protected output, finite quiet window, pending settlement and second admission blocked |
| Missing result feedback (`owner-handoff-withhold-feedback-05`) | Native success; missing pose observation rejected; exit 1 with incomplete status, no promise abort or B handoff |

Raw Owner/native observations, independent Gazebo checks and build logs are
retained under the research workspace's `experiments/ros2_navigation/results/`.
The final callback build passed all three Nav2 CTests. A final rebuild also covers
later comment/invalid-argument help edits, which do not change accepted-mode behavior.
Failed runs remain recorded: B lifecycle response timeout kept A unresolved;
an external Gazebo query timeout did not count as PASS; an A native abort exposed
an exception escaping the ROS result callback. The latter was fixed and exercised
by the missing-feedback case above. This is bounded functional evidence, not
reliability, timing or real-hardware qualification.

The unchanged Core passed 57/57 on native ARM64 Ubuntu 22.04 in this slice.
The amd64 emulated container passed 56/57: its invalid-executable `posix_spawn`
returned a child exiting 127 instead of an immediate spawn error, which violates
the existing `compute_prelaunch` test premise. A standalone spawn probe confirmed
that behavior; the test was not weakened. Native GitHub Ubuntu checks remain a
merge requirement. Local macOS configuration was unavailable because its installed
SDK/linker pair could not link even CMake's empty compiler test.

## Optional movement-time Nav2 cancellation

The Humble workflow builds `nav2_cancel_response_test` and runs
`nav2_cancel_response` with the existing three Nav2 checks. The new check uses
real ROS cancellation response types: exact target UUID, empty response, wrong
UUID, multiple entries, explicit refusal, unknown/terminated goal and unknown
return code. A matching entry cannot override an error return. This checks
response interpretation, not actual robot stopping. Core source is unchanged.

Real simulation acceptance requires all of the following in the same run:

- Actual movement before cancellation, one native goal and one targeted cancel;
  a corresponding cancellation response and a Cancelled native terminal result.
- No delivered output and no second admission at cancellation, terminal arrival
  or after the closure attempt. The receipt remains pending without B readiness.
- Normal cancellation: matching native work/drive closure, fresh quiet window,
  independent Gazebo position short of the original destination. Injected late
  generation-1 commands must be rejected by the actual drive and cause no
  significant displacement.
- Missing planner/drive acknowledgement: incomplete native closure, no release.
  Missing odometry: native closure can be observed but quiet motion cannot.
- Existing normal A-to-B behavior still passes with the updated native leaf.

Use the source-tree build commands from the example README and select all
`^nav2_` CTests. Actual cancellation simulation and same-run recording require
the research launcher; they are separate from hosted compile/unit CI. A cancelled
Action result does not prove its worker has ended. Timing records are sample
observations in an emulated simulator, not worst-case stopping guarantees.

Final local cancellation build passed 4/4 Nav2 CTests. The pinned native fixture
also passed its focused control-root lifecycle regression: root-only halt leaves
the interrupted root RUNNING; full `BT::Tree::haltTree` closes/resets it without
repeating already-ended asynchronous work. Real Ubuntu 22.04/Humble amd64 runs
on Apple Silicon recorded the following (research results prefix `owner-cancel-`):

| Scenario | Recorded result |
|---|---|
| `normal-04` | Cancellation after 0.50 m; final Gazebo displacement 0.540 m, short of A; native closure and fresh quiet window observed; 10 injected late generation-1 commands rejected without significant movement |
| `planner-04` | Final displacement 0.556 m; withheld planner ACK prevented full native-closure confirmation; second admission refused |
| `drive-04` | Final displacement 0.544 m; withheld applied-drive ACK prevented full native-closure confirmation; second admission refused |
| `odometry-04` | Final displacement 0.555 m; native closure observed, fresh quiet motion unavailable; second admission refused |
| `normal-regression-04` | Normal A-to-B preserved: Gazebo displacement A 2.497 m/B 1.785 m; A settled, B admitted/native closed, B pending and third admission refused |

All four leave settlement pending and deliver no output. In the normal run,
Cancelled terminal arrived 19 ms after the request and acknowledgement 25 ms
later than the request: the terminal-before-response ordering was exercised.
The successful quiet observation was recorded at 3677 ms after the request;
this includes a deliberately held 2500 ms native worker tail and a one-second
quiet window. It is **not** a measured physical-stop time or an upper bound.
The same-run 1600×900 RViz recording is retained with the raw observations.

Earlier failed runs are retained: `normal-01` exposed success-only child identity
publication; `normal-02`/`normal-03` exposed the interrupted control-root lifecycle.
The fixture now publishes accepted child identity while running and completes
the actual full tree halt only after the native worker has joined. No matching
or motion threshold was weakened to pass these runs. The cancellation Owner
also rethrows captured callback errors after closure/quiet, so an expected
missing-observation result cannot mask such errors as a successful fault test.

## Optional movement-time Nav2 replacement

`replace-moving` must record A cancellation/no delivered output, native closure
and fresh quiet, then B readiness, actual Core settlement/admission and B native
execution to the distinct goal (0.0, -0.5). A must move 0.45–1.5 m from spawn
before closure; B must move more than 1.0 m from that point and finish within
0.35 m of its target, measured independently in Gazebo.

The research fixture publishes forty late nonzero commands on A's raw input
while B executes. During this interval both A and B smoother bridges must forward
nonzero commands with their original identities; drive records must reject A's
generation-1 commands while generation 2 is open. B must still reach its target.
The checker correlates injection with B's execution interval, not a post-run
injection against an already sealed drive.

Five fault cases remove planner ACK, applied-drive ACK, fresh odometry, B map or
B controller. All must preserve A pending and block B admission/open/send.
Cancel-only and normal arrival-then-B remain regressions. These simulation cases
use the existing research launcher; their initial Humble revision ran four
local checks. The runtime observation increment below adds a fifth check;
Humble CI does not run Gazebo. No new Core test or dependency is needed for the
fixed replacement scenario. Hosted results are tracked by the delivery PR
separately from these local records.

The final local build passed 4/4 Nav2 checks. In `owner-replace-normal-04`,
Gazebo measured A displacement 0.551 m and B displacement 1.204 m. B reached
within 0.35 m of its new target; the drive rejected 41 old-generation commands
while B executed. The Owner recorded one cancel, two sends, A settled/no output,
B admitted/successful/native closed, and B pending/third admission denied.

The five final fault runs (`owner-replace-planner-03`, `drive-03`,
`odometry-03`, `map-03`, `controller-03`, all with the same `owner-replace-`
prefix) passed their expected refusals: missing planner/drive ACK prevented
native closure, missing odometry prevented fresh quiet, and missing map/controller
prevented B readiness. Every case retained A pending, one send and generation 1
only. `cancel-regression-03` retained cancellation-only behavior (0.553 m final
displacement, ten late commands rejected, no B). `sequential-regression-03`
retained normal A arrival then B (2.519 m/1.845 m). All eight final cases exited
0; prior failures below remain failures.

Earlier runs are retained: `normal-01` found the missing CLI mode whitelist;
`normal-02` found a same-source evidence sequence collision between no-output
and settlement observations. The Owner now advances a shared source counter;
Core checks are unchanged. Recorded `normal-03` failed safely during B startup
on a lifecycle response timeout, before settlement/admission. The headless
`normal-04` uses the same final binary; a successful run does not erase that
startup-availability limitation or prove its cause is recording load.

The same final binary also passed recorded `owner-replace-recorded-05`: A
0.548 m, B 1.207 m, 42 old-generation commands rejected during B execution.
The uncut RViz video is 1600x900, 43.3 seconds, with 409 decoded frames; A's old
path is green and B's new path orange. This is simulation footage, with no speed
change or authored motion. Independent review covered final source and the raw
successful/fault/regression records; it did not independently rerun Docker.

## Optional runtime observation loss

The local/CI `nav2_runtime_observation_watch` check adds expiry at the two-second
boundary, initial grace, frozen/backward stamps, invalid samples and a restoring callback arriving before
the next poll. Expired intervals remain latched until a new execution start. Humble CI now selects five `nav2_` checks. These do not prove
native stopping, bounded scheduling or a complete runtime supervisor.

Real simulation checks must start the observation-channel fault during actual
motion, with native Nav2 still connected to the simulator. Missing odometry,
repeated old odometry and missing clock must withdraw the Core binding, suppress
output, request one exact-goal stop and deny another admission. Native closure
may succeed while fresh quiet remains unknown. The restored-odometry case must
observe fresh quiet without settling, rebinding or granting B. A normal sequence,
cancel-only and moving-replacement run are required regressions with the same
final binary. The external checker correlates the actual relay fault, native
UUID/response/result, drive generation and Gazebo pose; it grants no authority.

The four named scenarios, remapping and limitations are defined in the
[runtime contract](ROS2_INTEGRATION.md#runtime-observation-loss-boundary).
Missing terminal results remain pending in the implementation; terminal loss,
native endpoint loss and Owner death are not injected by these four cases.

Final local Ubuntu 22.04/Humble build passed all five Nav2 checks. Four real
moving-fault cases passed: odometry loss, old-odometry replay, odometry restored
after six wall seconds, and clock loss. Each retained withdrawn binding and
pending settlement; only restoration established fresh quiet. Observed detection
was 1.94–1.97 seconds after the injected cut, consistent with a two-second budget
measured from the last progressing sample; this is not a stopping-time limit.

An initial restored-odometry attempt failed before admission because the native
controller lacked its map transform. A normal sequence also failed after B was
accepted: stale native map-to-odom transforms led to an ABORTED result. Neither
is a passing fault test. The latter did not expire clock/odometry progress,
illustrating that this watch does not supervise every Nav2 health condition.
A moving-replacement attempt completed the Owner flow but failed the independent
Gazebo pose query timeout. Failed records remain retained; these failures are not
claimed resolved.

The final unchanged binary also passed normal A→B, cancel-only and moving
replacement regressions. The replacement rejected 39 old A commands during B.
Independent review approved the source and seven successful raw runs within the
declared boundary, with non-blocking follow-ups for the failures above; the
reviewer did not rerun Docker. These successes do not establish simulator
reliability. These records establish local validation; hosted checks belong to
the delivery PR.

## Optional independent consumer isolation

The existing `nav2_closure_observations` CTest now accepts a matching actual drive
update before worker/BT closure without declaring full closure, and retains that
separate fact after producer reconciliation fails. Wrong scope/generation,
pre-request timestamps, service acceptance without an applying-update ACK and
malformed observations remain insufficient. Humble CI already builds/runs this
target; the selected count remains five.

The two [unresponsive-navigator cases](ROS2_INTEGRATION.md#independent-consumer-isolation)
require real movement before SIGSTOP of the actual navigator, no cancel response
or top-level terminal, and a driver applying-update before restored odometry.
The independent checker verifies the native process remains stopped and at least
ten real nonzero producer commands continue after driver application, with at
least ten subsequent consumer rejections. Only matching drive evidence plus a
fresh quiet window may report isolation/quiet; withheld drive ACK must report
both unconfirmed despite physical application. Both keep output/native outcome
pending, the binding withdrawn, settlement pending and second admission refused.

Two final local failure-injection cases have passed: the navigator remained
unresponsive while 43 continued nonzero producer commands were rejected by the
drive in each run. With the applying ACK, fresh quiet was observed; without it,
isolation and quiet remained unconfirmed. Both retained native/output pending
and denied B. This is one successful run per case, not a timing/reliability bound.

Run the same final binary through all four observation-channel faults and normal,
cancel-only and replacement regressions because they share closure handling.
Keep startup/native/evaluator failures as failures even if a bounded repeat
passes. These cases do not prove Owner-independent protection, a full network
partition response, drive-channel loss handling or a hard stopping bound.

The final binary passed all nine scenarios above and all five CTests. Independent
review approved the code and raw evidence within that boundary; it did not rerun
Docker. An initial injection attempt failed because emulated `/proc/PID/exe`
identified the interpreter, not the native navigator. The fixture now identifies
the exact launched argument and independently checks the actual stopped state.
That failed attempt remains recorded and is not counted as endpoint-loss proof.
Hosted checks for the committed revision are tracked by the delivery PR.

## What each environment establishes

| Check | Environment | Evidence boundary |
|---|---|---|
| Core build and state transitions | Local macOS and Ubuntu CI | Results apply to implemented tests; injected time is not a measurement of OS timing |
| Deterministic native adapter scenarios | macOS and Ubuntu from M1 onward | Simulated late output and settlement do not establish physical stopping |
| ROS 2 Humble lifecycle/action integration | Ubuntu target environment | Test native cancellation, provider loss, late completion, and actual adapter submission boundaries |
| Scheduling, latency, jitter, resource contention | Target Linux deployment host | Generic hosted CI and a Linux VM on a Mac do not establish deployment performance |
| Task outcome and physical effects | Relevant simulator, then robot hardware | Simulation and hardware results are reported separately |

The Ubuntu CI job is a GitHub-hosted Linux VM, not a Linux container hosted on the
development Mac. It establishes Linux compatibility and correctness coverage,
not bare-metal timing or physical safety. See GitHub's
[runner documentation](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).

## Core CI

[Core on Ubuntu](../.github/workflows/core.yml) runs on pushes, pull requests, and
manual dispatch. It uses Ubuntu 22.04, GCC, CMake, and CTest. The job has no ROS
dependency and runs the repository's registered tests. Ubuntu 22.04 is the initial
Linux build baseline; this does not configure a ROS integration environment.

The existing `Build and test Core` job runs a normal Debug build followed by an
AddressSanitizer/UndefinedBehaviorSanitizer build in `build-sanitizers`. Both run
all registered tests. Undefined-behavior recovery is disabled so a diagnostic
fails the test instead of merely printing a warning. A failure in either build
or test phase fails the same job; its check name remains unchanged for branch
protection. The [current baseline](#current-validation-baseline) summarizes the
latest merged result; earlier milestone evidence is retained below.

The checkout step follows the official
[checkout v6 interface](https://github.com/actions/checkout/tree/v6). No additional
digest, evidence archive, dependency matrix, or custom runner is needed for this
initial job.

The CI command below requires CMake/CTest 3.20 or newer (`--test-dir` was added
in 3.20). The project can still configure with CMake 3.16; the portable README
recipe runs CTest inside the build directory. The CI runner must have GCC, CMake,
and a build tool installed; the workflow does not install ROS or custom tooling.

To reproduce the CI job on Ubuntu with those tools installed:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --no-tests=error

cmake -S . -B build-sanitizers \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-sanitizers --parallel 2
ctest --test-dir build-sanitizers --output-on-failure --no-tests=error
```

Local build commands are in the [README](../README.md#build); see
[SDK selection](#macos-sdk-selection) for a macOS toolchain mismatch.
A Linux container or VM on a Mac may also catch build
compatibility issues, but cannot replace target-host timing measurements.

## macOS SDK selection

If the compiler and default SDK are incompatible, explicitly select a compatible
installed SDK in a fresh build directory. Replace the placeholder below with an
actual SDK path on your machine:

```sh
cmake -S . -B build-local -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="/path/to/compatible/MacOSX.sdk"
cmake --build build-local --parallel 2
(cd build-local && ctest --output-on-failure)
./build-local/robot_harness_normal_execution
```

Use `build-local` in subsequent example commands, including worker paths. Keep
machine-specific settings in local build directories; do not commit SDK paths or
change the project's compiler requirements to accommodate one workstation.

## Ubuntu development container

[docker/Dockerfile](../docker/Dockerfile) provides Ubuntu 22.04, GCC, CMake,
Ninja and process inspection tools for local development. On September 15, 2026,
the ECR-source recipe below built successfully with Docker Desktop 4.91.0 / Engine
29.8.0 on ARM64 with Linux kernel `7.0.12-linuxkit`. The source/test content of commit `4c4465b` passed all nine CTests
in both Debug and ASan/UBSan configurations, running as UID 1000 with GCC 11.4.0
and CMake 3.22.1. UBSan recovery was disabled. The working tree added only design,
documentation and container setup; no compute-process implementation was tested.
CTest logs remain in `/build/debug/Testing/Temporary/LastTest.log` and
`/build/sanitizers/Testing/Temporary/LastTest.log` in the named volume.
Install and start a local Docker runtime first; on macOS the official
[Docker Desktop installer](https://docs.docker.com/desktop/setup/install/mac-install/)
has an Apple silicon version. `docker info` must succeed before continuing.

Run from the product repository root, using a Docker engine on this machine:

```sh
docker info
docker build --pull -f docker/Dockerfile -t robot-harness-dev:ubuntu22.04 docker
docker run --rm -it --init --cpus=2 --memory=2g \
  --mount "type=bind,source=$(pwd),target=/src,readonly" \
  --mount type=volume,source=robot-harness-ubuntu-build,target=/build \
  robot-harness-dev:ubuntu22.04
```

If Docker Hub is unreachable, Canonical also publishes Ubuntu through
[Amazon ECR Public](https://ubuntu.com/docs/oci-registries/oci-how-to/getting-started/).
Select that official source explicitly instead of changing global registry settings:

```sh
docker build --pull -f docker/Dockerfile \
  --build-arg UBUNTU_IMAGE=public.ecr.aws/ubuntu/ubuntu:22.04 \
  -t robot-harness-dev:ubuntu22.04 docker
```

Edit source on the Mac; the container reads it at `/src`. The image contains only
tools and uses a non-root developer account. Linux build files and test logs live
in the named `/build` volume, separately from macOS builds. A new volume inherits
the prepared build directory; reuse it only for this checkout and architecture,
with one active build at a time. `exit` removes the container but retains the
named volume. The build context is only `docker/`, not the research workspace.

Inside the container, run the same two build configurations as CI:

```sh
uname -sm
g++ --version
cmake -S /src -B /build/debug -G Ninja \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++
cmake --build /build/debug --parallel 2
ctest --test-dir /build/debug --output-on-failure --no-tests=error

cmake -S /src -B /build/sanitizers -G Ninja \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build /build/sanitizers --parallel 2
ctest --test-dir /build/sanitizers --output-on-failure --no-tests=error
```

The current Linux suite registers 57 tests in each configuration. The nine-test
result above records only the original M2 container setup.
Record actual results, compiler, kernel/architecture and Docker's supplied image
identity when run; the Ubuntu tag and package repositories can change. There is
no image archive or additional project-computed digest in this workflow.

On Apple silicon, use native `linux/arm64`; the existing Ubuntu CI continues to
cover x86_64. Do not force amd64 emulation for cancellation timing observations;
[Docker documents](https://docs.docker.com/build/building/multi-platform/) the
architecture and emulation differences. Container process tests provide Linux
evidence for the recorded VM kernel, architecture and namespace configuration,
not target-device stopping latency. The 2 CPU / 2 GiB envelope is a development
container limit, not per-operation enforcement by Harness.

No privileged container, Docker socket mount, device access or writable cgroup
mount is needed for the first finite CPU-worker probe. `--init` handles container
PID 1 duties; probe/adapter code must still collect its own child exit and prove
its own cleanup. Container removal must not be counted as successful adapter
settlement. ROS/device integration and host-stall supervision require separate
environment choices. Keep the existing hosted CI until container-specific checks
have actual implementation and evidence.

## Manual verification

Build with the README recipe, then run from the repository root:

```sh
./build/robot_harness_normal_execution
(cd build && ctest --output-on-failure -V)
```

Substitute your chosen build directory when using a local SDK override. The
example runs fixed inputs; it does not accept motion commands or arbitrary sample
arguments. It prints four final receipts: A then B in synchronous mode, and A then
B in deferred mode. In each mode, A has squared values `[4, 9, 16]` and sum `29`;
B has `[25, 36]` and sum `61`. Each final line must report native success, output
acceptance, settlement, and an unassessed domain verdict. The array values are
checked in the fixture tests; the example prints the sums.

For intermediate states, follow
`deferred_events_expose_pending_delivery_and_settlement()` in
[the fixture tests](../tests/sample_execution_tests.cpp). It uses the public host
interface, so the same sequence can be stepped through in a debugger or a caller:

| Host action | Observable state |
|---|---|
| Initialize and submit A in deferred mode | One real worker submission, native accepted, no output yet |
| Complete deferred work | Four callbacks staged; Core has not processed the terminal evidence |
| Process started and terminal | Native succeeded, no sink result; B is blocked |
| Process result | Actual A result in the sink, settlement pending; B is still blocked |
| Process settlement | Current operation released; B can now be submitted |
| Close with B still pending | B is drained and delivered, callbacks are empty, new admission is closed |

The CTest entries contain multiple behavior checks, not just one assertion each. A successful test executable may print nothing; use CTest's exit
status and failed-check messages. Hand-inspecting the normal example does not
replace Core negative-evidence tests or establish robot motion safety.

For the M2a failure path, run `./build/robot_harness_failure_execution`. It
checks an actual blocked submission before settlement and then prints a fresh
operation with `result=16`. To run just the M2a host checks:

```sh
(cd build && ctest --output-on-failure -R robot_harness.m2a_failure)
```

The host tests use `reject_next_native_submission()` and
`fail_next_native_execution()`, and change worker/sink availability at the relevant
boundary. Submission count includes rejected native attempts; execution count
includes started work that fails. Read those counts alongside receipts and actual
sink contents. No-submission, rejection and native failure produce no result;
sink rejection preserves native success while reporting failed delivery. With M3b,
observed sample dependency loss withdraws the binding, so delivery is recorded as
authority-revoked and the next request requires explicit rebinding.

For M2b, run the cancellation example and focused checks:

```sh
./build/robot_harness_cancellation_execution
(cd build && ctest --output-on-failure -R 'm2b_cancellation|cancellation_execution_example')
```

The example prints four `true` observations: an acknowledged cancel with native
work still pending, blocked new admission, natural native success with the revoked
result discarded and cleanup observed, then a fresh request producing `16`.
[cancellation_execution.cpp](../examples/cancellation_execution.cpp) is the caller;
[m2b_cancellation_tests.cpp](../tests/m2b_cancellation_tests.cpp) additionally
exercises pre-dispatch cancellation, repeated/stale commands, cooperative/ignored/
refused/unavailable/absent responses, late natural failure, both output orderings,
missing cleanup during shutdown and conflicting terminal evidence. Tests inspect
actual submission/stop counts, retained native work and sink results.

For M2c, run the deadline example and focused checks:

```sh
./build/robot_harness_deadline_execution
(cd build && ctest --output-on-failure -R 'm2c_deadline|deadline_execution_example')
```

The example prints four `true` observations: receipt reads are passive, `poll()`
observes deadline `20` while the worker is still pending, expiry plus cancellation
attempts one native stop, and natural success at `25` yields no late sink result
but does produce cleanup evidence. [m2c_deadline_tests.cpp](../tests/m2c_deadline_tests.cpp)
checks absent/expired/equal deadlines, pre-dispatch expiry, idle polling, admission
with an expired occupied domain, both cancellation/expiry orderings, staged output
and pending-settlement orderings, synchronous work across expiry and clock changes
between entry and actual dispatch/sink/settlement boundaries. Core checks also
exercise wrong identity, backdated observations and deadline boundary backstops.
These use controlled clock values, without sleeping or timing measurements.

## Coverage grows with implementation

- **M0 (historical):** build, link, and call the placeholder Core library symbol.
  M1 replaces that smoke test with behavior tests; the old result does not validate
  execution-governance behavior.
- **M1:** build and run the normal-action example through the real library and
  deterministic adapter; register focused initialization, dispatch, event and
  receipt tests using host-supplied time. Run locally and in Ubuntu CI.
- **M2:** extend that suite with failure, cancellation, expiry and delayed/unknown
  settlement. Inspect actual fixture effects as well as returned receipts.
- **M3:** add stale-output, replacement, conflict and restart/recovery cases at the
  actual fixture submission boundary; keep earlier normal-path tests running.
- **M4:** run relevant Ubuntu ROS integration and cross-path regression separately
  from the ROS-independent Core/native job. Unsupported native guarantees must
  produce explicit limits, not a substituted synthetic ROS PASS.

Record the tested commit, environment, command, outcome, and material limitation
in ordinary test or CI output. Do not infer Linux PASS from a macOS result or
infer remote execution from the existence of a workflow file. The first Linux
result was verified on September 14, 2026: commit `0be526a` passed the
[Ubuntu build and 1/1 smoke test](https://github.com/xiao-yang25/robot-harness/actions/runs/34826759485).
This validates M0 only. Later changes require results for their own tested commit.

The M1 local implementation has passed all three registered tests on macOS with
AppleClang, both normally and with AddressSanitizer/UndefinedBehaviorSanitizer.
Independent code review also passed for that working tree. The host task record
retains the reviewed scope, commands, and output locations. Commit `7eb0e76`
subsequently passed the
[M1 Ubuntu CI run](https://github.com/xiao-yang25/robot-harness/actions/runs/34844507974)
on September 14, 2026: Ubuntu 22.04.5, GCC 11.4.0, configure/build succeeded, and
all three registered tests passed. This establishes the implemented M1 behavior
on both development platforms, within the coverage below.

The fixture tests check independent expected numerical results, sequential
requests, startup prerequisites, invalid arguments, explicitly staged deferred
delivery/settlement, and shutdown with pending work. Wrong/duplicate/stale and
conflicting native evidence is currently exercised directly against Core. A
separate independent review probe checked actual adapter dispatch and sink denial
for false permission, wrong result operation, and sink unavailability. This is
not full fault injection through the host event stream or M2/M3 coverage.

## M2 planned checks

These are acceptance scenarios for
[M2](DESIGN.md#m2-planned-behavior-failure-cancellation-and-expiry). M2a now has
Core and host tests; M2b/M2c each have focused tests and a caller example.
The current M2b/M2c implementation passes all nine registered CTests on macOS
normally and with ASan/UBSan. M2b independent review passed after the delayed-ACK
fix below; M2c independent code review found no blocking code issues. On September
15, 2026, implementation commit `8db3c38802950d025ec710dd6f71f43176108d18` passed the
[M2b/M2c Ubuntu CI run](https://github.com/xiao-yang25/robot-harness/actions/runs/34936844890):
Ubuntu 22.04, GCC 11.4.0, normal Debug and ASan/UBSan builds each passed all nine
CTest entries, with undefined-behavior recovery disabled. This closes the Linux
validation gap for the implemented sample cancellation/deadline behavior. It does
not validate the future expanded-execution requirements below.
The M2a implementation based on `fc283ec` passed all five CTests on
macOS with AppleClang, both normally and with AddressSanitizer and
UndefinedBehaviorSanitizer. The original pre-dispatch refusal reproducer failed
on M1 and passed on M2a, executing only the new request with actual result `9`.
Independent code review passed for that implementation. A separate probe checked
actual sink classification, deferred non-submission, retained failure controls,
and shutdown with an unavailable sink. On September 14, 2026, commit `16b2888`
passed the [M2a Ubuntu CI run](https://github.com/xiao-yang25/robot-harness/actions/runs/34864391097):
GCC 11.4.0 configured and built both Debug variants, with all five CTests passing
normally and under ASan/UBSan. Undefined-behavior recovery was disabled.
The host task record retains the
reviewed revision/diff and actual logs. Extend the same tests as later slices are built.
The same-source closure-sequence regression tests failed before the ordering fix
in both Core and host checks. After the fix, all five tests passed normally and
with AddressSanitizer/UndefinedBehaviorSanitizer. They reject regressing/reused
sequences even at equal timestamps or with a later timestamp, preserve exact
replays, and permit later valid closure. A constant-clock host test checks the
actual adapter sequences through non-submission, rejection and execution failure,
then verifies a fresh request's result. Different sources and operations retain
independent sequence spaces. Focused independent re-review of the sequence fix
passed, including an additional probe with shared native/adapter source identity.

M2b independent review found that a later ACK incorrectly raised the cleanup time
lower bound. The regression failed before the fix and passed afterwards. Settlement
now checks native/output prerequisites separately from control observation times;
new admission still respects those control times. Tests cover late ACK and late
cancellation arriving before earlier completed cleanup, while a premature cleanup
remains rejected. Focused independent re-review passed. This ordering fix preserves
the source/identity/sequence checks and does not introduce synthetic cleanup.

The host scenarios must exercise real fixture submissions, staged callbacks and
the actual managed sink; setting receipt fields directly is not sufficient.

| Slice / trigger | Required observable result |
|---|---|
| M2a: worker or sink becomes unavailable after initialization, before dispatch | Adapter makes no native submission; admitted authority is closed only with correlated non-submission and cleanup evidence; no retained request can execute later. After M3b, restoring availability alone stays blocked; explicit fresh rebinding permits a new caller request |
| M2a: native rejects a submitted request | Native acceptance is rejected, execution count and result count stay zero; attempted submission is distinguishable from accepted work; explicit cleanup permits a later caller request without retrying the old one |
| M2a: accepted work fails before result delivery | Actual fixture failure is reported; no successful output is fabricated; terminal alone blocks B, and native-use/output cleanup followed by settlement permits B |
| M2a: sink rejects the result after native success | Preserve native success, record failed delivery, inspect an empty sink; discard remaining output before settlement. No silent result retry or permanent pending state after proven cleanup |
| M2b: cancel before dispatch, then cancel again | No native submission; old dispatch permission stays denied; duplicate control is idempotent; release requires non-submission and cleanup observations |
| M2b: cancel accepted work, observe ACK, hold native termination or cleanup | Stop-request count is one; ACK alone leaves B blocked. Exercise both cooperative cancellation and ignored/refused cancellation followed by natural completion |
| M2b: completion is staged before cancellation, delivery occurs after | Native outcome remains truthful; current permission denies actual sink acceptance; disposal and cleanup are observed before B is allowed |
| M2b: result accepted before cancellation, cleanup delayed or missing | Existing sink result remains observable, with no rollback or second delivery; B stays blocked while settlement evidence is missing, including after shutdown if evidence never arrives |
| M2c: deadline absent, already expired, exactly reached, or crossed during delivery | M1 unchanged without deadline; expired input causes no submission; at equality dispatch/delivery is denied; advancing idle-host time uses the explicit poll; a synchronous computation crossing expiry cannot deliver late output |
| M2c: timeout while native work continues, then late completion | Distinct expiry reason, at most one stop request even if caller also cancels; no invented native terminal; B blocked until valid terminal/output cleanup/settlement; late output remains denied |

Also check control for the wrong operation and repeated commands, invalid source
or causally premature settlement, contradictory terminal evidence, and revocation
surviving shutdown. Cover both sides of the output-versus-cancel ordering and the
deadline-versus-settlement ordering: closure completed before the deadline stays
complete; pending closure at expiry retains the expiry fact. Reuse existing Core
evidence tests, adding host-path injection only where these new behaviors need it.

The partial-effect case here is an actual accepted managed result followed by
unfinished cleanup. Preserve that result and uncertainty about remaining effects;
do not relabel it as no effect or claim rollback. This is a fixture-scoped check,
not a partial-motion or physical-stop test. No complete fault matrix is required.
Keep M1 regression running, check changed lifetime paths with relevant sanitizers,
and run each implemented slice on macOS and Ubuntu with independent review.

## Expanded execution checks

These checks are required before enabling long-running, resource-intensive or
physical tasks under [stop and resource requirements](DESIGN.md#stop-and-resource-requirements-before-expanded-execution).
They are planned acceptance work, not tests implemented or passed by the current
nine CTest entries. The first implementation should use one small compute adapter
with observable running progress and resource ownership; physical guarantees need
separate controller/device validation.

| Scenario | Required observation |
|---|---|
| Requested stop/resource guarantee unsupported, unknown or stale | No native submission or expensive work allocation; admission reports the unmet requirement. A compatible request can run; changed prerequisites before dispatch also prevent submission |
| Cancellation while work is demonstrably running | Observe actual progress before cancel and cessation under the declared conditions/bound, plus resource release and output disposal; ACK or an empty sink alone cannot pass |
| Stop ignored/refused, no response or stop-response budget exceeded | Observe any declared bounded failure action at its real boundary; no synthetic terminal/settlement, repeated cancel loop or new conflicting admission while closure is unresolved |
| Resource limit reached or worker/host stalls | Observe the claimed resource limit at its enforcing owner and verify that required supervision still operates under the declared failure condition |
| Physical adapter loses command/host responsiveness | Verify the required native protection and resulting physical state in the target setup; process exit and ROS terminal status alone are insufficient |

Manual-clock unit checks establish policy ordering. Real worker tests must observe
running execution, cessation and owned resources; any timing claim needs its
specified clock, observation points, load and environment. Linux process/resource
behavior requires Linux execution, and hosted CI does not establish device stopping
latency. Reuse relevant CI checks as tests are implemented; retain existing M2
regressions. No fixed universal stop threshold or exhaustive fault registry is
introduced by this requirement.

### First compute slice checks

Acceptance cases for the [compute design baseline](DESIGN.md#first-compute-adapter-slice-design-baseline)
and [local integration contract](DESIGN.md#local-compute-integration-contract).
The local process implementation registers capability, process, host and example
tests in CTest; the existing Ubuntu job automatically runs them in both builds.
The original nine M2 tests retain their fixture scope.

| Case | Evidence required |
|---|---|
| Normal execution | Actual completed batches, independently checked scalar result, collected child exit and disposed channels; a second request can run |
| Policy and launch refusal | Unsupported hard bounds, excessive work, unknown/stale prelaunch adapter capabilities and changed prerequisites reject before launch; per-child readiness is not reused as prelaunch evidence; invalid requests allocate no worker; launch failure closes only proven non-submission |
| Cancel during partial startup | After a child exists but before readiness, missing readiness or failed/full control sends do not defer the grace anchor or prevent one supported escalation; collect the child exit and cleanup, without claiming non-submission |
| Cancel during running computation | Progress strictly between zero and N before cancellation, actual cooperative exit, discarded unauthorized output and resource closure; if the worker already finished, the run does not pass this case |
| Ignored/refused stop | Worker continues after the request; one supported escalation, collected exit cause, output disposition and cleanup; signal delivery alone cannot pass |
| Missing exit/cleanup evidence | No settlement or conflicting readmission; late natural success remains truthful and unauthorized output stays rejected |
| Channels and shutdown | Full/coalesced progress, partial message, early EOF, worker start failure and shutdown during execution cannot deadlock cancellation or fabricate success; supported/probed shutdown paths abandon no owned child; failures outside that scope retain explicit unresolved/blocked status and do not promise bounded destruction or proven cleanup |

Use controlled clock checks for grace/expiry ordering and real process checks for
execution/termination. Synchronization can make ordering deterministic, but a
worker parked at a test barrier alone does not prove interruption of computation.
Record progress/exit observations and monotonic timestamps; avoid claiming a
universal stop bound from sample measurements. Check the owned child's collected
status and channel lifetime directly; do not use system-wide process killing,
whole-system memory fluctuations or hashes as release evidence.

Run the POSIX probe on the available host and on Ubuntu before accepting Linux
behavior. A VM or remote Ubuntu environment is sufficient for this process scope;
no physical robot or privileged cgroup setup is required. Add focused CTest/CI
entries when the implementation exists, retain normal and sanitizer regressions,
and give process tests finite outer timeouts with explicit owned-child cleanup.


### Local compute usage and verification

On macOS or Linux, the default build includes `robot_harness_compute` and the
same-build `robot_harness_compute_worker`. Core itself retains its portable build.
From the product checkout after building:

```sh
./build/robot_harness_compute_execution "$(pwd)/build/robot_harness_compute_worker"
ctest --test-dir build -R 'compute_' --output-on-failure
```

The example prints the actual sum `332833505` for 1003 iterations, exit code 0 and
confirmed cleanup. Its 1000ms grace permits sanitizer exit checks; the host API
default remains 100ms, and neither setting is a hard stopping guarantee.

`ComputeExecutionHost` takes a trusted absolute worker path. Call `initialize`,
then `submit` (or `prepare` followed by `dispatch_prepared`), and keep calling
`poll` until the receipt settles. Inspect the optional result and separate process
observation. `request_cancel` revokes future result permission; it does not mean
exit. Call `begin_shutdown` and continue polling until `shutdown_status` is closed;
unresolved requires investigation, not treating the domain as free. The destructor
fallback may block and fail fast on irrecoverable ownership/cleanup errors.

`poll` here is the host's progress method, not the OS `poll`/`epoll` API. Keep
invoking it even without incoming events; elapsed deadlines and stop escalation
also need host execution. The example's 1ms sleep does not establish a stopping
bound. See [Design](DESIGN.md#local-compute-integration-contract) for the deployment
condition checks and the limits on per-call work.

The host integration tests check normal result/reuse, unsupported profiles and
workload limits, prelaunch loss, actual no-child spawn failure, running cooperative
cancel, ignored cancel, startup shutdown/deadline, delayed exit, cancel after an
independently observed exit, event backpressure, malformed/early worker exit, and
the example. Process tests additionally check raw wait status, control failure,
queued-but-unhandled stop and idempotent closure. Compile-time fixture workers are
built only for tests; production worker arguments have no fault-mode switch.

These checks do not establish hard stopping bounds, process RSS/CPU quotas,
host-crash recovery, remote/descendant/GPU effects or physical stopping. Full-control
send EAGAIN and irrecoverable OS close/reap failures have no runtime injection
coverage yet; failure paths must remain explicit, and unsupported required guarantees
are rejected. The bounded progress-backpressure case is distinct from a full
control channel. Protocol regression tests cover counter rollback across stop ACKs
and event EOF while a child remains alive; host-isolation tests reject cross-instance
authorities. Setup cleanup error propagation is statically reviewed, not fault-injected.

On September 15, 2026, the compute increment based on `4c4465b`
passed all 26 registered CTests in each of these configurations:

| Configuration | Result |
|---|---|
| macOS ARM64, AppleClang 21, SDK 26.5, Debug | 26/26 |
| Same macOS environment, ASan/UBSan | 26/26 |
| Ubuntu 22.04 ARM64 Docker, GCC 11.4, 2 CPUs/2GiB, non-root, Debug | 26/26 |
| Same Ubuntu environment, ASan/UBSan with UB recovery disabled | 26/26 |

CTest logs are retained in the host task record and build directories. The process
test entry contains ten focused scenarios; it is one CTest, not ten extra registered
tests. These results include the original M2 regressions. The existing Ubuntu CI
configuration runs all new entries automatically; the PR records the remote run
and its tested revision separately from these local results. Scoped independent
review approved the C++ implementation and independently rebuilt/reran all 26
tests on macOS. It found
no remaining blocking findings; no production or physical-stop certification is
implied.

## M3a replacement checks

Build normally, then run the example and focused checks:

```sh
./build/robot_harness_replacement_execution
ctest --test-dir build --output-on-failure -R 'm3a_replacement|replacement_execution_example|compute_replacement'
```

The registered suite grows from 26 to 29 tests on macOS/Linux. Existing CI runs
all entries in both Debug and ASan/UBSan without a new workflow job.

| Check | Required observation |
|---|---|
| Replacement fixture | B has no native submission while A's work, output disposition or settlement is pending; after settlement it has a fresh operation ID |
| Stale output/control | Replay A while B is prepared, running and native-success/output-pending; sink denial count increases, accepted storage stays unchanged and B's receipt remains valid; old dispatch/cancel cannot affect B |
| Positive result and duplicate | B accepts its actual `[25, 36]`, sum `61`, once; replay of an already accepted result cannot add another entry |
| Missing settlement and shutdown | Time and replay cannot release an unresolved A; shutdown drains staged replays, disables further replay and preserves missing settlement |
| Compute replacement | Cancel unfinished native work, collect/reap its exit and close channels before admitting B; old authority cannot dispatch or stop B, which produces the known sum `5` |

Replay retains the first real result after explicitly enabling the fixture slot.
It stages a duplicate as the next callback so tests can exercise B's live delivery
permission; it does not recompute a result, relabel authority or bypass the sink.
The retained duplicate is separate from A's original work/output disposal. This
does not permit settling outstanding native effects or starting B while A is
still running. The compute case checks real exit and reaping separately and does
not add synthetic messages to the process protocol.

Clearing the retained slot on adapter close is checked in code review. The public
post-close replay call returns false independently of whether that slot was cleared,
so the shutdown test alone does not prove its storage was released before destruction.

On September 15, 2026, the M3a increment based on `6bba453` passed all 29 CTests
on macOS ARM64 Debug and ASan/UBSan, and Ubuntu 22.04 ARM64 Docker Debug and
ASan/UBSan with UB recovery disabled (the same environments listed above).
A focused isolated mutation checking the current operation's authority instead
of the callback's original authority failed the replay-at-sink regression as
expected; the unchanged implementation passed. Scoped independent review approved
the final implementation and independently rebuilt and ran all 29 tests on macOS.
That local review preceded the push. The increment subsequently merged through
[PR #5](https://github.com/xiao-yang25/robot-harness/pull/5) as `3fd5474`, with the
same file tree as reviewed commit `22e5b28`. Its
[main-branch Ubuntu CI](https://github.com/xiao-yang25/robot-harness/actions/runs/35438203500)
passed all 29 tests in normal and ASan/UBSan configurations on September 19, 2026.
Provider generation changes, restart recovery, hard stopping deadlines and robot
effects remain outside these checks.

## Planned M3b and M3c checks

These checks implement the existing [M3 scope](DESIGN.md#planned-m3b-and-m3c-boundaries).
This table states scope, not evidence of PASS. The first Core/sample M3b increment
is mapped and tested below, followed by compute rebinding and task-caller integration.
M3c has the Linux prototype and two-step task integration mapped below.
Preserve M1–M3a regressions and run the existing macOS/Ubuntu
normal and sanitizer configurations, plus the required independent review.

| Slice | Required observation |
|---|---|
| M3b withdrawal and dependencies | An unavailable required provider/worker/sink blocks affected admission and dispatch; a ready replacement does not clear old pending work/output/cleanup |
| M3b successful rebind | Actual old-scope settlement and valid new prerequisites permit a fresh binding; a new operation produces an independently checked result |
| M3b stale or failed transition | Old-binding controls/evidence/output cannot affect the new operation at its submission/result boundary; rejected or incomplete rebinding cannot grant new authority |
| M3c restart | Host/Core starts recovery-required; old active work or partial/unknown effects remain blocking until corresponding native facts are established |
| M3c recovery evidence | Missing, wrong-identity, stale/expired or contradictory evidence cannot reopen admission; an interrupted recovery leaves no usable partial grant; valid fresh evidence permits subsequent execution |
| M3c native ownership | For the selected process path, observe surviving work or confirmed exit/cleanup externally and verify that restart cannot reuse old authority; fixture-only recovery is reported separately |

Cover declared conflict/composite prerequisites through the selected cases rather
than a separate exhaustive registry. Record which existing regressions already
cover partial effects or missing settlement, and add only the uncovered boundary.
Define the new observation/identity context before writing recovery tests; do not
turn an incremented counter or a synthesized clean flag into the test oracle.

The minimal task caller registers normal multi-step completion,
goal change, failure/unknown handling and clean shutdown as integration checks.
The existing local Host path uses public interfaces. The Linux recovery prototype
uses an example-local bridge to its private Client, without declaring a stable
recovery API. A dependent normal step needs the accepted
prior result and required settlement; replacing a cancelled goal instead follows
the declared handoff conditions and does not require a discarded result to be
accepted. Neither path changes Core's unassessed domain verdict.
The caller exercises M3b rebinding and now the limited M3c recovery path below. A future separate Agent
repository must exercise the actual Harness dependency in a small cross-repository
test; passing each repository independently is insufficient. ROS dependencies and
native cross-path checks remain separate M4 work, not part of today's Core CI.

### M3c first-slice validation

These requirements apply to the [recovery prototype](DESIGN.md#m3c-first-recovery-slice-design-and-experiment-boundary).
The original 47 CTests validate M3b; ten additional Linux CTests exercise the
private recovery prototype below. Research crash experiments remain separate
from product CTest. Linux Docker is suitable for initial process ownership checks;
GPU/device behavior, physical stopping and deployment latency need target hardware.

1. **Observe the current boundary.** Kill a real Host with a live worker paused
   after partial progress was observed; this does not prove computation was still
   incomplete at the instant of suspension. Separately cut after terminal emission
   but before native cleanup using a checked protocol fixture and an explicit stop
   barrier; do not infer that cut from elapsed time. Observe the worker's actual
   state and exit with a surviving test collector. A separate fresh process must
   not be mistaken for the worker's new parent or a source of the old result.
   Record deliberate suspension, subreaper adoption and cleanup as test controls.
2. **Demonstrate the owner prototype.** Keep the actual worker-owning process
   alive, kill Host A, and start Host B on the same scope. While the owner still
   holds old work or required output/channel cleanup, B admits no conflicting
   operation. After real closure and a fresh recovery exchange, one explicit
   caller request executes and yields an independently checked result.
3. **Reject invalid recovery.** Missing, stale, expired, wrong-session or
   contradictory observations, old commands/results and interruption before
   activation must leave admission closed. A delayed ready response must not
   bypass the current-session check at the owner's submission boundary. Cover
   owner connection loss during recovery without silently creating a fresh owner.
4. **Keep task truth.** Native success with lost result delivery does not become
   task success; no automatic retry or step-two execution after restart. Continue
   cleanup even when the caller reports an unknown or failed task outcome.

Only implemented product scenarios enter CMake/CTest, with bounded waits and
cleanup of owned test processes. The existing Ubuntu ordinary and ASan/UBSan
workflow discovers registered tests automatically; do not add research programs
or fabricate passing recovery jobs before that implementation exists. Core and
portable Host regressions remain in the macOS suite; Linux-specific ownership
checks must be labeled with their actual platform scope.

### M3c prototype checks

On Linux, the ordinary build also produces `robot_harness_recovery_execution`.
From the repository root, after building:

```sh
./build/robot_harness_recovery_execution normal "$PWD/build/robot_harness_compute_worker"
./build/robot_harness_recovery_execution handoff "$PWD/build/robot_harness_compute_worker"
./build/robot_harness_recovery_execution task-handoff "$PWD/build/robot_harness_compute_worker"
ctest --test-dir build --output-on-failure -R recovery
```

The parent launcher is the actual native owner. Each Host/Core is a separate
exec'ed process. The handoff example deliberately suspends the owned worker,
kills Host A with SIGKILL, observes that Host B is blocked, then resumes the old
worker so it can stop and be reaped. B recovers and explicitly submits three
iterations, yielding `5`. This controlled pause proves blocking during unresolved
ownership; it does not measure stop latency. The terminal test uses the existing
delayed-exit fixture and confirms a completed frame plus a stopped live worker
before killing A. No old task is retried or resumed.

| CTest suffix | Implemented boundary |
|---|---|
| `recovery_client` | Wrong session/request, contradictory snapshot, incomplete or expired activation, owner loss, old output, allocation failure and bounded outbound backpressure keep the Gate closed |
| `recovery_owner` | Submission before activation, invalid/expired activation token and stale-session submission launch no worker; new sessions do not reuse epochs |
| `recovery_normal_example` | Fresh activation, real native execution, independently checked result `5` and cleanup |
| `recovery_handoff_example` | Actual Host crash, a stopped live old worker blocks replacement, real closure precedes new explicit execution |
| `recovery_fd_collision` | Occupied inherited descriptors overlap fixed child targets; both independently mapped channels still complete normal execution and cleanup |
| `recovery_terminal_example` | Old terminal emission is not new-session task success; pending old ownership blocks recovery until cleanup |

The task variants use the same `FiniteComputeTask` as the existing local Host.
`task-normal` checks `3 → 5 → 6 → 55`. `task-handoff` kills Host A with its first
worker held alive; `task-between` kills it after accepting/settling the first
result, before submitting step two. The restarted controller marks the remembered
old intent NeedsAttention and cannot revise it or advance a step. Its ready
report confirms zero admissions, no result and unchanged uncertainty. Only then
does the surviving caller send a separate new-goal command. The new task runs
`4 → 14 → 7 → 91`; total launches remain exactly three, including A's first step.
The launcher retains intent only in memory, not a durable result or receipt.

| Additional CTest suffix | Implemented boundary |
|---|---|
| `recovery_task` | Owner-channel loss after a queued submission leaves task NeedsAttention; no retry, dependent step or accepted result; bridge rejects mismatched authority |
| `recovery_task-normal_example` | Same controller completes two dependent real compute operations through the recovery bridge |
| `recovery_task-handoff_example` | Real Host death with old worker alive; unknown old goal never resumes; explicit new goal waits for scope closure |
| `recovery_task-between_example` | Real Host death between settled steps; permission recovery does not imply recovered first result or automatic step two |

The Linux suite now contains 57 tests; macOS retains the 47 portable regressions.
The earlier prototype's allocation injection failed before its fix and passed
afterward; it remains in the suite. Before the launcher correction, full-suite
results were Ubuntu ARM64 Debug and ASan/UBSan each 56/56,
macOS ARM64 Debug and ASan/UBSan each 47/47. Earlier incremental reviews passed.
Delivery review subsequently reproduced a channel-mapping failure with inherited
descriptors 3 through 60 occupied. The launcher now duplicates both child sources
above its fixed 64/65 destinations before setting up file actions. Scoped ownership
closes all untransferred endpoints if socket creation, duplication, attachment or
spawn fails. The new `recovery_fd_collision` regression runs the normal example
under that layout: it fails before the fix and passes afterward. All 10 affected
recovery tests passed in Debug and ASan/UBSan after the correction. Unchanged Core,
Host and portable task checks retain the prior full-suite evidence; this targeted
run is not a new full 57-test run. Focused independent re-review approved the
correction.
The existing Ubuntu workflow runs all registered tests normally and under
ASan/UBSan, so no additional workflow job is needed. Local execution does not
substitute for the submitted revision's GitHub CI. Protocol fault injection
uses dedicated test peers; these are not external service or security tests.
A silent connected Host and a stalled owner have no watchdog in this prototype.
Rare OS failures, owner death with surviving work, machine reboot, durable receipt
replay, stable public recovery APIs and physical effects remain outside this slice.


### M3b focused acceptance mapping

The [live-host handoff design](DESIGN.md#m3b-implementation-design-live-host-binding-handoff)
is implemented for Core and the sample fixture, followed by the compute host. Tests expose observable refusals and actual sink
behavior, rather than checking only an internal phase enum:

| Case | Observable acceptance evidence |
|---|---|
| Withdraw before/after dispatch | No native submit for an undispatched revoked operation; accepted old work gets at most one stop across withdrawal/cancel/expiry; missing old cleanup continues to block replacement |
| Provider/dependency unavailable | Loss of a required provider/adapter/sink blocks prepare/dispatch; new-provider readiness cannot settle old work; restored availability alone does not undo withdrawal |
| Candidate preparation and failure | Require fresh old-binding quiescence even without a prior operation; reject an initial idle fact, wrong binding/source or expired old-scope evidence. Candidate-identity withdrawal aborts preparation, old-identity withdrawal cannot abort it; retry cannot reuse abandoned identity; candidate setup failure and permanent shutdown cannot reopen execution |
| Readiness ordering | Missing/false/expired facts block commit; reject wrong-source/identity input; poison matching equal-sequence contradictions for readiness and candidate capabilities rather than retain an earlier positive. Newer negative supersedes positive; valid newer evidence resolves the concern and permits commit |
| Actual replacement | A new operation after commit executes using the new provider and returns its independently known result; old controls/results cannot affect it or mutate the retained old receipt between commit and the next admission; initial Active without startup readiness still rejects work |
| Compute owner | Same host/gate/domain, old child confirmed exited/reaped and channels closed before provider rebind; do not substitute cross-host isolation or a new PID for evidence of rebind |

Run the first-increment example and focused tests:

```sh
./build/robot_harness_rebinding_execution
(cd build && ctest --output-on-failure -R 'm3b_rebinding|rebinding_execution_example')
```

`m3b_rebinding_core` covers the public control/evidence boundary, and
`m3b_rebinding_sample` replaces the actual sample adapter in the same host, retains
an old completed result for sink rejection, and checks the new result against 61.
The example demonstrates blocked handoff until closure, then a new provider with
a fresh generation. These three CTests are registered in the existing Ubuntu CI;
no extra job is needed.

`m3b_withdrawal_allocation` additionally injects allocation failure while preparing
the returned stop authority, first before any identity copy and then after a partial
copy. It verifies unchanged binding/receipt state on exception, retry delivering
the original operation's stop action, and no duplicate stop on later withdrawal
or cancellation. Allocation replacement is confined to this single-threaded test
executable; it does not change the production allocator or measure native stopping.

On September 19, 2026, the uncommitted Core/sample increment on base `3fd5474`
passed all 32 CTests on macOS ARM64 (AppleClang 21, SDK 26.5) and Ubuntu 22.04
ARM64 Docker (GCC 11.4), each in Debug and ASan/UBSan configurations with UBSan
recovery disabled. Independent implementation review passed, with a separate clean
macOS Debug build and all 32 tests passing. These are local results, not a new
GitHub CI run or evidence for compute-host rebinding. The
sample uses deterministic callbacks; no stopping-time measurement is claimed.

The compute increment adds three process scenarios and a runnable example:

```sh
./build/robot_harness_compute_rebinding_execution \
  "$(pwd)/build/robot_harness_compute_worker" \
  "$(pwd)/build/robot_harness_compute_worker"
(cd build && ctest --output-on-failure -R 'compute_rebind')
```

- `compute_rebind_handoff`: a real delayed-exit worker completes its calculation
  while still alive; withdrawal denies handoff until exit/reaping/channel cleanup.
  `waitpid(..., WNOHANG)` then reports `ECHILD`. In the same host/domain, install
  the early-exit fixture and observe failure, then install the normal worker and
  check the result `5`. Verify old controls are rejected and policy limits remain.
- `compute_rebind_unavailable`: remove a temporary executable link before any task
  or after preparation. No child is launched; a failed candidate stays withdrawn
  and consumes its generation. Restoring the link/initializing alone stays blocked;
  explicit handoff allows work. Shutdown after commit without another task preserves
  the old accepted result and closes normally.
- `compute_rebind_running_loss`: remove a required launch prerequisite while the
  actual child runs. Poll observes withdrawal, preserves its distinct reason, and
  drives cleanup before a replacement can compute its own result.
- `compute_rebinding_execution_example`: withdraw a launched worker, observe blocked
  handoff, drive native closure, rebind and compute the known sum `332833505`.

The compute increment passes all 36 CTests on macOS ARM64 (AppleClang 21, SDK 26.5)
and Ubuntu 22.04 ARM64 Docker (GCC 11.4), each in Debug and ASan/UBSan with UBSan
recovery disabled. Independent compute-increment review passed, including a separate
macOS Debug build and the four focused process checks. The additions
run through the existing Ubuntu job in both configurations; these local results
do not establish a new GitHub CI pass.
No OS close/reaper error injection or cross-host restart claim is added. Synthetic
old-result replay remains the Core/sample check; process tests establish actual
ownership release and executable selection without injecting a new process protocol.

Include an old result delivered while the new operation is itself eligible to
deliver; otherwise a denial could merely reflect that no operation currently has
permission. Keep initial M1 readiness and M3a same-binding replacement working.
Use fixture observations for controlled missing/contradictory facts, and real
process observations for compute ownership. Identity-exhaustion and rejected-time
branches can be focused Core checks, without a new exhaustive assertion registry.

### Finite task caller checks

The [controller](../examples/finite_compute_task.cpp) and
[focused tests](../tests/finite_compute_task_tests.cpp) use public compute APIs.
Build normally, then run from the repository root with absolute worker paths:

```sh
./build/robot_harness_two_step_compute normal "$(pwd)/build/robot_harness_compute_worker"
./build/robot_harness_two_step_compute revise "$(pwd)/build/robot_harness_compute_worker"
./build/robot_harness_two_step_compute rebind "$(pwd)/build/robot_harness_compute_worker"
./build/robot_harness_two_step_compute shutdown "$(pwd)/build/robot_harness_compute_worker"
./build/robot_harness_two_step_compute failure "$(pwd)/build/tests/robot_harness_compute_test_worker_early_exit"
./build/robot_harness_two_step_compute unknown "$(pwd)/build/tests/robot_harness_compute_test_worker_delayed_exit"
ctest --test-dir build --output-on-failure -R 'task_'
```

Normal completion prints `first=5 final=55`; revised/rebound goals print
`first=14 final=91`. Each mode checks its expected outcome and closed host before
returning zero. Failure and unknown modes deliberately use test workers; they are
not successful task results. `cleanup=1` refers to host closure, separately from
the printed task state. A missing executable or unexpected outcome returns nonzero.

The six `task_*_example` checks cover those executable paths. Four focused checks
test distinct progression boundaries:

| Check | Observation |
|---|---|
| `task_dependency` | A real delayed-exit child has calculated the first result but still owns native resources; no second step is admitted until accepted result and release. Final result is independently `55`; Core domain verdict remains unassessed |
| `task_revision` | Observe running work, submit two newer goals, reject a duplicate revision, and retain old authority until release. Only the latest goal runs, giving `14` then `91` |
| `task_uncertainty` | Advance only the caller clock while actual child cleanup is pending. Keep uncertainty sticky, reject new goals, admit no retry and continue native cleanup |
| `task_termination` | Native failure prevents step two; external provider replacement does not retry the failed goal. Shutdown during running work and between steps prevents further admission and reaches host closure |

The uncertainty fixture does not inject native `Unknown`, reaper ownership loss,
or cross-restart recovery. It establishes the caller's conservative response to
an unconfirmed observation window; existing Core/process checks retain their own
scope. These ten tests are automatically included by the existing Ubuntu Debug
and sanitizer jobs. All 46 CTests pass on macOS ARM64 (AppleClang 21, SDK 26.5)
and Ubuntu 22.04 ARM64 Docker (GCC 11.4), each in Debug and ASan/UBSan
with UBSan recovery disabled. Independent caller-increment review passed, including
a separate macOS Debug build and all ten focused task checks. After the source
module regrouping, clean builds in all four configurations again passed 46/46.
The seven moved implementation/private files retain identical contents; existing
behavior reviews remain applicable. Public headers and target names are unchanged.
The subsequent withdrawal-allocation fix adds one regression, bringing the suite
to 47 tests. All 47 pass in the same four configurations; the regression fails
against the pre-fix Core library and passes after the fix. Independent focused
review also confirms that allocation failure leaves withdrawal retryable. All six
manual modes above were run successfully with the validated custom build directory
substituted for `build`, retaining the documented absolute-path expressions.
This M3b increment subsequently merged through
[PR #6](https://github.com/xiao-yang25/robot-harness/pull/6) as `eb2ae06`;
its [main-branch CI](https://github.com/xiao-yang25/robot-harness/actions/runs/35447391085)
passed all 47 tests in both configurations. The current larger suite is recorded
in the [validation baseline](#current-validation-baseline).

## PR review and evidence

This section is the contributor-facing review and evidence workflow. Maintainers
also apply their configured shared engineering guidance; contributors do not need
that separate checkout or its local tools to prepare a PR. Use the
[PR template](../.github/pull_request_template.md) to record the proposed
behavior, tested revision, independent review and limitations. Keep personal
model settings and private host logs outside the public PR.

1. Before implementation, select the affected Design behavior and the relevant
   rows above. A planning PR is checked for design consistency; it is not required
   to demonstrate unimplemented runtime behavior.
2. The implementer checks the diff and runs the relevant tests. An independent
   reviewer receives the base/head revision (or base plus the explicitly scoped
   uncommitted diff), applicable contracts, acceptance scenarios and evidence.
   The reviewer reconstructs affected behavior and may run focused probes.
3. Review findings identify location, trigger, consequence, evidence and whether
   they block acceptance. Address blocking findings and obtain a focused re-review
   of the affected changes; the lead agent resolves findings and evidence gaps.
4. Associate the final review with the final code. If review happened before
   commit, compare the reviewed diff with the committed changes. Later behavior
   changes need affected checks and independent re-review; explanatory docs or
   direct include cleanup need an appropriate incremental check, not an automatic
   repetition of the whole review. Record that comparison and any remaining gap.
5. Link the Ubuntu CI run and tested revision. For implementation PRs, required
   tests and required independent review must both be complete for acceptance.
   CI success is not an AI review; an AI review is not a successful test run.
   Report a reviewer failure or missing result explicitly, never as approval.
6. Present the findings, limitations and merge recommendation to the maintainer.
   Merge follows maintainer authorization. After merge, verify the merge revision
   and main-branch CI; reuse earlier evidence when the code content is unchanged.

The initial workflow uses a host-organized independent AI review and the existing
Ubuntu CI. There is no automatic GitHub AI reviewer or enforced AI review status
check configured by this document. A summary in the PR describes the actual
review; it does not impersonate a GitHub reviewer approval. Consider automated
triggering only after observing repeated useful review runs and defining what
happens when the reviewer cannot complete. No AI credentials or new CI permissions
are introduced here.

## Change-to-check mapping

| Changed scope | Check entry and expected result | Status / evidence location |
|---|---|---|
| Core, sample fixture, example and CMake | README configure/build commands; CTest runs `robot_harness.authority_gate`, `robot_harness.sample_execution`, `robot_harness.m2a_failure`, `robot_harness.normal_execution_example`, and `robot_harness.failure_execution_example` | Registered in `tests/CMakeLists.txt`; local CTest output and `build/Testing/Temporary/LastTest.log`, or corresponding custom build directory |
| C++ formatting and naming | Follow [Coding style](CODING_STYLE.md); run clang-format on changed C++ files and review names | Local formatter check; not currently a CI job or behavior test |
| Ubuntu Core workflow | Parse workflow YAML, inspect its commands/permissions and diff; after push, inspect the completed `Core on Ubuntu` job for the tested commit | M0 passed at `0be526a`; M1 passed at `7eb0e76`; M2a normal and ASan/UBSan passed at `16b2888`; see the linked Actions results above |
| M1 normal action/events | Check fresh initialization, active host with not-ready worker, one complete sample operation, a sequential second operation, synchronous/deferred callbacks, rejected input, duplicate/wrong-operation evidence and clean fixture shutdown; compare actual worker submissions and sink results with layered receipts | Implemented: `tests/authority_gate_tests.cpp` and `tests/sample_execution_tests.cpp`; macOS and Ubuntu results passed within the coverage above |
| M2 failure/cancel | Follow the M2 planned checks above: explicit failure closure, cancel ACK before settlement, expiry, missing/partial-effect evidence; no unearned success or conflicting redispatch | M2a/M2b/M2c implemented, including cancellation/deadline tests and examples; macOS and Ubuntu normal/sanitizer suites each passed nine entries |
| M3 replacement/recovery | Actual sink rejects held old output; unsettled conflicts block; provider/Core restart requires fresh observations and authority; invalid recovery stays closed | M3a operation replacement, M3b live-host rebinding and the two-step caller are merged; see [rebind checks](#m3b-focused-acceptance-mapping) and [task checks](#finite-task-caller-checks). M3c has the private Linux [prototype checks](#m3c-prototype-checks); the two-step caller uses its private bridge; stable public recovery APIs remain pending |
| M4 ROS and cross-path behavior | Map supported normal, cancellation, loss, late-output and recovery paths to Ubuntu native observations and receipts | First normal Nav2 observation plus startup/pre-dispatch denial validated locally; see the optional Humble section. Bounded sequential settlement is covered separately above; bounded moving cancellation, replacement and clock/odometry observation loss are covered above; independent consumer isolation is covered above; broader endpoint loss/recovery remain pending |
| Markdown / project instructions | Inspect diff, local links and anchors, code fences, personal-path/credential leakage, and affected command syntax; review any changed normative scope under applicable shared rules | Use the host's available documentation checks or targeted inspection; retain results in the current task record |
| Documentation site | Follow [Documentation development](WEBSITE.md#build-and-preview); run the site tool tests and strict build, then inspect navigation, search and responsive rendering | `Documentation site` CI builds PRs and checks generated pages, anchors and media; only successful master builds deploy through Pages |

Behavior changes involving authority, security/authorization, core public APIs,
concurrency/cancellation,
ownership/lifecycle, persistent state/recovery, data integrity or protocols require
independent review. The maintainer arranges it and records its scope and outcome
using the workflow above; registered tests alone do not complete that review.
Explanatory documentation changes without such effects need appropriate checks,
not an automatic repetition of runtime validation.

No robot target or credential is configured by these instructions. Missing Linux
or hardware evidence remains explicit; writing a test plan does not satisfy it.
Build output is local and ignored by Git. Do not add log archives, private host
details, or a second status registry merely to record a check.

## Repository simulation package

The obstacle-wait increment adds `nav2_obstacle_watch` to automatic Humble CI
and the image build, for six Owner checks total. Its focused cases cover missing
inputs, clear baseline, repeated close observations, incomplete agreement,
replayed/invalid/stale data and future stamps. The manual workflow exposes
`obstacle-wait` and `obstacle-wait-frozen-scan`; changing perception/stop behavior
requires both actual runs plus normal-navigation regression. Check actual box
and robot poses, distinct blocked/unknown reasons, exact-goal cancellation,
native closure/quiet, no delivered output, pending settlement, refused second
admission and actual container cleanup. Unit tests alone do not prove this chain.
The resume increment extends the same predicate test with post-stop sample
boundaries, three fresh clear scans, duplicate/stale observations and occupied
costmaps. Five focused checker regressions reject stale/pre-wait clearance,
identity reuse and accidental admission in fault cases. Actual `obstacle-resume`
must show stop, physical box removal and renewed movement to the original goal
under a new identity. `obstacle-resume-frozen-scan` and
`obstacle-resume-missing-controller` must show no B admission/drive opening and
no resumed movement. The post-admission scan-loss case must explicitly revoke
B, record non-submission and seal its drive without another goal. The B runtime
loss/restoration case must retain withdrawal after native closure and quiet.
Re-run waiting-only and replacement paths when their
shared policy/handoff/checker changes. Automatic Humble CI discovers the tests;
all five new full scenarios are explicit manual-CI options.

The PR #19 counts and recorded results below remain historical; new hosted runs
must identify their own revision before being reported as passed.

The [Humble/Gazebo tutorial](../integrations/ros2/simulation/README.md) builds the
Owner, native worker extensions and drive directly from repository source. The
source package removes the external research launcher/build-mount prerequisite
for the sixteen listed settlement/cancellation/loss/obstacle scenarios. Earlier observations
in this document remain tied to their original fixtures and revisions.

The automatic [Humble workflow](../.github/workflows/ros2.yml) runs six
Owner predicate checks and 35 Python checks: eleven launcher/mirror, seven diagnostic,
eight viewer and nine obstacle-evidence checks. The diagnostic checks use real child processes to verify query success, malformed
pose/nonzero-exit rejection, timeout evidence and process reaping, plus diagnostic-collection/logging
failure and receipt-versus-progress semantics. These do not reproduce the
historical intermittent Gazebo or TF failures.
A separate [manual simulation workflow](../.github/workflows/simulation.yml)
builds the complete image, runs one selected motion scenario and uploads logs
even on failure. Its timeout is 45 minutes, including dependency downloads; it
is not a required check or evidence of a run until executed.

Run launcher and diagnostic checks without Docker:

```sh
python3 -m unittest discover -s integrations/ros2/simulation -p 'test_*.py'
```

These use a fake Docker process to distinguish natural completion, detached
clients, missing verification, nonzero container exit and failed cleanup, and
to reject reused output and prevent output links from redirecting host writes.
Diagnostic write failure must not prevent container cleanup; a create timeout
without a returned ID must retain an uncertain cleanup state and a unique name. They do not establish
actual Docker cleanup or robot
motion. Full simulation and interrupted-container observations must be recorded
separately. The image build runs six Nav2 predicate tests plus the native BT
halt test and the inactive action-executor startup check. The latter checks the
pinned pre-spin cancellation behavior, rejects discovery without a response,
checks both controller/planner action types in a task namespace, and rejects an
active server. It submits no goal. CTest limits this check to 20 seconds. The
default Core build and its ROS independence are unchanged.

Controller TF diagnostics are compiled by the simulation image build, not the
lighter Owner-only Humble job. They read the actual plugin buffer once per second
with zero-wait lookups. Runtime validation must compare raw observations with
`controller_tf_sample` in both task contexts; a separate stationary fault probe
can withhold only the controller's map transform, restore it, disable/restore
AMCL broadcasting, and verify the unchanged readiness response. Include an
initially missing transform and controller deactivate/cleanup. These controlled
faults validate diagnostic distinctions, not the cause of earlier intermittent
failures. Normal navigation remains a separate regression; no diagnostic field
is used as authority or settlement evidence.

Local package validation on 2026-09-24 used Ubuntu 22.04/Humble in an amd64
Docker container. The image build passed the native BT test and all five Owner
tests; all nine tutorial scenarios passed and each owned container was removed.
The normal sequence moved A about 2.516 m and B about 1.827 m. A wrong-image
run with no verifier was rejected, and interrupting an active run returned 130
and removed its container. Actual container settings were checked for no
network, dropped capabilities and the documented resource limits. Eleven host
tests passed separately. These are local package observations, not a completed
remote workflow or a resolution of the earlier TF/query failures.


### Optional simulation viewer

The [local viewer](../integrations/ros2/simulation/README.md#watch-a-live-run)
executes the existing scenario launcher. Python discovery in Humble and manual
simulation CI includes viewer HTTP Host/Origin/token/path/session rejection,
output symlink/FIFO handling, stale/completed frame classification, conservative
result/cleanup handling, duplicate launch and real child shutdown tests. These
host tests do not prove Docker cleanup or robot motion. Linux discovery also runs
five recorder supervision/cleanup fault-injection tests with real GNU `timeout`;
those tests are skipped on macOS. A visual manual CI run decodes its complete MP4
and retains the decode log alongside the recording.

For changes to the visual execution path, build `--visual` and run normal,
cancel-moving and replace-moving with real frames and the existing scenario
checks. Decode each complete MP4, inspect motion frames and preserve the uncut
recording with its same-run evidence. Exercise early encoder failure and a stuck
remux: both must fail and reach cleanup within the existing outer limits.
Inspect the browser's live/final-frame distinction and Owner event
presentation, plus raw output and actual container removal. Check one mid-run
viewer interruption separately; its outcome must be interrupted, not passed or
settled. Preserve failed attempts. A successful headless run does not prove the
optional display tools work, and viewer integration does not establish unfamiliar
contributor usability or physical-robot safety.


## Installed Core consumer

The Core Ubuntu workflow also runs a separate installation check:

```sh
cmake -DSOURCE_DIR="$PWD" -DTEST_ROOT="$PWD/build-install-check" \
  -P tests/installed_core_test.cmake
```

Run from the repository root. This creates a fresh Release Core build with tests
disabled, installs it, moves the prefix, then copies and builds the
[independent consumer](../examples/installed_core/README.md). The consumer uses
only the installed header/library and verifies that missing readiness denies
admission. It inherits C++17 from the exported target. Failure of any configure,
build, install or consumer run fails the check. Per-run directories are retained
beneath `build-install-check` for inspection; remove them when no longer needed.
This separate check does not change the existing Core behavioral test count or
run Gazebo. Sanitizer-instrumented libraries are not reused for its clean consumer.

Optional `CMAKE_CXX_COMPILER`, `CMAKE_OSX_SYSROOT` and `CMAKE_TOOLCHAIN_FILE` values
are passed to both builds; use a compatible native toolchain. This is not a
cross-compilation/emulator test. Package compatibility is limited to matching
architecture and compatible C++ runtimes; it is not a stable ABI or binary release.
The default package installs Core only, as specified in the
[installation boundary](DESIGN.md#installed-core-boundary).
The opt-in Python session has its separate installation path and validation
described in [First Runtime slice](#first-runtime-slice).

<a id="continuous-handoff-profile"></a>
## Continuous ALOHA segments: M5d first batch

### Candidate identity regression (2026-10-02)

The Host rejects integer identity counters echoed as booleans or floating-point
numbers before queueing actions, while allowing unrelated optional metadata.
Three focused regressions cover a malformed observation epoch with zero native
effects and a failed native outcome with completed settlement, numeric aliases that preserve the pending request,
and a valid candidate carrying extra metadata followed by all400 transfer steps.
The existing normal, cancellation and stale-candidate checks remain enabled.

The final patch passed12 handoff checks on Mac/Python3.12, and15 optional backend
checks plus23 ordinary Session checks on Ubuntu22.04 ARM64/Python3.12 through its
installed package. These are explicit no-physics fixtures. The existing Ubuntu
Core workflow discovers the new regressions in `integrations/mujoco/tests`;
no workflow expansion is needed. The patch merged through Harness PR32 as
`0de9eb0`; main Core/Python, Humble and documentation/deployment checks passed.

<a id="m5f-task-owner-comparison"></a>
### Selected task owner comparison (2026-10-02)

A research-only native owner/ACT worker was compared with the installed Harness
candidate containing the identity fix above and Agent3482858. At measurement time,
the published Agent manifest pinned Harness3acc9aa. The later installed-combination
check below is separate from these measurements. Native owns its own framing, scene, queue and
shutdown path; its completion facts are not Core receipts.

Both used Ubuntu22.04 ARM64, Python3.12.14, CPU ACT/OSMesa, seed0,
float32/batch1/chunk100 and four Torch threads, in sequential fresh offline Docker
containers launched with6GiB/6CPU. Both recorded640×480 video and physics for
400 transfer plus50 saved-target hold steps. The task polling interval was10ms.
Model revision was `ba73b2766f1371cdc133ca4efb97eb090d744625`, LeRobot
`e595b7902714ba51f91e47523f66f89c5181b649`, gym-aloha
`bd3325740ea8d1c97411c41ea1e0f4ce0a7de8da`.

| Sequential run | Total wall seconds | Container cgroup peak GiB |
|---|---:|---:|
| Harness1 |36.591|1.815|
| Native1 |36.962|1.867|
| Native2 |37.411|1.873|
| Harness2 |37.609|1.819|

All450 recorded action/physics samples matched across the four runs. Each had
451 decoded frames, completed cleanup and one successful evaluation of the same
fixed finite one-second hold predicate. Evaluation/decoding ran outside timing;
domain/task verdicts remained `unassessed`. Complete status/observation responses
were observed inside actual ACT computation on both paths. With two runs each,
overlapping wall ranges and cgroup accounting including observer/page cache, no
general speed or memory advantage is established. CPU limits are evidenced by the
accepted launch arguments; cgroup memory limits and peaks were directly sampled.
RPC counts have implementation-specific startup boundaries; phase intervals do
not isolate Core overhead.

Matched Harness fault conditions stopped queued work at19 returned steps,
inference-time and declared300ms post-inference-delay cancellation at100, and an
injected wrong observation identity at0. Subsequent hold was refused, output was
not delivered and cleanup was observed. Cancellation acknowledgements do not
withdraw prior effects or establish a hard stop deadline. The finite modification
case changed the post-transfer proposal to help: both paths observed0→400,
completed transfer and closed without hold (401 frames). Harness reused its
existing task consumer; native needed an explicit task-driver help branch.

This shows reuse of the selected authority/queue/resource boundary, while a
task-specific native implementation can provide the related guarantees too.
Worker adaptation, dependencies and domain qualification remain application work.
Historical development time and unfamiliar-user setup were not measured, so no
net engineering time saving is claimed. Independent focused implementation and
evidence reviews approved this bounded comparison. It does not qualify visual
decisions, Owner death/recovery, hard physical stops, other bodies or GPU paths.

<a id="m5-closeout"></a>
### M5 selected scope delivered (2026-10-02)

Agent PR5 merged as `c3b291c`, with a file tree identical to reviewed `8f38cd0`;
its [manifest](https://github.com/xiao-yang25/robot-agent/blob/c3b291c5d39c5ce32df472acfc071a457f509b77/workspace.repos)
pins Harness `0de9eb0`. Both implementation deliveries are on main, and applicable
main CI passed. Agent Ubuntu22.04/Python3.10 ran all30 task/preparation/subprocess
checks and installed command entry points. This does not run the model/simulator.

The new pin was separately consumed through a fresh public Harness checkout,
Release build/install and an installed Agent/default ACT worker outside source
checkouts. In offline Ubuntu22.04 ARM64 CPU/OSMesa, seed0 deterministic proposals
completed observations0/400/450 in epoch0,400+50 steps, two accepted/settled/released
operations,451 decoded640×480 frames and observed process cleanup. The single
same-run fixed evaluator passed the bounded one-second hold; `task_verdict` stayed
`unassessed`. Its38.853-second wall time is a consumption result, not another paired
measurement. Mac and installed Linux application suites each passed30 tests.
Fresh-context independent review approved the pin/worker compatibility and the
scoped normal consumption evidence.

M5 delivers the Core-backed Session, separate existing-agent MCP path, one embodied
business application, public skill/model preparation, selected Mac/Linux normal
qualification, bounded task counterexamples and the comparison above. Real visual
normal/cube-disturbance and mixed-caller temporal results retain their recorded
Harness `3acc9aa` combination; this deterministic pin check does not renew visual
qualification. No automatic recovery, broad reliability, Owner restart/persistence,
hard stopping latency, GPU/other-body support or physical-robot validation is
established. Execution facts, visual interpretation and physical evaluation remain
separate. A versioned preview and external reproduction remain open project work;
the [MIT OR Apache-2.0 licensing and contribution terms](../CONTRIBUTING.md#licensing-status)
are specified separately from runtime qualification.
Full M5 engineering closeout also requires the publicly packaged independent
task evaluator/regressions and pinned cross-repository combination CI. These
follow-up changes are locally prepared; their hosted/mainline delivery is still
pending. The next integration phase then selects one heterogeneous combination
and qualifies its native task before changing Runtime or claiming reuse.

<a id="installed-agent-combination"></a>
### Installed Agent compatibility check

[Installed Agent compatibility](../.github/workflows/agent-combination.yml) calls
one reusable test workflow maintained by
[Robot Agent](https://github.com/xiao-yang25/robot-agent). It pairs this Harness
push/PR candidate (`github.sha`, including the PR merge commit) with fixed Agent
`52ae39d795544a69ae9e8b5a661dea9e3947b6dc`. Publish and qualify that Agent
workflow commit before pushing the Harness caller; do not substitute a moving
branch for the fixed revision. Agent's own PR workflow tests its candidate against
the one Harness commit in `workspace.repos`; paired changes may be checked through
its explicit full-commit inputs.

Both Python packages are installed into fresh prefixes and consumed outside source
trees. The actual public Session launches the real Host/Core and a separate worker;
only environment/recording/policy providers are explicit no-physics fixtures.
Three cases check normal400+50 effects/held target and distinct settled receipts,
help without hold, and a stale answer without hold, with process exit checks.
No ACT, model, MuJoCo dynamics, recording or physical-success qualification follows.
This workflow is read-only, inherits no secrets, and adds no model/Agent dependency
to the Core package. The compatibility test implementation remains in Agent;
Harness does not copy it or introduce reverse runtime dependencies.

Local macOS and Ubuntu22.04 ARM64/Python3.12.14 installed-boundary checks each
passed all three cases against runtime pin `0de9eb0`; the current Harness changes
only CI/docs. Agent's30 unchanged checks plus11 evaluation regressions passed;
the final resource fix reran the affected11 in both environments. Hosted
Ubuntu x86_64/Python3.10 results and delivery of both batches remain unverified.


### Previously delivered simulation evidence

At the first M5d batch, the opt-in `mujoco-stepped-handoff-v1` implementation had
a deterministic caller; the model-backed business Agent was not yet implemented. Local Mac/MPS execution
completed400 transfer steps plus50 saved-target hold steps in the same epoch,
with two distinct Core operations and accepted output/settlement. Observations
progressed0→400→450, a stale post-transfer reference was refused, explicit reset
created the next epoch, and old receipts stayed unchanged. Both processes were
observed reaped. All450 action/qpos/qvel/control/contact/time samples matched the
previous native continuation directly;451 video frames decoded, plus one frame
for the explicitly reset idle episode.

The fixed independent research task predicate accepted the same-run finite
one-second hold. This is one native combination/seed, not visual-Agent acceptance,
long-duration grasp reliability or a public evaluator/skill distribution. Core's
domain verdict and execution summary task fields remain `unassessed`.

Actual simulator cancellation checks stopped transfer at12 returned steps and
hold at8 returned steps (episode sequence408). Neither advanced after the Host's
recorded revocation step; both settled as cancelled without successful output,
and follow-up hold was refused. Send time is still not revocation, and a blocked
native call can delay Host progress. These observations do not establish a hard
stop latency or Owner-death protection.

The lightweight backend suite adds nine Host/Core/continuous-backend tests using
an explicit no-physics sink and a separate binary test worker. They cover retained
epoch/operation counters and target equality, no hold prediction, preconditions,
unresolved/queued cancellation, expiry inside the last step, recording failure,
an unsolicited old prediction, expiry before dispatch with a new0-step identity,
and preparation-recording failure with non-submission settlement. Together with
the three existing backend checks,12 tests pass locally using NumPy without loading a model or renderer.
The23 ordinary Session tests pass, including default and explicit fixture skills.
The optional Ubuntu job discovers these new backend tests through its existing
command; this batch's hosted CI has not been run. Actual Linux ACT/render and
business-Agent evidence remains required in its own stage.


Independent implementation review initially found two preparation/non-submission
recording defects. Both were fixed and verified with targeted regressions: the
operation owns its counters before dispatch, and preparation failure uses factual
non-submission evidence rather than a terminal event for unaccepted native work.
Preparation changes metadata only; native stepping/prediction still requires
Core dispatch. Final normal/cancellation runs and the Session/MCP checks were
repeated on the repaired implementation. The five legacy ACT trials matched before
this repair; their no-op preparation path is unchanged. Relocated fixture/MCP
consumers and continuous-backend import also verify package consumption.

Independent review of the repaired patch and its final normal/cancellation evidence
concluded APPROVE. This qualification is local and uncommitted; hosted CI/mainline
delivery, the business Agent and Linux/model/render support remain separate.

## Native identity bridge support

The private Python bridge accepts a trusted owner's explicit native identity while
retaining the legacy three-argument episode identity. Seven focused checks use the
real compiled C++ Core: legacy completion, explicit identity retention and wrong
identity refusal, pending acceptance, late acceptance/cancellation and late success
after revocation, old-operation isolation, text validation, and owner-thread refusal.
Acceptance or a terminal event does not itself settle an operation; a late success
cannot publish a revoked result.

Local macOS ARM64/Python3.12 checks pass: the ordinary Session suite plus the seven
checks (30 total), 15 lightweight MuJoCo backend checks and the existing MCP suite.
These are bridge/consumer regressions, without a new model, renderer, navigation
Session or robot stop qualification. The Ubuntu workflow already discovers Python
tests in `bindings/python/tests`; no job change is needed for these new checks.
Hosted CI and installed navigation consumption have not run for this increment.

## Shared execution coordinator extraction

The private `robot_harness._execution.ExecutionCoordinator` is now consumed by
the existing Host. Seven focused tests use the real compiled Core to check
retained/refused requests, replay/conflict/capacity, pending native facts, explicit
settlement and later driver release, old cancellation, late acceptance after
revocation, expiry during publication and required output disposition. Native
completion, output and settlement remain distinct; an unknown business verdict
does not waive Core's result-disposition requirements.

Local macOS ARM64/Python3.12 checks pass: 37 Session/Core tests, 15 lightweight
MuJoCo backend tests, 15 MCP tests, and a relocated installed-package consumer
performing two tasks and reading the first retained receipt. Existing Session
signatures, profiles and prior test assertions are preserved. The installed
package includes the new private module. The existing Ubuntu discovery and install
consumer commands cover it without a new CI job; hosted CI has not run.

These checks validate the extraction with the existing Host, without a new model
or renderer run. Independent implementation review approved the extraction and
reran the seven focused coordinator checks. At that extraction checkpoint, actual navigation driver consumption, heterogeneous ALOHA/Nav2
reuse, navigation-Agent behavior and the new physical closure profile remain
unqualified. See [the extraction boundary](RUNTIME_SLICE.md#first-binding-increment-and-shared-coordinator-extraction).

## Scoped navigation research consumption

The private bridge now accepts an optional settlement scope, preserving its
six-argument constructor and episode default. Three additional real-Core tests
verify actual scoped settlement, unresolved-native refusal and configuration
validation. Local macOS ARM64/Python3.12 checks pass: 40 Session/Core tests,
15 lightweight backend tests and 15 MCP tests. The existing Ubuntu test discovery
includes the new scope tests. These local checks preceded the hosted
[delivery checks](#m6b-delivery-ci-coverage).

A bounded, deterministic Humble/Nav2 research driver consumes the compiled C++
Core and the same private coordinator used by the existing Host. Six real-Core
transport checks and three evaluator checks pass. Three further checks execute
actual driver methods under the existing Linux/ROS environment with synthetic
edges: expiry during reservation blocks submission, native cancellation errors
do not block outlet-close requests, and readiness lost during recording prevents
settlement. These are boundary tests, not physical cancellation qualifications.

The normal Linux/amd64 simulation used the existing image without network,
2 CPUs and 4 GiB. It passed in 55.293 seconds: independently observed target errors
were 0.214737 m at A and 0.154156 m at B. Actual native/child/BT closure, manager
PAUSE, matching outlet acknowledgement and fresh quiet observations preceded
closure publication. A rechecked B readiness before settling and releasing;
B finished and closed with its Core settlement pending because no C context
was prepared. A retained old cancellation left B unchanged. The container exited
successfully without OOM and was removed.

The preceding run failed when B's native planner acknowledgement timed out;
its failed/pending/unknown diagnostics are retained. The successful run does not
establish that root cause was fixed. The evaluator's linkage checks were tightened
during the successful run; the initial evaluation was retained and the final
evaluator checked that same new evidence once. The final post-readiness-log guard
was added afterward and verified by the actual-driver boundary test, without
repeating the complete physical run. Independent review approved this limited
research path and independently checked its receipts and ordering.

This is neither an installed public navigation Session nor a live business Agent.
Real ALOHA/Nav2 business consumption, externally routable navigation cancellation,
general late-future qualification, installation/CI combinations and hardware
remain separate work. The coordinator and C++ Core behavior were not changed.

## Navigation request entry

The optional Python package installs the new public `NavigationSession` client
and trusted-owner `NavigationRequests` support. The client uses the existing
bounded local framing and connects to an already prepared Unix endpoint; it
does not launch ROS or own the owner process. The helper borrows the shared
coordinator and never supplies native/physical settlement facts itself.

Nine additional checks pass with the compiled real Core or actual Unix transport:
registered-site requests, advancing observations, retained replay/conflicting ID,
expired/foreign/changed-epoch/invalid-issued references, delay inside a context
callback, active pending/busy/capacity refusal, old cancellation, failed stop
scheduling and repeated shutdown, timeout/late RPC correlation, EOF and client
cleanup. Final macOS ARM64/Python3.12 totals are 49 Session/Core tests, 15 lightweight
backend tests and 15 MCP tests. The first transport attempt hit the local sandbox's
Unix bind restriction; the final suite ran with the required local socket permission,
without skipping tests. Existing Ubuntu discovery includes the new tests; these
local checks preceded the hosted [delivery checks](#m6b-delivery-ci-coverage).

Linux/amd64/Python3.10 build and install succeeded in the existing offline Humble
image. A separate client process imported only the public request API from the
installed prefix, outside the source tree; the owner continued servicing requests
and progressing native/Core work. Its final normal run passed in 55.545 seconds,
2 CPUs/4 GiB, without OOM. Independent target errors were 0.241817 m and 0.140541 m.
The single evaluator checked physical/native closure, A settlement before B
admission, public observation/request/native identity association, request replay,
old cancellation, retained A receipt and B's pending settlement. The client exited
with code0 and the owned container was removed. Close afterward revoked B and
kept settlement pending; connection cleanup was not called physical closure.

The first startup attempt failed at socket chmod in Docker Desktop's shared output
directory, before any native goal. The endpoint was moved into an owner-created
private directory on the container filesystem, with socket mode0600. Failed logs
were preserved. The directory is released after client reaping and socket closure.
Two checks execute final actual owner RPC/abort methods with real Core and synthetic
ROS edges: EOF and close revoke and schedule the matching native UUID and exact
scope/generation outlet request, retain pending and report unknown. These confirm
request scheduling, not physical stop after disconnect or motion-time cancellation.

Independent design and final implementation review approved this limited slice.
The bounded installed owner is available as described below. The new heading profile
completed standalone normal reproduction. New request-path physical qualifications, live navigation
Agent, installed two-business-task consumption and hosted CI remain pending. No new dependency, image, model call, hardware action or Agent pin was added.


### Installed bounded Nav2 owner checks

The optional [owner package](../integrations/ros2/nav2_session/README.md) installs
with the Python bridge. Final macOS ARM64/Python3.12 regression: 56 tests, including
private endpoint modes, batched replies, EOF, malformed RPC, partial socket startup,
and package/startup isolation checks without ROS. Existing Ubuntu test discovery
includes the endpoint/package additions and two native profile checks. The host launcher has 12 focused checks; its Session
case verifies separate readonly source/prefix/example mounts. This records local
qualification before the hosted [delivery checks](#m6b-delivery-ci-coverage).

Linux/amd64/Python3.10 build/install succeeded in the retained Humble image. Ten
checks exercise installed actual driver methods with real Core and synthetic ROS
edges: dispatch and settlement rechecks, independent native/outlet scheduling,
late acceptance after outlet failure, logging failure cleanup, idle close, partial
resource cleanup, B preparation failure with an actual OS child reaped, and
actual endpoint close/EOF revocation plus exact native/outlet scheduling. These
are not physical cancellation/EOF stop evidence. The first check invocation
incorrectly replaced the sourced ROS Python path; preserving that path fixed the
invocation without changing predicates. Nine existing research checks also passed.
Unchanged lightweight backend/MCP checks retain their preceding 15/15 evidence.

A real installed owner plus independent public example passed normal A→B in
62.412 seconds, 2 CPU/4 GiB, no OOM. External target errors were 0.243104 m and
0.198024 m. One evaluator checked native/physical closure, request/reference/native
identity association, A settled/released before B admission and B pending. The
runtime consumed no evaluator capture markers. The research supervisor supplied
only bounded container ownership and a separate external observer. Both client
and container exited 0, and the owned container was removed. An earlier attempt
failed before startup when Docker tried creating a child file mount in a readonly
parent mount; the example now mounts at a separate root path. Failed logs remain.

The separate public `simulate.py session` invocation started the same installed
owner and independent example without research mounts. A settled successfully;
B was accepted and the bridge produced nonzero commands, but Nav2's controller
reported `Failed to make progress` under its original 10-second progress check.
Native outcome was failed; output stayed pending, abort revoked authority and
kept stop unknown/settlement pending. Container exit1/no OOM and removal were
observed. The exact controller cause is unresolved. This old-profile run was a
normal-reproduction failure, not a successful CLI qualification or proof
that the earlier planner-ack failures share a cause. Do not silently retry tasks
or widen Nav2 limits to make this evidence pass.

The current installed profile is `scoped-two-context-nav2-shim-v1`: native Nav2
1.1.20 Rotation Shim wraps unchanged DWB. No dependency or image was added.
The original progress (0.5 m/10 s), goal, observation, quiet and closure gates
remain. Shim desired angular speed 0.8 rad/s is not a whole-route physical cap;
native fallback can delegate early. This is explicit heading policy, not a proven
root-cause fix. A read-only observer run of the old profile also passed; observer
timing effects and exact DWB/smoother/input causes remain unresolved.

The new profile's installed physical normal run passed in 61.748 seconds under
the same 2 CPU/4 GiB budget. A/B target errors were 0.236923/0.173668 m. B's initial
heading error was 2.919885 rad, nearly stationary rotation was observed, and
translation exceeded 0.5 m after 5.4 simulated seconds. Native logs show actual
shim and DWB load in both contexts; one evaluator checked the original physical
and Core predicates plus this initial motion. A released/settled before B
admission; B remained pending. Client/container exited 0, no OOM, container removed.

A separate public `simulate.py session` run with that installed candidate,
without research mounts or an observer, completed in about 61.4 seconds: exit0,
no OOM and exact container removal. Its verification checks process completion,
not independent physical arrival or stop. Failed and diagnostic old-profile
evidence is preserved; no automatic task retry or progress-budget extension was used.

Independent implementation review approved this fixed normal heading profile,
including versioned native semantics and raw runtime/installed-source evidence.
Separate finite public moving cancel/EOF evidence follows below; next consume
actual ALOHA/navigation business tasks. General ROS, hardware, owner restart,
context replenishment and hard stop limits remain outside this profile.

### Public motion-time protection checks

Two external public-API consumers used the same installed shim-profile owner:
each settled/released A before submitting B. Public cancel was issued during B
rotation (0.623704 rad/s, 0.001036 m/s); abrupt EOF used SIGKILL of the actual
connected child during B translation (0.259627 m/s, 0.513473 m displacement from
B's initial position, child return code -9). The parent held no socket. The
cancel child kept its connection open through the external physical window,
so EOF/close did not substitute for the explicit cancel trigger.

Both showed actual current Core revocation before exact retained native UUID
cancellation scheduling, matching B scope/generation applied outlet sealing,
and no further admission or reopening. A separate bounded producer then sent
nonzero old B smoothed commands through the existing fixed bridge. Sustained
injection, forwarding and drive rejection each covered both Gazebo snapshots
and the whole fresh quiet interval, with the original 0.25-second coverage gap.

Cancellation: 33 quiet samples/3.196 simulated seconds, 84 injected/84 forwarded/
106 rejected commands; external drift 0.000003162 m and yaw change 0.000087 rad.
EOF: 34 samples/3.298 simulated seconds, 87 injected/87 forwarded/109 rejected;
drift 0.000053935 m and yaw change 0.00208 rad. Original 0.01 m/0.02 rad drift,
0.01 m/s/0.02 rad/s speed, at least 10 samples/one simulated second and fresh
observation predicates were retained. These were fixed observation intervals,
not quiet intervals selected after looking at the data.

Runtime/container completion was exit0/no OOM in 47.305/50.131 seconds under
the same 2 CPU/4 GiB isolated Linux budget. Both initial offline readers stopped
on mixed JSON/ROS stderr, before semantic evaluation; failure records remain.
The final reader strictly parses JSON records and retains optional diagnostic
text without treating it as coverage. Each recorded run then had its first
complete semantic evaluation, without a physical rerun or repeated full adjudication.
Four focused anti-vacuity checks passed. The host independently removed and
checked each exact container. Independent final review approved this finite scope.

The owner kept result/output pending, settlement pending and stop unknown.
The configured native callback tail was still running after the two-second abort
report; later worker-return facts do not constitute a complete native closure
certificate. External quiet does not upgrade the receipt to confirmed stop or
permit reuse. Product runtime code, API, waits and cleanup were unchanged in this
increment; prior 56/10/12 checks retain their earlier scope, not a new run here.
No new image, dependency, model call, hardware action or hosted CI was added.
The consumer parent preserved the scene while the owner independently aborted:
this qualifies a live-owner fault path, not direct launcher teardown, owner death,
outlet failure, a hard stop bound or an arbitrary robot installation.


<a id="m6b-shared-consumers"></a>
## Recorded local qualification: installed ALOHA and navigation consumption

The recorded local Harness candidate preserved the existing installed Agent's
public `Session` / `HandoffTask` consumption. A Linux ARM64/Python 3.12.14 run
used the retained pinned ACT environment, CPU and OSMesa under 6 CPU/6 GiB,
network disabled and a 240-second experiment budget. It completed in 38.824
seconds: observations 0/400/450 in epoch0, distinct transfer400/hold50 operations,
accepted output and settled/released receipts. All 451 frames decoded at 640×480;
the final control target remained fixed for the 50 hold steps. The sampled caller,
owner, worker and recorder exited, with no matching process identities remaining.
This is finite observed cleanup, not a hard stop guarantee or complete transient
process census.

The installed Agent used deterministic proposals and its real default ACT worker.
The existing fixed evaluator ran once after closure and reported a one-second
hold success: maximum relative translation 0.0254mm, rotation 0.346 degrees and
minimum table clearance 0.2799m. Agent/Core `task_verdict` remained `unassessed`.
No live visual-model request was made; earlier visual qualification retains its
original version.

The retained [installed navigation normal run](#installed-bounded-nav2-owner-checks) uses the
same current shared coordinator source, compiled separately for Linux amd64 /
Python 3.10. Direct comparison of 18 installed modules in both prefixes matched
current source. This increment did not alter runtime code and reused navigation's
existing evaluation, without re-adjudicating it. Navigation A settled before B
admission; B output was accepted but settlement remained pending without a next
context. Separate profile-specific native closure conditions remain necessary.

Fresh ARM64 checks passed 56 Session/Core, 41 Agent and three installed combination
cases. The latter use no-physics providers and do not replace the actual ACT run.
Four bounded experiment containers and the read-only preflight were removed;
retained images, models and useful candidate build/install evidence remain.
No new image, dependency, model download, hardware action or hosted CI run was added.
A fresh independent read-only review checked installed equivalence and focused
raw receipt/ownership facts, approving this finite scope. It did not rerun the
physical evaluator or inherit other qualifications.
This run established local candidate compatibility. At that time, the published
Agent manifest still pinned `0de9eb0`. Subsequent navigation Agent consumption is
recorded below; the later delivered pair and hosted checks are listed under
[M6b delivery CI coverage](#m6b-delivery-ci-coverage).


<a id="m6b-navigation-business-agent"></a>
## Installed navigation business Agent normal consumption

A fresh installed Agent candidate consumed this same qualified Nav2 owner/client
through the public `NavigationSession`. The application owns its A→B goal, bounded
proposals and waiting; Harness owns native execution/Core/outlet closure. Three
actual host Codex text proposals used only public map feedback, with tools disabled.
Original proposal references were retained, then fresh references revalidated at
submission; the owner still enforced its one-second admission reference lifetime.
No simulator ground truth or physical evaluator output drove the application.

The final isolated Ubuntu22.04/Humble amd64 run completed in 87.722 seconds under
2 CPU/4 GiB, network disabled and a 420-second host budget. The single independent
physical evaluator confirmed A/B distances 0.205273/0.173651m. A was settled and
released before B admission. B output was accepted but settlement remained pending;
local connection close and native abort did not fabricate release. The Agent kept
its task verdict unassessed and native cleanup unknown. All three proposal children
and the host model server exited/reaped; the container exited0/no OOM and was removed.

The first run's initial model proposal exceeded30 seconds before any admission;
its failed logs and unknown close outcome are retained. A same-input/same-budget
proxy-path diagnostic returned in 8.945 seconds without submitting a robot task,
then a new isolated task passed. The exact original timeout cause remains unknown.
There was no change to Core, owner/controller policy, freshness gates or native waits.
The host proposal relay is research setup, not a product remote-provider interface.

Final Agent checks passed 56 on macOS and installed Linux ARM64, plus three installed
ALOHA combination cases. New navigation checks entered the existing Agent test discovery
and CLI-help CI step; this qualification preceded the candidate's hosted CI. No new
image, dependency, model download or hardware qualification was added. This normal
case does not qualify model errors during real motion, general navigation/obstacles,
owner loss or hard stop/resource limits. The subsequent public pairing and hosted
CI do not expand this recorded simulation scope.

### Caller budget compatibility and recorded model consumption

A new recording exposed a genuine caller/owner budget mismatch: after B, the
owner's default 10-second final-close wait expired while a roughly10.03-second
proposal returned under the application's unchanged 30-second limit. The report
remained needs-help with unknown operation outcome/native cleanup after connection
reset; the local connection closed. No successful video was
published from that failed run. The owner now accepts explicit finite caller
idle waits in `(0, 60]`, keeping the default15/10-second behavior. Four regression
checks cover defaults, final12-second failure under the old default and success
under explicit45, expiry above45 with abort, and invalid values before ROS startup.
The new Linux installed candidate passed60 Python/Core checks and10 focused owner
checks;12 launcher checks also passed. Existing discovery includes the new tests;
these local checks preceded delivery and are distinct from the hosted results below.

A separate recorded run configured45 seconds of caller idle wait, retained the
30-second proposal budget and existing native deadlines, freshness and physical
predicates, and completed in107.642 seconds.
Three actual Codex/GPT text proposals visited A, visited B and assessed completion.
The same-run evaluator ran once, observing A/B distances0.192253/0.166768m; A
settled/released before B, final B accepted/pending. Agent task verdict stayed
unassessed and native cleanup unknown. Recorder completed cleanly, all three
proposal children/model server exited and were reaped, and the exact scene
container was removed without OOM. The [70.9-second recording](assets/demo/README.md#model-guided-navigation-local-candidate)
retains model waits and original speed; trim/crop/captions/final hold are declared.
It demonstrates one normal loop, not broader model reliability or hardware safety.


## M6b delivery CI coverage

The runtime changes are delivered in Harness
[`6f32578`](https://github.com/xiao-yang25/robot-harness/commit/6f32578f8d9157fa7c26c1f4e2ece13e05ec6211).
Agent [`9516daf`](https://github.com/xiao-yang25/robot-agent/commit/9516dafb13d5df8e64c0491d9c8f01cce54a4ff8)
pins that Harness revision. Its Ubuntu22.04/Python3.10 mainline
[application workflow](https://github.com/xiao-yang25/robot-agent/actions/runs/37107518275)
passed 56 checks and installed CLI validation; its
[installed combination workflow](https://github.com/xiao-yang25/robot-agent/actions/runs/37107518308)
passed five cases: three retained ALOHA cases and two navigation cases. The latter
consume real Session/Requests/coordinator/Core with controlled native and model
providers; they do not run ROS physics or qualify model reliability.

The [presentation update](https://github.com/xiao-yang25/robot-harness/commit/6394da08f3c9b9f91707c6f21396f7a0d8b99c17)
is also merged. Its Harness mainline
[Core/Python](https://github.com/xiao-yang25/robot-harness/actions/runs/37109841581),
[Humble](https://github.com/xiao-yang25/robot-harness/actions/runs/37109841649),
[fixed older Agent compatibility](https://github.com/xiao-yang25/robot-harness/actions/runs/37109841989)
and [documentation build/deploy](https://github.com/xiao-yang25/robot-harness/actions/runs/37109841546)
all passed. The Harness compatibility workflow retains its earlier Agent pin;
the new navigation pair is checked by the Agent workflow above. Presentation
does not change the dependency pin or the recorded physical qualification.

The Core workflow's existing `python_session` discovery includes the shared
coordinator, native identity, navigation requests, local endpoint and caller-budget
regressions. Lightweight MuJoCo and MCP checks, relocated Session/MCP consumers
and Core ordinary/sanitizer/installed checks retain their existing entries.
The Humble workflow now also builds and installs the optional Python bridge,
relocates its prefix, then runs the installed Nav2 owner CLI help and ten focused
owner checks outside the source directory. It preserves sourced ROS/interface
Python paths. The checks exercise real Core and owner methods with synthetic ROS
edges; they do not run Gazebo, models or certify physical stop. Existing Humble
observation and simulation-launcher checks remain. Hosted results must be tied
to the delivered revision; prior local qualifications do not establish a new CI pass.

## Final proposal cancellation candidate

This section records the pre-delivery validation. The fix is delivered in
Harness `ea7b1eb`, paired by Agent `ffad065`; the
[published recording](assets/demo/README.md#final-proposal-expiry) preserves that
run's scope. Later delivery does not repeat its physics qualification.

Native A/B completion leaves final B pending in Core. The local owner candidate
routes explicit cancellation at that point through client shutdown, preserving
accepted native output and pending settlement. Completed normal close retains its
existing terminal wait path; neither path creates a task verdict or native cleanup
evidence. The focused cancellation regression failed against the prior installed
owner and passed after the fix. All 12 focused owner checks then passed against a
separate installed candidate in the retained Ubuntu 22.04 / Humble environment.
Existing Humble workflow discovery runs this entire check file; no workflow change
is needed, and these local results are not new hosted qualification.

A controlled-provider installed Agent simulation first exposed the defect, then
a new Gazebo run passed one offline evaluation in 108.362 seconds. Actual A/B
visits and scoped closure were retained, with independent distances
0.198780/0.159009 m; A released before B admission. The final proposal exceeded the
unchanged 30-second deadline and produced a valid answer while terminating, but
was not accepted. Exact B cancellation revoked authority while its native result
remained succeeded/output accepted with settlement pending. The Agent reported
needs_help/unassessed/native cleanup unknown and exited 1; owner/container exited
0, without OOM, and the container and child processes were removed or reaped.
Status/close connection errors remain explicit. The first failed run is retained.

The optional Python package was installed from current sources into a separate
prefix using the retained Core build; this was not a fresh full Core build.
Agent runtime is unchanged. Its nine synthetic installed combination checks on
macOS and three new Linux proposal checks used the prior manifest pin; the real
simulation used this matching owner fix. These are pre-delivery local results;
paired delivery requires a fixed Agent dependency and installed verification
against the delivered Harness revision. Hosted results belong to the exact commit
in Actions. The same-run video was subsequently published as linked above. Controlled
proposal faults and a live owner do not establish real model faults, Owner restart,
complete native cleanup, hard stop or hardware safety.

## Nav2 preparation diagnostics and supervisor failure propagation

The isolated Session launcher checks each owner, scene and bridge child before
accepting its endpoint file. Any child exit is reported with role, PID and wait
status and enters the existing cleanup path, even while the other two remain
alive. The final scene/bridge check also requires both processes to be alive.
The 160-second endpoint deadline is unchanged.

Three real-child supervisor checks passed on macOS and Linux: each failed role
is rejected despite live siblings and an existing endpoint file, readiness with
all children alive succeeds, and a missing endpoint still times out. The focused
installed Humble owner suite passed 14 checks, adding preparation service
discovery/response diagnostics without changing its service or context budgets.
The existing partial-startup OS child-reaping regression remains intact. These
are process and method checks, not physical stop or general ROS reliability.

`context_preparation_failure` records the failing B service, discovery/response
phase, stage budget, elapsed time and context exit status in `caller.jsonl`.
It preserves the original exception. In particular, a GetState response timeout
is not treated as a retry or proof that its underlying cause has been fixed.
The existing Humble workflow discovers both new supervisor tests and owner
checks; a specific hosted result is required before delivery is marked passed.

## M6c delivery audit

The selected public-CLI interruption and proposal-failure paths have recorded
live-owner simulation evidence. The matching final-cancellation fix is delivered
in `ea7b1eb`, paired by Agent `ffad065`. Same-strategy native normal/late-proposal
comparisons and a fresh fixed-public-source Linux build/install normal run were
subsequently completed in research. The fresh consumer used new Core/Agent
install directories in the existing Humble image, rather than rebuilding that
entire image. These runs preserve pending settlement, unknown cleanup and the
independent evaluator's role; they do not establish a performance advantage.

Startup diagnostics are separately delivered in `03a357b`. Its mainline Humble
checks passed all 14 owner and 44 launcher/diagnostic cases, alongside Core/Python,
installed old-Agent compatibility and documentation build/deployment. This does
not move Agent's immutable `ea7b1eb` dependency or migrate recorded qualification.
GetState preparation-timeout cause remains unresolved.

The original audit found that full business reproduction required research-only
control wrappers. That delivery gap is now resolved by the Agent's
[public navigation tutorial](https://github.com/xiao-yang25/robot-agent/blob/master/examples/navigation/README.md),
using the trusted-client launcher below, fixed dependencies and new installations.
A launcher completion marker remains distinct from a physical task verdict.
The final bounded delivery scope is recorded in [M6 closeout](#m6-closeout).

### Trusted external session client

The session launcher now accepts a local `--client-script`, optional read-only
`--client-prefix`, and explicit `--caller-wait-seconds`. Defaults retain the
independent request example. Only the client's Python path sees the additional
prefix; no Agent strategy or provider is imported by the owner. Three added
launcher checks cover read-only mounting/idle forwarding, failed-client exit and
exact cleanup, plus rejection of unusable client configuration. Existing
simulation discovery includes these checks; qualification belongs to the final
installed run and corresponding hosted revision, not the presence of options.

### B context startup ordering

Public tutorial qualification exposed two zero-admission preparation failures:
B's first lifecycle service was not discovered within the 5-second RPC budget
while its process was still initializing. B now follows A's startup ordering:
wait for the native action endpoint inside the existing 70-second context deadline,
then query lifecycle/readiness services with the unchanged bounded RPC waits.
The endpoint marker is not full readiness, authority or permission to move.
A context-process exit prevents service queries; the final lifecycle, transform,
map and fresh-observation checks still precede admission. Two focused actual-driver
checks cover ordering/deadline and process-exit rejection. Historical response-time
failures remain separate; this change does not establish general reliability.

<a id="m6-closeout"></a>
## M6 closeout: two consumers of shared execution

The declared M6 slice is delivered: the ALOHA Host and bounded Nav2 Owner consume
one execution coordinator for retained requests, Core authority, deadlines,
result disposition and explicit settlement. ACT stepping and Nav2 control/closure
remain dedicated backend responsibilities; task decisions stay in Robot Agent.
The coordinator and installed examples do not form a general task or skill SDK.

Recorded normal tasks, selected public-CLI interruption and proposal faults,
and matched native normal/late-proposal comparisons cover their stated conditions.
The comparison identifies responsibilities and single-run resource costs; it does
not establish a net performance, reliability or development-time advantage.
Earlier input-loss/closure probes retain their original profiles and do not
qualify every later profile. There is no general navigation physical evaluator;
the Agent's installed task evaluator applies only to ALOHA.

Agent [`0c219f5`](https://github.com/xiao-yang25/robot-agent/commit/0c219f5b62c24a497f40fda8ff545ab4d38eda4d)
delivers the controlled and host model tutorial, pinning Harness
`13bc75e903f6005da3dd5d969f8242ce211d182f`. New public-source Linux/host installs
completed one real three-proposal navigation task; its separate finite physical
evaluation and local process/container cleanup passed. The
[Agent qualification](https://github.com/xiao-yang25/robot-agent/blob/0c219f5b62c24a497f40fda8ff545ab4d38eda4d/docs/TESTING.md#public-host-model-navigation-workflow)
records the versions, failures, outcomes and limits. Its mainline application and
installed combination CI passed 67/12 checks. Harness retains the independent
older-Agent compatibility pin; this documentation update changes no runtime or
manifest and does not repeat prior qualifications. Published recordings keep
their own source versions and scope.

Final B acceptance, pending settlement, unknown native/remote cleanup and an
unassessed application verdict remain valid distinct outcomes. M6 completion does
not imply Owner restart recovery, a hard stopping deadline, physical hardware,
model reliability, stable APIs or public preview readiness. Agent licensing and
external adoption remain separate work. The next integration begins with a
specific third task/backend and environment preflight, before new assets or GPU
capacity are provisioned.

<a id="finite-owner-motion-permission"></a>
## Finite Owner motion permission checks

The optional finite Nav2 profile keeps the default v1 and public Session/Core
API. Its contract is in [Design](DESIGN.md#finite-owner-motion-permission).
These checks cover separate boundaries:

| Check | Actual scope |
|---|---|
| `nav2_motion_permission` | Literal policy traces: issue-based deadlines, normal A/B handoff, identity rejection, exact expiry, clock reversal, fault latch; controlled expiry after calculation and between native writes |
| Installed `linux_driver_checks.py` | Actual Owner methods and real Core with synthetic ROS edges: unsolicited fault ACK leads to abort/revocation, keeps output/settlement pending; malformed ACK rejection, missing-service startup refusal and unresolved-renewal handoff refusal |
| `native_drive/linux_protocol_checks.py` | Actual Gazebo wheel plugin and ROS callbacks: finite mode refuses old wire/late renewal/new generation after expiry; default generation mode retains open/close and ACK fields |
| Separate navigation runs | Actual installed Owner/Core/bridge/native workers and external motion observations; process or protocol unit checks alone cannot qualify physical arrival or stopping |

Automatic Ubuntu 22.04/Humble CI builds the private messages, installed Owner
and pure permission checks. A second job builds the actual Gazebo 11 drive and
runs the finite/legacy protocol fixture, retaining its raw logs as an artifact.
The protocol fixture disables gravity and does not adjudicate braking or a
physical navigation task. Existing manual simulation runs retain their scope.

After building/installing the private interfaces and drive in a Humble Linux
environment, the focused checks can be run with:

```sh
ctest --test-dir build-nav2 -R '^nav2_motion_permission$' --output-on-failure --no-tests=error
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" \
  python3 integrations/ros2/nav2_session/tests/linux_driver_checks.py
ROS_LOCALHOST_ONLY=1 ROS_DOMAIN_ID=76 M4_ISOLATED_SIMULATION=1 GAZEBO_MODEL_DATABASE_URI='' \
  GAZEBO_PLUGIN_PATH="/drive/lib:${GAZEBO_PLUGIN_PATH:-}" \
  python3 integrations/ros2/simulation/native_drive/linux_protocol_checks.py
```

The last command requires a fresh writable `/output` in its isolated container
and a sourced custom-interface setup. It owns and reaps only its child Gazebo
processes. No Owner restart, hardware, hard stopping-time or native worker
termination qualification follows from these checks.

## Navigation cancellation settlement

The explicit revision profile uses the existing shim/native closing graph.
Ubuntu 22.04/Humble CI builds and relocates the installed bridge, then runs both
`linux_driver_checks.py` and `linux_revision_checks.py`. The latter uses real Core
and public endpoint with controlled ROS edges: same-tick cancel, success racing
cancel, retained late acceptance, missing terminal/outlet/lane/quiet/readiness,
original deadline expiry, global close and EOF during closure. These checks
prove scheduling and Core dispositions, not physical stopping. CLI/profile
selection and host forwarding are covered by the existing package/launcher suites.

After sourcing Humble and the current private fixture interfaces:

```sh
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" \
  python3 integrations/ros2/nav2_session/tests/linux_revision_checks.py
```

Separate local installed real-Humble runs observed moving accepted A, exact
public cancellation, actual cancelled terminal, scoped closure, fresh quiet
and six B readiness responses before Core release and public B admission.
One normal reuse run released A after about 4.89 seconds and observed B within
the existing 0.35 m tolerance. A controlled actual B-domain pause returned
lifecycle state 2: A output/settlement stayed pending and B was never sent.
Quiet at the reuse boundary relies on correlated fresh odometry and native
closure; passive Gazebo snapshots corroborate selected motion/position, not an
independent continuous pre-B quiet window or hard stop limit.

The separate installed Agent `1891459` / Harness `7279cd1` pair now has bounded
real Humble/Nav2 qualification for no instruction, in-motion stop, redirect and
selected readiness/identity/global-stop failures. An actual host-model redirect
used three proposals: prepare before the business input, then after-revision and
final after it. Cancellation and confirmed A release precede the after-revision
model wait and B admission. See
[Agent qualification](https://github.com/xiao-yang25/robot-agent/blob/master/docs/TESTING.md#in-motion-revision-qualification)
and [same-run recordings](assets/demo/README.md#in-motion-business-revision).
These results do not change the old default profile or fixed old-Agent
compatibility lane; CI's synthetic native facts do not establish physics/model
results. The first recorded model scene failed at RViz startup before any model
request and remains incomplete; a subsequent unchanged scene passed, without
establishing a root-cause fix or broad reliability.
The final B, task verdict and whole-native cleanup remain pending/unassessed/unknown.

<a id="navigation-failure-settlement"></a>
## Confirmed planning failure checks

The Humble workflow runs installed `linux_failure_checks.py` with real Core and
public endpoint, using controlled ROS edges. It checks failed/no-output release,
explicit status6 selection, missing terminal or lane/quiet proof, original expiry
and task clipping, post-readiness input loss, cancel/close/EOF precedence and
native proof/unused-controller-seal rejection. These are scheduling and receipt
checks; mocked ROS edges do not prove physical closure.

```sh
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" \
  python3 integrations/ros2/nav2_session/tests/linux_failure_checks.py
```

A separate version-pinned Humble job compiles all native workers and both plugin
sets, and runs the leaf-evidence, actual tree-halt and Action-startup tests.
The existing default/revision Owner, public Python and launcher suites remain
compatibility checks. Installed real-Humble movement evidence must be recorded
separately before claiming a complete failure-recovery or Agent/model combination.
The profile is restricted as specified in [Design](DESIGN.md#navigation-failure-settlement).

Local fresh-install Humble checks passed: 62 public Python, 21 existing Owner,
11 cancellation, 11 failure, 50 launcher checks and 3 native lifecycle tests.
The installed failure profile also completed a real accepted/status6 A:
correlated native closure, quiet and six B readiness checks preceded
failed/no-output settlement and release after about 2.78 seconds, then one B
succeeded within 0.231 m of the registered site. Final B remained pending.
An actual paused B domain kept failed A pending and admitted no B. Normal A/B
passed under both the new profile and the old default. These runs used the same
isolated Humble/Nav2 1.1.20 setup; experimental occupied-map metadata and passive
map observation were fixture-only. Each scene had one physical evaluation.
No business Agent/model, restart, general unreachable classification or hard-stop
qualification follows from these checks. Remote CI is a separate delivery gate.


## Public failure-recovery scene entry

The Session launcher selects `--scene normal|occupied-a`; occupied A requires the
explicit failure profile before resources start. Current simulation builds
include the failure BT leaves and drive/interfaces; older qualified images for
other profiles may not contain them. See the [public scene commands](../integrations/ros2/nav2_session/README.md#explicit-recovery-test-scenes).

Focused checks cover fixed-map preparation without changing the source map,
actual map geometry/occupancy rejection, wrong-profile startup with no ROS/Docker
resources, Owner pre-endpoint map gating and matching public observation identity.
The existing Humble CI installs PyYAML and discovers these checks; no additional
workflow or second physical evaluator is introduced. Local macOS simulation
checks passed56 tests (five ROS-dependent skips); a real installed Linux Core /
current drive-interface run passed24 Owner,11 cancellation,11 failure and56
simulation checks without skips. Its new installed Owner files were compared
directly with source. The first Linux check used an older image with missing
`OpenPermit` interfaces and failed five finite-permission tests; the unchanged
new installation passed with current interfaces. That does not qualify the old
image for the new scene. New public scene/physical runs are qualified separately.

[Recovery recordings](assets/demo/README.md#single-failure-recovery) present two
already qualified actual-model runs from the original Harness/Agent pairing.
They are not recordings or qualification of the new public scene command.


The current public image recipe rebuilt successfully on Ubuntu22.04/Humble with
Nav2 1.1.20, including both failure leaves and the current drive interfaces.
An initial archive connection failure was retained; an explicit HTTPS Ubuntu
mirror supplied the successful build without changing package/signature checks.
The new image passed its native/observation build checks and installed Owner24 /
failure11 checks. Two new public `simulate.py session` runs used this image,
the new Linux Owner installation, and installed Agent `682ead6` with controlled
proposals: normal completed only A (0.225161 m); occupied A returned accepted /
status6, completed native closure and A release, then admitted one B
(0.233654 m). Each run used one external same-run evaluation; Gazebo observations
never entered task inputs. An event-name compatibility view reused the existing
evaluator; raw `scene_map_consumed` events were preserved. Final B remains
pending, task verdict unassessed and native cleanup unknown.

A fresh independent review found no implementation/presentation defects and
performed one separate map-mismatch startup check with the actual entry/callback
and synthetic ROS edges: no B preparation or public endpoint, with disposal and
shutdown. Its initial outstanding build/run/remote-CI evidence was subsequently
supplied; this does not turn its synthetic check into physical qualification.
The recordings above retain their original model runs and versions.

## Passive navigation collection checks

The optional [normal Session collector](../integrations/ros2/simulation/README.md#optional-passive-evaluation-collection)
has no effect on default execution. Existing Humble simulation discovery includes
its tests without adding a workflow or a physical evaluator. Tests check opt-in
identity forwarding, unsupported scope before resources, actual prepared
configuration guards, finite/failed/late/duplicate queries, missing-context gaps,
a real five-second query timeout, and interruption with a live query child.
Linux additionally exercises the actual Session cleanup function with real
collector/query and stand-in scene processes: the collector and query exit are
observed, the direct children are reaped and collection output precedes scene
teardown, while the original failure exit is preserved. A delayed-group partial
startup test rejects waiting for a child outside the shared four-second grace;
the supervisor tracks the leader before its group exists and kills both at the
original deadline.

Local checks on this increment passed 68 simulation tests on Linux/amd64 and
68 on macOS (seven Linux/ROS-specific skips). A read-only inspection also consumed
the installed Nav2 1.1.20 assets and prepared AMCL/map configuration in the current
Humble image. These are software/lifecycle checks, not a fresh physical A→B
qualification or proof of arbitrary map alignment. The offline Agent evaluator
and a new installed public consumption have their own delivery and evidence.


The 2026-10-06 public controlled tutorial consumed Harness
`4b4bc5bf28c249016cfe4d7e272fa9877766a838` and Agent
`71a804dfa9ff7f8671d464f8ed562cd2eeaec5e8` in fresh Linux installations. One new
normal A→B run explicitly enabled collection. The installed Agent evaluator ran
once, reporting `succeeded` with A/B errors 0.208517/0.188799m under its fixed
0.25m predicate; a focused independent safety-decision check accompanied it.
The retained qualified Ubuntu22.04/amd64 Humble/Nav2 1.1.20/Gazebo Classic image
was reused with the current source-mounted collector, not rebuilt at this revision.

The collector and three query children exited zero and were reaped without
forced group cleanup. The scene container exited zero without OOM and was
removed. Owner reports A settled/released and final B pending/current;
complete native cleanup remains unknown. These facts are distinct from the
physical goal verdict. The [Agent qualification](https://github.com/xiao-yang25/robot-agent/blob/master/docs/TESTING.md#installed-fixed-navigation-evaluation)
owns the task result and limits. This single controlled run made no model calls
and does not qualify other scenes/profiles or hardware. Existing recordings
retain their original provenance.

### Explicit Nav2 terminal-window software checks

[Endpoint/window tests](../bindings/python/tests/test_nav2_terminal.py) exercise
real Core/Unix-socket cancellation and status, exact replay/conflict, new-request
refusal without record growth, fixed expiry, EOF and close-response backpressure.
[CLI checks](../bindings/python/tests/test_nav2_caller_budget.py) preserve defaults
and reject invalid budgets or unsupported profiles before ROS startup.
Existing Python discovery includes these cases.

[Humble Owner checks](../integrations/ros2/nav2_session/tests/linux_terminal_checks.py)
invoke actual abort and terminal-query methods using real Core/socket requests
and synthetic ROS edges. They distinguish disabled behavior, status/close during
native cleanup progress, reply backpressure, expiry without caller close, and
cleanup progress after partial preparation before the request service exists.
The existing Humble workflow now invokes this focused script after the original
driver/revision/failure checks. This software CI does not qualify Gazebo movement,
native physical stopping, model behavior or restart recovery. The selected real
scene must be recorded separately for this explicit option.


#### Selected opt-in navigation stop scene (2026-10-10)

A fresh installation of this candidate and Agent
`b8ba3c537930b82d7018b7b8ce65c5312dbbd1e3` ran one real default-profile
Nav2/Gazebo final-decision-stop scene with `--terminal-query-seconds 8`.
The retained Ubuntu22.04/amd64 Humble/Nav2/Gazebo recording image was reused,
with network disabled and 2 CPU/4GB limits. The unchanged boundary evaluator
ran once and passed; a focused independent check of raw safety-decision facts
approved the limited interpretation.

The original B cancellation was acknowledged and its revoked/pending record
remained queryable. Close intent returned before the held late decision was
released; that completion was retained without a new submission. Agent threads
finished and their resource report was closed. Remote closure and native cleanup
remain unknown, the close reply does not claim Owner reaping, and the withdrawn
task has no physical-success or hard-stop verdict. Collector exit/reaping and
container exit 0/noOOM/removal are separate launcher observations. The status
query has no independent timestamp event; its result and installed call order
establish completion, not precise query timing. Same-scene video is retained as
local evidence. This does not qualify other profiles, models, hardware, default
migration or restart recovery; the earlier failed closing run remains failed.
