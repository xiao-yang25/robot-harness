# Navigation recording

[Watch on the project website](https://xiao-yang25.github.io/robot-harness/#demo)
or [download the original MP4](navigation.mp4).

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
provides a source build, nine bounded scenarios and an optional live viewer.
This historical recording does not demonstrate those later capabilities;
public cancellation and handoff recordings remain to be added.
