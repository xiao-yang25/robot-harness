# ROS 2 integration: probe and robot simulation

Status: **M4 in progress**. Two optional builds share the C++ Owner:
`robot_harness_nav2_observation` retains the first, observation-only behavior;
`robot_harness_nav2_settlement` adds bounded sequential A-to-B and
accepted-goal movement-time cancellation, replacement, observation-loss
withdrawal and independent consumer-isolation experiments.
The [example README](../integrations/ros2/nav2_observation/README.md) defines
build commands and the trusted fresh/exclusive deployment premise.
The optional profile is experimental and requires the research native fixture;
public simulator packaging remains pending. The bounded observation-loss and
unresponsive-navigator behavior is defined below; Owner-independent protection
and drive-channel loss remain outside the validated scope.
Core and its public API remain ROS-independent.

## Sequential settlement boundary

Execution profile `nav2-fixed-context-sequence-v1` has the normal scenario with two fixed goals:
(-2.0, -0.5) → (0.7, -0.5) → (-1.5, -0.5). The original
`nav2-normal-observation-only-v1` profile remains exclusive to the observation binary.

One Owner keeps the Gate, operation authority, result sink, node and single-threaded
executor alive. A callback captures its operation identity; a callback from A
cannot mutate B's native state or result. Callback exceptions are retained by
the Owner and rethrown outside ROS promise callbacks; a missing/invalid result
observation cannot crash through a fulfilled promise or authorize settlement. A's native UUID is correlated with the
fixed tree's planner/controller UUIDs and sealed worker acknowledgements. The
Owner then closes the BT worker, seals generation 1 at the drive, observes the
applying update and collects a fresh finite quiet window.

With the drive still sealed, the launcher creates B's distinct native namespace.
The launcher response establishes only process creation. The Owner probes active
planner/controller/smoother and navigator, the controller's own TF buffer, the
planner's own current costmap/robot bounds, B's Action endpoint and immutable
command producer identities. It checks fresh quiet motion again. Only then does
it submit A's `SettlementEvidence` under `fresh-context-generation-drive-v1`,
refresh the execution profile, and request B admission from Core. B's generation
opens after admission; a separate Core dispatch claim immediately precedes the
actual Action send. Retained generation-1 drive acknowledgements cannot close B.

B navigates back toward (-1.5, -0.5), then closes its native work/drive and records
fresh quiet motion. **B's receipt stays pending and a third admission is denied**:
this two-task example has not prepared a C context. A reusable indefinitely
cycling Host would need an explicit next-context lifecycle and resource policy.
Creating C solely to label B settled would hide this boundary and add unnecessary
processes. Neither arrival, a pause, a fixed wait nor container teardown supplies
settlement evidence.

Missing worker/applied-drive acknowledgement, missing fresh odometry or an
unready B context leaves A unsettled and prevents B admission/open/send.
Readiness is a sampled observation in a trusted, exclusive fixture, not an atomic
lease over remote processes. General runtime context loss, Owner death/restart, stop latency bounds and real
hardware safety are outside this sequential contract. The observation-loss
increment below adds one specific failure boundary. The fixture's
immutable producer-to-generation binding and drive rejection of old commands
remain essential assumptions, supported by separate native isolation experiments.
The product Owner reads native facts directly; no Python summary grants authority.

## Movement-time cancellation boundary

The optional settlement executable also has a bounded `cancel-moving` scenario.
It cancels the one accepted goal after fresh odometry reports at least 0.5 m of
movement from spawn and speed at least 0.05 m/s, with recent clock and navigation
feedback. This is a reproducible test trigger, not an obstacle detector or a
physical stopping policy. Pending goal acceptance and cancellation before motion
remain outside this scenario.

The owning thread revokes Core authority first and sends exactly one targeted
`async_cancel_goal` for the retained ROS UUID. Repeated Core cancellation must
not issue another native stop. A successful cancel-service response only counts
as acknowledged if its single `goals_canceling` entry matches that UUID. It never
releases the domain. The native terminal result is observed independently;
Cancelled/Failed produce no output, and any success arriving after revocation
cannot reach the local result sink. The finite scenario requires an actual
Cancelled result to pass rather than treating a cancellation race as a pass.

