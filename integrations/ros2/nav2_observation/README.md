# Experimental normal Nav2 observation

This optional, source-tree-only example is the first navigation increment inside
M4. It links the real `robot_harness_core` to Humble's
`nav2_msgs/action/NavigateToPose`. It is not an installed ROS SDK, a reusable
navigation Host, or a completed M4 adapter.

The single fixed simulation goal is (-2.0, -0.5) → (0.7, -0.5). A trusted launcher
must create a fresh, exclusively owned, network-isolated simulator, supply its
session identity and writable `/output`, and mount the binary under `/harness`.
The development workspace currently owns that simulator/recording launcher;
this product directory supplies the optional C++ build and execution contract.
External-user simulation packaging remains to be delivered.

## Build

In Ubuntu 22.04 with ROS 2 Humble and `ros-humble-nav2-msgs` installed, from the
repository root:

```sh
source /opt/ros/humble/setup.bash
cmake -S integrations/ros2/nav2_observation -B build-nav2 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-nav2 --target robot_harness_nav2_observation nav2_motion_window_test --parallel 2
ctest --test-dir build-nav2 -R '^nav2_(requires_fresh_deployment|motion_window)$' --output-on-failure --no-tests=error
```

The separate [ROS workflow](../../../.github/workflows/ros2.yml) compiles this target
and checks the finite motion-window predicate and rejection without the deployment
premise. These checks
do not run Gazebo, test navigation, or establish cancellation/settlement. The
normal root CMake build remains ROS-independent.

## Execution boundary

- `M4_ISOLATED_SIMULATION=1`, `M4_FRESH_NAV2_RUN`, a disposable Linux container and
  a one-use `/output/harness-navigation.claim` identify the intended test setup.
  These are trusted launcher assertions and an accidental-repeat guard, not
  authentication or an independently verified remote-server identity.
- Readiness requires advancing clock/odometry, localization near the declared
  spawn, active AMCL and navigator, and the Action server. Discovery alone does
  not prove the absence of old work. Fresh/exclusive deployment is essential;
  never run against an already-used or shared Nav2 instance.
- Core receives the declared startup/profile facts, admits one operation and
  claims dispatch immediately before `async_send_goal` on one owning thread.
- The accepted ROS UUID is checked on feedback and result. Each callback uses
  the same instance-retained operation authority. Result success and a fresh map-frame
  pose within 0.35 m are necessary for output delivery.
- The local result is staged, time is observed, and Core's delivery permission is
  checked immediately before the local in-memory commit, without callback spin
  or re-entrant caller code. The resulting receipt records output acceptance.
- After output acceptance the same owner, Gate, authority, node and executor
  remain alive for a bounded post-result motion observation. It waits up to eight
  wall seconds in total for the first low-speed sample and one continuous quiet
  window. At least ten fresh samples must cover one simulation second; drift is
  at most 0.01 m, yaw change 0.02 rad, speed 0.01 m/s and angular speed 0.02 rad/s.
  Time gaps must not exceed 0.25 s; odometry must track the advancing clock within
  0.25 s, whose last advancement must be within one wall second. Old pre-window
  data is ignored. Missing/invalid new data cannot pass. Once the quiet window
  starts, any violation rejects it without restarting. This is a finite planar
  observation, not ongoing supervision or a device stop guarantee.
- **No settlement evidence is supplied.** The actual second admission attempt
  must return `kDomainOccupied`. Admission is closed before the example exits.
  The result can be accepted while settlement remains pending.

The narrow profile is `nav2-normal-observation-only-v1`: one fixed navigation
request, not a general workload/cancellation capability. No ROS types or policy
state machine were added to Core. The local result is an observation, not a
promise that the robot has stopped or the controller has no remaining work.

## Scenarios and limits

The trusted launcher can select `normal`, `deny-startup`,
`cancel-before-dispatch`, or `withhold-odometry`. The two pre-dispatch cases exercise real Core denial and require
zero native submissions; the experiment also checks negligible Gazebo movement.
Missing startup evidence and revoked pre-dispatch authority must not be bypassed.
The `withhold-odometry` case completes navigation and accepts its output, then
removes the owner's odometry subscription: motion observation must fail and the
domain must remain occupied. The default is one normal goal, with matching
feedback/result, a successful finite motion observation and a blocked second
admission. Each invocation needs its own fresh deployment/output directory.

