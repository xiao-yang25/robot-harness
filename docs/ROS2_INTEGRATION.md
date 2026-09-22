# ROS 2 integration: probe and robot simulation

Status: **M4 in progress; experimental normal Nav2 observation implemented**.
The [optional example](../integrations/ros2/nav2_observation/README.md) documents
its build, trusted deployment premise and exact limits. Local Humble/Gazebo
validation covers one Core-admitted goal with actual displacement, correlated
feedback/result and protected output. No settlement evidence is supplied; the
second admission is blocked. Startup denial and pre-dispatch cancellation are
separate zero-send cases. See [Testing](TESTING.md#optional-humble-nav2-observation).
This is neither a reusable ROS Host nor completed cancellation/recovery support.
The existing [execution boundaries](DESIGN.md#product-boundary) and
[implementation sequence](DESIGN.md#implementation-sequence) remain authoritative.
This document selects a bounded first slice inside M4; it does not declare M4 complete
or introduce a stable public ROS API.

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
facts; the navigation-specific settlement design still needs investigation and
review before enabling conflicting work. Missing evidence keeps it unresolved.
Compare with direct Nav2 usage at the same declared guarantee: navigation itself
is Nav2's capability; the demo must show the additional Harness admission/result/
settlement behavior rather than attribute ordinary motion to Harness.

Record actual simulation, not an authored robot animation. Include an uncut
reference run alongside a short explanatory edit; disclose speed changes/cuts and
label all footage as simulation. A rosbag replay, if used, is labeled replay.
Physical hardware follows only after the device's own stop/loss protections are
validated. The current implementation covers only the normal observation
increment above. Motion-time cancellation, revision and navigation settlement
remain to be designed and validated; historical probe approval does not cover
those future behaviors.

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
execution records stay outside the product repository. This first increment
introduces no experimental native-closure dependency or settlement API.