Keep this Owner alive after the terminal result. Match planner/FollowPath work
completion, close the navigation BT endpoint, confirm the generation-bound drive
applied its stop and observe the existing fresh quiet window. The native BT leaf
must publish its actual accepted FollowPath UUID while running; a success-only
association cannot support cancellation. A controller worker may remain alive
after the top-level terminal result, so no timeout or terminal flag replaces its
completion acknowledgement.

`cancel-moving-withhold-planner-ack`, `cancel-moving-withhold-drive-ack` and
`cancel-moving-withhold-odometry` remove the corresponding Owner observation.
They must leave the missing fact unresolved and refuse a second admission.
The first two cannot confirm complete native closure; the third cannot confirm
fresh quiet motion. These are observation-loss injections, not proof of
independent protection after Owner/network failure.

No replacement context is prepared by this cancellation-only scenario. Even
when all closure and motion facts are present, the receipt stays pending and
revoked, with a real second admission refused. The replacement scenario below
uses the sequential readiness/settlement/reopen chain to continue with B. None
of these scenarios promises a hard stop deadline, Owner-independent supervision,
restart recovery or physical safety.

## Movement-time replacement boundary

`replace-moving` reuses the cancellation trigger and exact-goal protocol above,
then the same Owner completes A closure and fresh quiet observation. It prepares
B while the drive stays sealed, verifies B readiness, settles A through Core,
admits B, opens generation 2 and claims dispatch immediately before sending B.
The scenario keeps A at (0.7, -0.5), but sets B to (0.0, -0.5): B must navigate
from the cancellation position. The normal sequence's return point (-1.5, -0.5)
would lie too close to that position to demonstrate replacement movement.
These are fixed test scenarios, not an arbitrary-goal or preemption API.

Only the first native send can trigger automatic cancellation. A must retain
Cancelled/no-output history after settlement; B has a distinct operation and
native goal. No-output and settlement evidence share a source and therefore
use one increasing source sequence; result-sink evidence has its own sequence.
B succeeds through the normal result sink, closes native work and
stays pending without a prepared C context. A callbacks cannot mutate B state.

The five suffixes `withhold-planner-ack`, `withhold-drive-ack`,
`withhold-odometry`, `missing-map` and `missing-controller` (each prefixed by
`replace-moving-`) must prevent B admission, generation opening and native send.
A terminal result or the launcher's process-creation response cannot fill a
missing observation. Expected fault handling must still surface callback errors.

Simulation validation injects nonzero commands into A's retained **raw** channel
only after B starts moving. Those commands pass through the old smoother and
immutable generation-1 bridge; the drive must reject them while generation 2 is
open, and B must still reach its new destination. The injector grants no authority
and the external checker does not decide settlement. The observation-loss increment below covers stale clock/odometry while awaiting
a result. Other runtime loss and Owner-independent stopping remain separate
boundaries.

## Runtime observation loss boundary

The optional executable checks clock/odometry progress while waiting for each
native goal and again before a terminal result can reach the result sink. A
private two-second budget uses the Owner's monotonic wall time, with a fresh
grace period at each actual send. New clock stamps and valid odometry stamps
must advance; identical or backward stamps cannot renew progress. An expired
interval is latched before a restoring callback can renew its timestamp, so
callback order cannot hide the gap. Odometry also
needs the expected frame, finite pose/speed, normalized orientation and a stamp
within 250 ms of the observed clock. This budget is a fixture policy and a sampled
check, not a scheduling or physical stopping guarantee. The default observation
binary retains its previous behavior.

On stale observations, the same Owner calls Core `withdraw_binding`. This records
binding withdrawal rather than a user cancellation, revokes result delivery and
makes admission require recovery. Any returned native-stop authority is used
once for the accepted UUID; if acceptance is still pending, the Owner retains
the intent and sends when the handle arrives. Result, cancel response, worker
closure, applied-drive closure and quiet motion remain distinct facts. A result
arriving after withdrawal cannot be delivered. Waiting for a terminal result
ends at a bounded reconciliation interval after loss; a missing result remains
pending, rather than being invented as Cancelled or successful.

At withdrawal the Owner also submits one generation-bound drive seal, without
waiting for cancel acknowledgement, worker completion or BT closure. Native
closure reconciliation continues independently. Quiet observation requires the
drive seal response and its actual applying-update acknowledgement.
If clock/odometry stays unavailable, it cannot confirm fresh quiet even when
native work and the drive have acknowledged closure. Fresh observations can
later support a quiet window, but they never undo binding withdrawal or authorize
B. The receipt stays pending; recovery/rebinding is not implemented here.

