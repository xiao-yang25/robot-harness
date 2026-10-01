# Simulation recordings

[Watch on the project website](https://xiao-yang25.github.io/robot-harness/#demo).
The gallery contains actual ALOHA / MuJoCo and TurtleBot3 / Gazebo Classic
recordings. All are silent, scripted simulation scenarios. Video shows motion;
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
reproduction remains unqualified. This single-seed demonstration is not a
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
