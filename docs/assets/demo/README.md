# Simulation recordings

## In-motion business revision

Two new recordings use installed Agent
`189145996d2c7811ecd5d70641bc01c3e575a3d5` and its immutable Harness
`7279cd12ae48d01384e081757c54c38b7858557b` dependency, explicitly selecting
`scoped-two-context-nav2-revision-v1`. The retained Ubuntu22.04/Humble/Nav2/Gazebo
amd64 image runs without network, at 2CPU/4GiB. No physical robot is used.

Both business instructions are controlled and delivered once after the single
motion window opens: stop after four seconds, redirect immediately. Each scene
has its own one-time physical evaluation; each clip contains only its own run.

| Clip | Proposal source | Recorded outcome |
|---|---|---|
| [Stop during motion](revision-stop.mp4) | Controlled proposal | Exact A cancelled/no output, native closure/quiet/six B readiness replies, then settled/released in about5.050s. No B or completed site; `stopped_by_instruction` |
| [Redirect during motion](revision-redirect.mp4) | Three actual host proposals; explicitly requested `gpt-6-sol` / high via Codex0.159.0 | A cancelled/no output and released in about4.994s before after-revision proposal/fresh B admission. Only B completed, independent error0.236m; final B current/pending |

The model sees the bound redirect instruction and public map measurements,
without a robot interface or independent Gazebo truth. Cancellation precedes
the model wait; the model cannot grant reuse. Native/child/BT/outlet closure,
fresh odometry quiet and successor readiness establish the reuse boundary.
Passive Gazebo snapshots corroborate selected motion and B position, not an
independent continuous quiet window. Prepared B resources can exist even when
no B request is sent. Task verdicts stay unassessed and native/remote cleanup
unknown. Exact scene/export containers and owned model/relay children were
removed/reaped; host groups exited zero without forced cleanup.

Startup is trimmed, the RViz view cropped, captions added and the last frame
held two seconds. The stop clip ends after task return; its later scene teardown
is excluded. The redirect clip retains the model waits and task completion.
There are no internal cuts or speed changes. Event captions
use same-run monotonic time with screen-capture startup uncertainty; these
videos and individual closure times are not timing guarantees. Old A paths
may remain visible after cancellation and do not indicate live authority.