Four isolated fixtures remap only the Owner's clock/odometry inputs through a
relay. The native Nav2 stack still receives real simulator observations. After
actual movement of at least 0.5 m at 0.05 m/s, the relay drops odometry
(`runtime-odometry-loss`), repeats the last odometry stamp
(`runtime-odometry-replay`), drops odometry for six wall seconds then restores it
(`runtime-odometry-resume`), or drops clock (`runtime-clock-loss`). The Owner
receives no fault-trigger signal. Each scenario must withdraw the binding,
request the exact native stop and refuse another admission. Only the restored
case can establish fresh quiet motion; it still cannot settle or admit B.

These four observation-channel scenarios retain a reachable native stop path.
The separate boundary below tests one unresponsive native endpoint. All require
a live, scheduled Owner; this is not continuous supervision during every
blocking closure/readiness call. Owner death, full transport partition,
pending-acceptance and late-success fault injection remain unverified. A missing
terminal/ACK is not proof of stopping.

## Independent consumer isolation

A withdrawn Owner can close the current drive scope/generation independently of
native work completion. `NativeClosure::request_drive_close()` submits once and
returns without spinning, including when called from a result callback. The same
request timestamp and applying-update observation are reused during later native
closure reconciliation. A service response means the seal request was accepted;
only the matching actual simulator update establishes wheel-target application.
The existing driver rejects subsequent commands under the same lock as its seal
and wheel-target update. This preserves isolation while a producer keeps running.

Drive isolation, fresh quiet, native outcome, worker/BT closure and Core settlement
remain separate. A worker/BT timeout does not erase a valid drive observation;
conversely a drive ACK does not establish worker closure. Full `closed()` still
requires workers, BT and drive. Without the drive ACK, the Owner cannot claim
isolation or start the post-application quiet window, even if it sees low speeds.
Neither drive isolation nor restored observations grants settlement or B admission.

The research cases `runtime-stop-unreachable` and
`runtime-stop-unreachable-withhold-drive-ack` stop the real navigator process
with SIGSTOP during motion, leaving its controller, smoother and driver running.
This makes the cancel/result executor unresponsive; it is not a full DDS network
partition. The observation relay drops odometry and restores it after six wall
seconds. The Owner never reads process state or injection events. The external
check verifies the navigator remains stopped, cancel/terminal responses remain
absent, real nonzero producer commands continue after drive application, and the
consumer rejects them. The second case withholds the applying-update ACK only
from the Owner: physical application may occur, but its isolation/quiet report
must remain unconfirmed. Both retain withdrawn binding and pending settlement.

This path requires the Owner, independent drive control channel, driver executor
and simulator update to remain available. It does not cover Owner death or loss
of the drive channel itself, provide a hardware emergency stop, or bound stopping
latency. A device-local watchdog/lease is a separate design if those failures
enter scope. No Core API or driver wire protocol changes are introduced here.

## User-facing M4 outcome

M4 must reach a **robot navigation demonstration in simulation**, with reproducible
video and execution observations. A sum carried over ROS is an engineering probe,
not the milestone's product demonstration. This clarification leaves the existing
Core semantics intact and makes the next application outcome explicit.

The planned scenario is a simulated mobile robot navigating to A, receiving a
revised goal B while moving, and responding to cancellation. Record the robot,
path/pose feedback and Harness observations from the same run. Start with normal
navigation, then add goal revision and cancellation; loss/unknown cases are a
separate regression, not a requirement to make the first useful video theatrical.

| Step | Deliverable and stopping condition |
|---|---|
| Small ROS probe | Resolve pending-send cancellation, identity correlation and terminal-versus-quiescence uncertainty. Retain a bounded sum fixture only where it forces a useful counterexample; do not finish a generic sum SDK or exhaustive second compute adapter before trying simulation. If the native navigation baseline answers a question directly, reuse that evidence. |
| Native simulation baseline | Launch a robot, known world/map and Nav2; complete one target without Harness. Verify pose initialization, action feedback, simulation clock and a usable recording view. This may proceed alongside the probe and is not advertised as Harness capability. |
| Harness navigation integration | Wrap `nav2_msgs/action/NavigateToPose` with the same Core. Use a navigation-specific caller and native capability/settlement observations; do not impose compute request/result types or the probe's inspection service on Nav2. |
| Reproducible simulation demonstration | Record normal arrival, A-to-B revision and cancellation with real Harness/native observations. Supply commands, revision, world/map/start pose and limitations. Only then put the video on the project homepage as a Harness demonstration. |

