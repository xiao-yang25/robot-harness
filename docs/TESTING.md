# Testing environments

Use macOS for fast local Core development and Ubuntu for Linux build and
correctness coverage. Robot integration and deployment measurements require
their relevant target environments.

## Current validation baseline

The latest functional baseline is M4's sequential navigation slice, merged as
`e2e1ebe` through [PR #10](https://github.com/xiao-yang25/robot-harness/pull/10).
Its [Core CI](https://github.com/xiao-yang25/robot-harness/actions/runs/35768798923)
passed all 57 Linux tests in Debug and ASan/UBSan; its
[Humble CI](https://github.com/xiao-yang25/robot-harness/actions/runs/35768798949)
compiled both optional binaries and passed three local checks. That job does not
run a simulator. Actual navigation observations are recorded in the
[sequential verification section](#optional-sequential-nav2-settlement).

The moving-cancellation increment adds a fourth local Humble check and the
[bounded simulation cases](#optional-movement-time-nav2-cancellation) below.
The [replacement scenarios](#optional-movement-time-nav2-replacement) reuse
those checks and add separate simulation coverage. The linked hosted runs above
predate those increments and the runtime observation-loss work below.

The sections below retain older dated evidence, including M3c at `dabad9b`
([PR #7](https://github.com/xiao-yang25/robot-harness/pull/7)). Historical counts
and results do not replace checks for the revision being changed.

| Environment | Current coverage | Limits |
|---|---|---|
| GitHub-hosted Ubuntu 22.04 x86_64, GCC | All 57 CTests in Debug and ASan/UBSan | No ROS, hardware or deployment-timing validation |
| Ubuntu 22.04 ARM64 in local Docker, GCC | All 57 Core CTests in Debug for the M4 second slice; earlier recovery-specific sanitizer results retained below | Runs in a Linux VM; not a target-device performance result |
| macOS ARM64, AppleClang | All 47 portable CTests in Debug and ASan/UBSan before the Linux-only launcher correction | Linux recovery targets are not built; macOS is not currently a hosted CI job |
| Optional Ubuntu 22.04/Humble build | Both Nav2 binaries and five local observation/deployment/cancel-response/freshness checks; separate Gazebo evidence below | Automatic CI compiles/tests predicates and launcher boundaries; full movement uses the separate manual simulation workflow |
| Other platforms/toolchains | No verified support claim | CMake platform branches alone are not platform validation |

For a first run use [Build](../README.md#build) and the
[example guide](../examples/README.md). The normal CTest command discovers all
tests built for that platform. Later revisions must carry their own CI results;
the links here establish only the recorded revision.

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
research launcher/recorder and browser viewer are not yet packaged here for
external users. Earlier startup-query failures are retained as incomplete runs;
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

The [Humble/Gazebo tutorial](../integrations/ros2/simulation/README.md) builds the
Owner, native worker extensions and drive directly from repository source. The
source package removes the external research launcher/build-mount prerequisite
for the nine listed settlement/cancellation/loss scenarios. Earlier observations
in this document remain tied to their original fixtures and revisions.

The automatic [Humble workflow](../.github/workflows/ros2.yml) retains its five
Owner predicate checks, eleven Python launcher/mirror checks and seven diagnostic
checks. The latter use real child processes to verify query success, malformed
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
separately. The image build runs five Nav2 predicate tests plus the native BT
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