Separate no-instruction/foreign-identity/global-stop/actual B-pause scenes cover
their selected outcomes. A first model scene failed in RViz startup before any
proposal and remains incomplete; later unchanged runs passed without resolving
that graphics failure's root cause. A separately evaluated four-second-delay
model run completed the task but its RViz view stayed empty; that recording was
rejected for presentation and is not spliced into these clips. Its graphics
issue also remains unresolved. No arbitrary-route, Owner recovery, human
intent recognition, hardware or general model-service qualification is implied.
See the [revision tutorial](https://github.com/xiao-yang25/robot-agent/blob/master/examples/navigation/README.md#run-the-in-motion-revision-task)
and [qualification scope](https://github.com/xiao-yang25/robot-agent/blob/master/docs/TESTING.md#in-motion-revision-qualification).

## Checkpoint navigation

Two new same-run recordings exercise Robot Agent's explicit checkpoint task with
the existing fixed Harness `13bc75e903f6005da3dd5d969f8242ce211d182f` dependency.
Recorded application code is `2ac951b0cafedf5e4871e689dbd5d6ec4d7e749c`; new Linux
Core and Agent installations use an existing qualified Ubuntu22.04/Humble amd64
image. The scene has no network, 2 CPU and 4 GiB. No physical robot is used.

| Clip | Business and proposal sources | Recorded outcome |
|---|---|---|
| [Finish at A](checkpoint-finish.mp4), about 28 seconds | Controlled business instruction, delivered once after a four-second wait; controlled proposal | A settled/released, then this new task completes at A; no B request. Separate same-run Gazebo evaluation measured A error 0.189m |
| [Continue to B](checkpoint-continue.mp4), about 68 seconds | Controlled business instruction, delivered once after a four-second wait; three actual host `gpt-6-sol` / high proposals via Codex0.159.0 | A releases before a single B admission with fresh feedback; final B current/pending. Separate same-run evaluation measured A/B errors 0.188/0.175m |

The model receives the bound business instruction and declared map measurements;
it does not choose business intent or receive physical-evaluator truth or a robot
interface. Actual host model and relay children were reaped, both host groups
exited zero without forced cleanup, and the exact scene containers were removed.
Task verdicts remain unassessed; native and remote model cleanup stay unknown.
Prepared B resources may exist even when no B request is sent.

Startup is trimmed, the RViz view cropped, captions added and the last frame held
two seconds. Motion plays at original speed; caption alignment uses same-run
monotonic events and retains screen-capture startup uncertainty. These clips are
not timing instruments, in-motion replanning or hard stop evidence. Selected
foreign and late instruction rejection runs are qualified separately; they are
not silently inserted into these normal recordings. See the
[checkpoint tutorial](https://github.com/xiao-yang25/robot-agent/blob/master/examples/navigation/README.md#run-the-checkpoint-task).

[Watch on the project website](https://xiao-yang25.github.io/robot-harness/#demo).
The gallery contains actual ALOHA / MuJoCo and TurtleBot3 / Gazebo Classic
recordings. Each silent clip identifies model-guided or deterministic decisions. Video shows motion;
the corresponding observations and receipts establish execution and settlement.
It does not establish a physical stopping guarantee.

## Continuous ALOHA transfer and hold

[Complete recording](aloha-handoff.mp4) · [Same-run summary](aloha-handoff-run.json) ·
[Runtime setup and caller](../../../integrations/mujoco/README.md#continuous-handoff)

Recorded on 2026-10-01, using the explicit `mujoco-stepped-handoff-v1` profile,
seed0 and a deterministic caller. The external ACT worker supplies 400 transfer
targets. After delivered settlement, the caller obtains fresh RGB/joints and
requests a separate50-step hold of the last actually executed target. There is
no reset between operations and no policy prediction during hold.

| Same-run observation | Value |
|---|---|
| Episode / observation sequence | Epoch0 throughout; 0 → 400 → 450 |
| Core operations | IDs1/2; 400/50 returned steps; both outputs accepted and settled |
| Core domain verdict | Unassessed; execution settlement is not task success |
| Separate task evaluation | `aloha-left-handoff-hold-v1`: succeeded in the bounded one-second hold |
| Recording | 451 frames, 640×480, 50fps, 9.02s; silent, uncut original |

The video is copied from the final qualified `m5d-handoff-session-02` episode.
It follows simulation time (0.02s per step), not elapsed runtime: inference and
caller waits have no frames. There is no crop, resize, speed change, internal cut
or authored motion. The poster is frame350 from this same recording. All450
recorded steps matched the native continuation baseline, and all451 frames decoded.
The linked summary selects existing report/evaluator fields and omits local paths;
it is not the full raw trace or a second task evaluation.

Qualification used a development tree based on `53e65c4` with the continuous
profile patch delivered with these assets. This is locally qualified on macOS
arm64/MPS; see the [backend version and dependency scope](../../../integrations/mujoco/README.md).
Reproduction requires the external worker and migrated checkpoint described
there; they are not installed or shipped with the Core package. Linux ACT/render
qualification is recorded separately; this Mac video does not establish it. This single-seed demonstration is not a
reliability estimate, an autonomous business Agent, self-improvement, or a
physical grasp/stop guarantee. It does not demonstrate the separate cancellation
checks. Raw experiment records and unsuccessful runs remain retained separately.

## Navigation recordings

The clips below show TurtleBot3 / Gazebo Classic through RViz on Ubuntu22.04 /
ROS2 Humble. Their checker reads Gazebo poses and native/Owner observations.

## Moving handoff

[Readable excerpt](replace.mp4) · [Full uncut recording](replace-full.mp4) ·
[Owner events](replace-owner.jsonl) · [Scenario verification](replace-verification.json)

A starts toward (0.7, -0.5). After about 0.5 m of observed movement, the caller
cancels it. The same Owner observes native work/drive closure and fresh stillness,
prepares and checks a new context, settles A and admits B toward (0, -0.5).
Green is A's path; blue is B's distinct task-scoped path. RViz may retain A's old
path after cancellation; a visible path is not active execution authority.

In this run A moved 0.555 m and B moved 1.179 m. Fifty old-A commands were rejected
at the drive while B was moving. That isolation claim comes from the native
observations and checker, not from what the camera can show.

| Final observation | Value |
|---|---|
| A native result / output | Cancelled / not delivered |
| A settlement | Settled after closure and replacement readiness |
| B | Admitted, native result accepted, native work closed |
| B settlement / third admission | Pending / refused; no C context was prepared |

## Cancellation without a successor

[Readable excerpt](cancel.mp4) · [Full uncut recording](cancel-full.mp4) ·
[Owner events](cancel-owner.jsonl) · [Scenario verification](cancel-verification.json)

A moved 0.557 m. The Owner sent one exact-goal cancellation and independently
observed native termination, work/drive closure and a fresh quiet-motion window.
No output was delivered and no B goal was sent. Ten injected late commands were
rejected. The final receipt remains **revoked and unsettled**, and a second
admission is refused. A successful scenario check means these expected facts
were observed; it does not label the cancelled navigation task successful.

## Recording provenance and edits

Captured on 2026-09-27 using the packaged `cancel-moving` and `replace-moving`
scenarios. Runtime sources match commit
[`4911d79`](https://github.com/xiao-yang25/robot-harness/tree/4911d799f61303fb3bb5aba47b0413b4978b42d6);
this delivery adds capture/finalization only. The local visual dependency image
was reused with the changed capture script. Runtime source comparison found no
code differences; this was not a fresh dependency rebuild. This does not replace
hosted clean-source simulation evidence in the testing guide.

| Recording | Uncut duration | Captured frames | Public excerpt |
|---|---|---|---|
| Cancellation | 34.4 s | 164 | From 22 s to the end |
| Moving handoff | 56.6 s | 255 | From 22 s to the end |

Full recordings retain the original 1600×900 capture and timestamps, remuxed to
MP4 without re-encoding. Capture requests 5 fps; frame drops under load mean
frame count is not a clock. Excerpts remove only the first 22 seconds of startup,
crop the 800×450 region at (400,300), and resize it to 1280×720. There are no
internal cuts, speed changes, invented movement or simulated replacement frames.
Posters come from the corresponding excerpt (8 s cancellation, 25 s handoff).
Use logs for event ordering; no frame-exact event/video synchronization is claimed.
The native result, observed quiet and settlement remain distinct. Neither timing
from these recordings nor the emulated Linux environment establishes a stop bound.

## Reproduce

From a checkout containing the recording support:

```sh
python3 integrations/ros2/simulation/simulate.py build --visual
python3 integrations/ros2/simulation/simulate.py run cancel-moving --visual --output simulation-results/record-cancel-01
python3 integrations/ros2/simulation/simulate.py run replace-moving --visual --output simulation-results/record-replace-01
```

Use a new output directory each time. `recording.mp4` is the uncut capture;
`run.json` must report a passed run and completed container cleanup, and
`verification.json` must pass the scenario checks. Failed or interrupted runs
may still produce video. See the [simulation tutorial](../../../integrations/ros2/simulation/README.md#watch-a-live-run)
for requirements and failure handling. A model or real robot is not needed.
These runs do not validate Owner restart, full network partition, general route
replanning or physical-robot safety.

## Earlier normal navigation

[Download the original MP4](navigation.mp4).

This is an uncut 23.8-second, 1600×900 recording of a TurtleBot3 simulation on
Ubuntu 22.04 / ROS 2 Humble, captured on 2026-09-21. Gazebo simulates the robot;
RViz displays the robot and map. The recording is a stream-copy remux of the
original capture, without speed changes or authored movement. The poster is an
actual frame from the same recording.

The experimental Harness caller admitted one navigation goal, submitted it to
Nav2 and accepted its result. The run observed approximately 2.485 m of displacement.
It was captured from the normal-observation development tree based on `b0929ab`,
before the final native-result log event and the later sequential-settlement work.

This clip demonstrates normal navigation. It does not demonstrate A→B handoff,
movement-time cancellation, durable recovery or physical-robot safety. The
observation-only path kept settlement pending and denied a second admission.
Current behavior and validation are documented in
[Testing](../../TESTING.md#current-validation-baseline) and
[ROS integration](../../ROS2_INTEGRATION.md). The current
[simulation tutorial](../../../integrations/ros2/simulation/README.md)
provides a source build, bounded scenarios and an optional live viewer.
This historical recording does not demonstrate those later capabilities;
the cancellation and handoff recordings above cover their own later scope.


## Adding a major feature recording

Each major user-visible feature should include an actual-run demonstration. The
project homepage embeds a video player; setup and full-evidence links supplement
it. Record the implementing application and runtime together, identify model or
deterministic decisions, preserve pending/unknown states and label any trimming,
speed changes or held frames. A demonstration is presentation evidence for its
stated run, not a replacement for tests, physical evaluation or hardware safety.
Small fixes do not require a new recording unless the displayed behavior changes.


## Model-guided navigation (local candidate)

[Watch in the homepage player](https://xiao-yang25.github.io/robot-harness/#demo) ·
[Complete task excerpt](model-navigation.mp4) · [Same-run summary](model-navigation-run.json) ·
[Owner setup](../../../integrations/ros2/nav2_session/README.md) ·
[Agent application](https://github.com/xiao-yang25/robot-agent/tree/9516dafb13d5df8e64c0491d9c8f01cce54a4ff8)

Recorded on 2026-10-03 from a local Harness candidate based on `a145364` and
Agent candidate based on `5e8b63b`. The corresponding runtime and application are
now delivered in Harness `6f32578` and Agent `9516daf`; this recording retains its
original local candidate scope. In isolated Ubuntu22.04/Humble amd64 Gazebo simulation, the installed
business Agent consumed public NavigationSession feedback. Host Codex0.159.0
requested `gpt-6-sol` / high and supplied three actual text proposals: visit A,
visit B, observed complete. Model input was task context and public navigation
pose/time/health; external Gazebo truth and evaluation did not drive decisions.
The model ran on the host; the 2 CPU/4 GiB, network-disabled simulation received
no credentials. Each proposal remained bounded at 30 seconds; the owner was
explicitly configured for 45 seconds of caller idle time.

The scene ran 107.642 seconds. A/B same-run physical distances were
0.192253/0.166768m under the existing separate evaluator. A settled/released
before B admission; B output was accepted with settlement pending. Agent
assessment was observed complete, while task verdict stayed unassessed and
native cleanup unknown. Local connection close or container removal does not
invent a native reuse certificate. This single normal run does not establish
model reliability, model motion cancellation, owner recovery or hardware safety.

The H264 video is 1280×800, 10fps and 70.9 seconds, silent. Startup is trimmed;
the RViz scene is cropped/resized, same-run event captions are added, and the
final frame is held for two seconds. There are no internal cuts or speed changes;
model waits remain visible. Caption timing uses approximate monotonic event
alignment with recorder startup latency retained, not a stopping-time instrument.
The poster is a frame four seconds after A admission from this same final video.
The summary selects retained facts without private prompts or operator paths;
it is not a second task evaluation.

A prior recording failed after B: the owner's default 10-second final-close
wait expired while a roughly10.03-second model proposal was returning. Its
needs-help/unknown report and clean recorder/container cleanup remain retained
separately. An explicit bounded caller wait resolves that budget incompatibility;
no native deadline, freshness TTL, physical predicate or model budget was widened.
Setup uses the paired commits above; the Agent manifest pins Harness `6f32578`.
Their installed combination checks preserve ALOHA and navigation consumption,
without repeating this physical/model qualification. See
[Testing](../../TESTING.md#m6b-navigation-business-agent).

### Presentation layout

The project README features the model-guided navigation recording through a
GitHub video attachment, so readers can play it inline. It is the current local
candidate with the most complete observation, decision and execution loop.
The website keeps a featured player, three smaller players for ACT transfer,
moving handoff and cancellation, and a separate two-player row for business stop
and final proposal expiry. These recordings remain separate: their callers,
time scales and qualification scopes differ. Original MP4 files and evidence
remain available here; attachment URLs are presentation copies, not evidence IDs.

## Business CLI interruption

[Homepage player](https://xiao-yang25.github.io/robot-harness/#agent-stop-demo) ·
[Recording](agent-navigation-stop.mp4) · [Same-run summary](agent-navigation-stop-run.json)

Recorded on 2026-10-03 with installed Harness `6f32578` and Agent runtime
`9516daf`. Controlled executable proposals visit A and B; after A releases and
B is accepted and moves over 0.5 m, the public Agent CLI receives one SIGINT.
The Agent explicitly cancels exactly B, without another proposal or admission.
Core revocation, native cancel scheduling, a scoped drive seal/wheel-zero ACK,
old-command rejection and a separate quiet/physical window were checked once
in the same-run evaluation. The fixed physical window had 36 fresh samples over
3.502 simulated seconds, with endpoint drift about 0.000083 m and 0.00247 rad.

The report stays cancelled/task unassessed/native cleanup unknown/settlement
pending. Explicit cancel response is retained; subsequent status/close connection
errors remain visible in the task report. Proposal children and CLI were reaped;
the scene container exited 0 without OOM and was removed. An earlier recording
failed native preparation before any admission; that failure is retained separately.

The 1280×800 H264 recording is 30.9 seconds, original speed, with startup and
view trimmed, approximate event captions and a two-second final hold. The poster
is from B motion in the same export. It is not a stopping-time instrument.

## Final proposal expiry

[Homepage player](https://xiao-yang25.github.io/robot-harness/#late-proposal-demo) ·
[Recording](navigation-late-proposal.mp4) · [Same-run summary](navigation-late-proposal-run.json)

Recorded on 2026-10-03 with the owner fix now delivered in Harness `ea7b1eb`
and unchanged Agent runtime `9516daf`. Controlled executable proposals visit A
and B. The final child exceeds the original 30-second deadline, then emits a
valid, matching answer while terminating and exits 0. The expired answer is not
accepted; the Agent requests help and cancels exactly retained B. Native
succeeded/output accepted remains alongside authority revoked/settlement pending;
task verdict remains unassessed and native cleanup unknown. Status/close errors
are retained. The Agent exits 1; owner/container exit 0, with children reaped
and container removed without OOM.

One same-run evaluator confirmed A/B physical distances 0.198780/0.159009 m
and A release before B admission. The first run exposed final cancellation being
misclassified as an authority error; it is retained separately, without
reevaluation. The focused regression failed before the fix and all 12 owner
checks passed afterwards; completed normal close retains its prior wait path.

The 1280×800 H264 recording is 71.7 seconds at original speed, including the
full proposal wait. Startup/view trimming, approximate event captions and a
two-second final hold are declared; there are no internal cuts or speed changes.
The poster is from B motion in this export.

### Delivery and limits

Harness `ea7b1eb1a075cd91ef590475f2496a48d19ca52a` and Agent
`ffad065ae81ca9e9b17cba558d471078d657a9b2` deliver the matching fixed runtime
and nine installed combination cases. Agent runtime source is unchanged; pairing
and presentation do not repeat either recording's physical qualification.
These clips use controlled proposals and a live owner, not real model failures.
They do not establish complete native cleanup, Owner loss, restart recovery,
hard stop deadlines or hardware safety. Both final exports decoded completely
and their B/wait/final frames were inspected. Summary files select existing facts
without operator paths or prompts; they are presentation metadata, not another
evaluation. See [Testing](../../TESTING.md#final-proposal-cancellation-candidate)
and the [fixed Agent checks](https://github.com/xiao-yang25/robot-agent/blob/ffad065ae81ca9e9b17cba558d471078d657a9b2/docs/TESTING.md#updated-fixed-pairing).

## Finite Owner motion permission

[Recording](owner-permission.mp4) · [Selected same-run facts](owner-permission-run.json) ·
[Explicit profile and reproduction](../../../integrations/ros2/nav2_session/README.md#explicit-finite-motion-profile)

The installed product candidate uses `scoped-two-context-nav2-owner-permit-v1`,
based on `f43f4c3` with the corrected Owner patch delivered alongside these assets. The final
installation passed21 focused Owner/Core checks and a new normal A→B run.
In one isolated Ubuntu22.04/Humble/Gazebo run, A settles/releases, B starts
moving, and the actual Owner is killed with SIGKILL. Native command production
continues; the drive independently expires the issue-stamped permission and
actually writes every wheel target to zero. Late renewal and a fresh generation
are refused. The separate evaluator confirms fresh external quiet and the fixed
Gazebo snapshot window, declared to start 1.5 seconds after fault injection.

This run observed 1.013 seconds from the fault to zero application and 29.3 ms
from the last permission deadline to application. These are observations, **not
hard time bounds**. Native cleanup and Core settlement remain unknown, task
success unassessed; there is no automatic restart or hardware qualification.
A separate installed normal A→B and actual SIGSTOP check also passed their
declared predicates; this recording demonstrates the SIGKILL case only.

The 1280×800 recording is about31.1 seconds at original speed. Startup/view are
trimmed, approximate monotonic event captions are added, and the last frame is
held two seconds. Display refresh is5fps; the recorder samples10fps. Full decoding
and final-frame inspection passed. Captions are not a timing instrument. The
poster is the final frame from this export. The JSON selects the existing
unique evaluation; it does not rerun physical adjudication or contain local paths.