The selected experiment environment is **Ubuntu 22.04 / ROS 2 Humble**, as
confirmed by the maintainer. Keep the probe and simulation on this series; do not
silently migrate to Jazzy or mix newer distribution instructions. Use Nav2's
[TurtleBot3 simulation launch](https://github.com/ros-navigation/navigation2/blob/humble/nav2_bringup/launch/tb3_simulation_launch.py).
It supplies a robot, world/map and navigation bringup. `headless:=True` disables
`gzclient` only; `use_rviz:=False` is also needed for a no-window probe.
[ROBOTIS simulation instructions](https://emanual.robotis.com/docs/en/platform/turtlebot3/simulation/)
provide a second official reference; use the Humble section/branch rather than
mixing distribution instructions. These sources establish a candidate, not local
ARM64 package/rendering success.

This legacy launch uses Gazebo Classic, whose support ended in January 2025
([official notice](https://classic.gazebosim.org/)). Reusing it for a bounded local
comparison is not adopting it as the public long-term stack. Before environment
installation, check package availability and recording feasibility. The current
choice retains this narrow Humble baseline for compatibility; a future simulator
change requires a separate decision based on observed limitations. Do not spend
an open-ended effort repairing an obsolete graphical stack. Nav2's current
[package table](https://github.com/ros-navigation/navigation2) does not provide
`nav2_loopback_sim` for Humble, so a newer distribution's loopback example is not
a drop-in substitute.

A local Ubuntu 22.04 / Humble **amd64** Docker experiment on Apple Silicon has
now completed one native Nav2 goal with Gazebo-observed displacement, action
feedback, a successful terminal status and an actual RViz recording. The
configured ARM64 package sources lacked the Classic/TurtleBot3 simulation set.
This established the native baseline. The first Harness normal observation now
reuses this scene; neither run establishes real-time performance, and missed
control-rate warnings were observed. A Linux graphical environment remains an
alternative if later workloads exceed the emulated container's capacity.

For the video, distinguish native cancellation response, action terminal status,
observed motion and the adapter's declared settlement. Humble's
[NavigateToPose interface](https://github.com/ros-navigation/navigation2/blob/humble/nav2_msgs/action/NavigateToPose.action)
has pose/distance feedback and an empty result payload; arrival interpretation
cannot reuse the compute result checker. A zero velocity command or a brief still
frame is insufficient for a general stopping guarantee. Use fresh simulator
observations over a declared window plus the applicable controller/work-closure
facts; the bounded sequential profile above applies only with its complete native
fixture. Missing evidence keeps it unresolved.
Compare with direct Nav2 usage at the same declared guarantee: navigation itself
is Nav2's capability; the demo must show the additional Harness admission/result/
settlement behavior rather than attribute ordinary motion to Harness.

Record actual simulation, not an authored robot animation. Include an uncut
reference run alongside a short explanatory edit; disclose speed changes/cuts and
label all footage as simulation. A rosbag replay, if used, is labeled replay.
Physical hardware follows only after the device's own stop/loss protections are
validated. The sequential profile extends normal observation with the boundary above.
Movement-time cancellation and replacement use the bounded scenarios above;
bounded observation loss and an unresponsive navigator with reachable drive
control are covered above. Broader endpoint loss and recovery remain future work. Historical probe
approval does not cover new behaviors.

## Delivery increments

1. Normal navigation observation, startup/pre-dispatch denial, finite motion
   observation and the independent Humble build/guard tests. Settlement stays
   pending and a second operation is refused.
2. Same-Owner work/drive closure, fresh motion, a usable task-bound native
   context, Core settlement and admitted sequential A to B. Native research
   evidence alone must not grant product authority.
3. Motion-time cancellation, replacement and loss/unknown regressions, followed
   by the reproducible user-facing simulation demonstration.

Each increment includes its design, usage, test and CI changes. Homepage and
contributor navigation can ship independently. Research fixtures and private
execution records stay outside the product repository. The default observation build introduces no native-closure dependency. The
second executable opts into the fixture interfaces and JSON evidence matcher;
neither introduces a stable public settlement API.