The launcher independently reads Gazebo position before and after the invocation;
the C++ result alone is not evidence of physical displacement. Timeout, native
rejection/failure, invalid observations and exceptions produce an incomplete
observation. The observation budget is not an operation deadline or stop bound.
Destroying the client/executor does not cancel remote work. Container teardown
owns the test environment; it cannot retroactively mark the Core receipt settled.

The owner is retained through this observation and then closes admission/exits;
it is not yet a persistent multi-task navigation Host.
Safe native rearming and old-command isolation remain prerequisites for settlement
and a second explicit goal.

Motion-time cancellation, goal replacement and native settlement remain outside
the default observation binary. The optional scenarios below add these bounded
paths; provider loss/rebinding and restart recovery remain future work. Keep the
normal observation scenario as the reference comparison.

## Optional sequential settlement example

The separately named `robot_harness_nav2_settlement` binary extends the same Owner
with the [bounded A-to-B contract](../../../docs/ROS2_INTEGRATION.md#sequential-settlement-boundary).
The default observation binary keeps its pending receipt and refused second goal.
Build the optional profile on Ubuntu 22.04/Humble with `nlohmann-json3-dev` and
`ros-humble-std-srvs` / `ros-humble-rosidl-default-generators` in addition to the dependencies above:

```sh
source /opt/ros/humble/setup.bash
cmake -S integrations/ros2/fixture_interfaces -B build-nav2-interfaces -DCMAKE_INSTALL_PREFIX="$PWD/install-nav2-interfaces"
cmake --build build-nav2-interfaces --parallel 2
cmake --install build-nav2-interfaces
source install-nav2-interfaces/share/m4_drive_probe/local_setup.bash
cmake -S integrations/ros2/nav2_observation -B build-nav2 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DROBOT_HARNESS_NAV2_SETTLEMENT=ON -DCMAKE_PREFIX_PATH="$PWD/install-nav2-interfaces"
cmake --build build-nav2 --parallel 2
ctest --test-dir build-nav2 -R '^nav2_' --output-on-failure --no-tests=error
```

The message-only `m4_drive_probe` package allows a clean compile without Gazebo.
It supplies no native server, driver, simulator or settlement guarantees; do not
install it alongside a different package of the same name. Licensing remains
undecided for the project. These experimental fixture interfaces are not a stable
public robot API. The isolated research deployment supplies matching service
implementations, fixed producer bindings and one-use native task contexts.

At runtime the launcher supplies `/output/producer-context.json` with distinct
32-character lowercase hexadecimal `a_scope` and `b_scope`, task-bound bridges,
and `/prepare_next_context`. B uses namespace `/m4_task/s_<b_scope>`; creating it
must not send a goal or open the drive. The Owner independently verifies readiness
and owns every Core admission, drive open and native goal send. Supported cases
are `normal`, `withhold-planner-ack`, `withhold-drive-ack`, `withhold-odometry`,
`missing-map` and `missing-controller`. The last two require the launcher to omit
the corresponding B dependency. Normal must emit `owner_handoff_result` with
A settled, B admitted/native closed, B unsettled and third admission blocked.
Expected closure/readiness fault cases emit `owner_handoff_blocked` with one
native send. `withhold-feedback` additionally exercises a result-observation
failure: it exits 1 with `incomplete`, without A settlement or B admission. The
Owner reports the error outside ROS callbacks instead of throwing through an
already fulfilled Action promise.

CI compiles both binaries and checks the deployment guard, motion window and
native evidence matcher. Actual navigation and closure require the separate
simulation evidence described in [Testing](../../../docs/TESTING.md#optional-sequential-nav2-settlement).
The public repository does not yet provide an end-to-end simulator launcher.

## Cancel an accepted goal while moving

The optional binary accepts `cancel-moving` and the three
`cancel-moving-withhold-planner-ack`, `cancel-moving-withhold-drive-ack`,
`cancel-moving-withhold-odometry` fault cases. Run them only through a fresh
exclusive simulator launcher with the same fixture mounts as the sequential
profile. The native FollowPath leaf must publish the accepted child identity
while running; older success-only fixtures cannot establish cancellation closure.

The trigger is fresh observed movement of at least 0.5 m and speed 0.05 m/s.
The Owner makes one targeted cancellation, rejects result delivery after
revocation, waits for the separate native terminal/closure observations and
checks a fresh quiet window. Output logs include `owner_cancel_requested`,
`owner_cancel_response`, `native_result`, native observations and
`owner_cancellation_result`. The final event distinguishes `native_closed` and
`motion_observed`; `passed` in a fault case means the expected evidence was
withheld, not that the robot was proven stopped.

The receipt stays pending and second admission is blocked in all four cases:
this cancellation-only scenario prepares no replacement context. It does not
implement cancellation during pending acceptance, runtime
loss supervision or a hard stopping bound. Details are in the
[cancellation contract](../../../docs/ROS2_INTEGRATION.md#movement-time-cancellation-boundary).
The Humble workflow also runs `nav2_cancel_response`, checking exact target UUID,
empty/multiple response entries, refusal and unknown/terminated-goal responses.

## Replace an accepted goal while moving

`replace-moving` cancels A after the same motion trigger, then uses the same
Owner's closure, fresh quiet, B-context readiness, Core settlement/admission and
generation reopening flow. B targets (0.0, -0.5), so it must actually navigate
from the cancellation position. Only A triggers automatic cancellation; B's
result is delivered normally and B stays pending without a C context.

The five `replace-moving-` fault suffixes are `withhold-planner-ack`,
`withhold-drive-ack`, `withhold-odometry`, `missing-map` and `missing-controller`.
Each must block B admission/open/send. Normal output includes the cancellation
events, `owner_core_handoff` (with A cancellation/output facts) and
`owner_handoff_result`; faults end with `owner_handoff_blocked`.

Use the same fresh research launcher and pinned native fixture. The replacement
check injects late A commands through A's raw input and retained smoother while
B moves; their generation remains 1 and the drive rejects them. See the
[replacement contract](../../../docs/ROS2_INTEGRATION.md#movement-time-replacement-boundary)
and [validation](../../../docs/TESTING.md#optional-movement-time-nav2-replacement).

## Lose fresh observations while navigating

The optional binary checks clock/odometry progress during each native goal and
before result delivery. Two seconds without advancing valid stamps withdraws
the Core binding, denies output/new admission and requests the exact accepted
goal's native stop once. This uses monotonic Owner time; repeated old messages
do not renew the observation. It is a fixture threshold, not a stopping bound.

The four `runtime-` cases are `runtime-odometry-loss`, `runtime-odometry-replay`,
`runtime-odometry-resume` and `runtime-clock-loss`. Their research relay remaps
only Owner inputs (`/owner_odom`, `/owner_clock`), starting the fault after real
motion. All retain a withdrawn binding and pending settlement. Restored data
can support quiet observation, but cannot authorize B or revive result delivery.
Logs distinguish `owner_runtime_observation_lost`, the real native observations
and `owner_runtime_loss_result`.

Build `nav2_runtime_observation_watch_test` alongside the existing targets and
select `^nav2_` CTests (five checks). Humble CI includes that target. The test
checks wall-time expiry, frozen/backward stamps, invalid odometry and fresh
observation recovery; actual robot behavior requires the separate simulator.
The native stop/closure path remains reachable in these tests. See the
[contract and exclusions](../../../docs/ROS2_INTEGRATION.md#runtime-observation-loss-boundary)
and [runtime validation](../../../docs/TESTING.md#optional-runtime-observation-loss).

## Isolate motion when the navigator cannot answer

Use the separate research modes `runtime-stop-unreachable` and
`runtime-stop-unreachable-withhold-drive-ack`. They freeze the real navigator
executor after motion starts, keep downstream producers and drive alive, and
restore the Owner's interrupted odometry after six wall seconds. Run through the
exclusive research launcher; the product executable does not inject the fault.

At withdrawal, the Owner independently requests the current drive generation's
closure. `owner_runtime_loss_result` distinguishes `drive_isolated`,
`motion_observed` and `native_closed`; missing native results remain pending.
Fresh quiet and driver isolation cannot authorize another task. The withheld-ACK
case must report no confirmed isolation/quiet even if the simulator stops.

The existing `nav2_closure_observations` CTest now also checks early drive ACKs,
partial closure and producer failure without conflating those facts. CI already
runs this target; no new test selector is needed for these assertions. See the
[consumer isolation boundary](../../../docs/ROS2_INTEGRATION.md#independent-consumer-isolation).
