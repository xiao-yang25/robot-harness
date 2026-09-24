# Run the navigation simulation

This package builds the M4 demonstration from this repository: a TurtleBot3 Waffle
in Gazebo Classic 11, ROS 2 Humble / Ubuntu 22.04, Nav2 1.1.20 and
`gazebo_plugins` 3.9.0. It includes the native worker extensions, scoped drive,
Owner, launch support and independent scenario checks. No research workspace,
prebuilt project image or host ROS installation is required.

It is an isolated experimental simulator, not a general ROS adapter or robot
controller. The first package runs without a graphical window. See the
[recorded demonstration](https://xiao-yang25.github.io/robot-harness/) for the
visual introduction; the recording is a separate earlier normal-navigation run.

## First run

Use Python 3.9+ and Docker on Linux or macOS. This package selects `linux/amd64`;
Apple Silicon requires Docker Desktop's amd64 emulation. Reserve at least 4 GB
for the container and allow extra memory and disk for the image build. The first
build downloads ROS/Gazebo dependencies and compiles the project with two jobs.
Run these commands from the repository root:

```sh
python3 integrations/ros2/simulation/simulate.py build
python3 integrations/ros2/simulation/simulate.py run normal --output simulation-results/normal-01
```

Each run must use a **new output directory**. The normal case navigates A from
(-2, -0.5) to (0.7, -0.5), observes native closure and fresh quiet, prepares and
checks a new B context, settles A, and admits B back toward (-1.5, -0.5).
B must move and finish; its final receipt remains pending and a third admission
is refused. Passing does not mean every receipt is settled.

The launcher prints the output location and final status. It returns nonzero on
failed assertions, startup failure, timeout, interruption or failed container
cleanup. Allow up to about five minutes per case; startup and emulation affect
elapsed time. There is no automatic retry.

## Try cancellation, replacement and loss

```sh
python3 integrations/ros2/simulation/simulate.py run cancel-moving --output simulation-results/cancel-01
python3 integrations/ros2/simulation/simulate.py run replace-moving --output simulation-results/replace-01
python3 integrations/ros2/simulation/simulate.py run runtime-stop-unreachable --output simulation-results/stop-01
python3 integrations/ros2/simulation/simulate.py run runtime-stop-unreachable-withhold-drive-ack --output simulation-results/no-ack-01
```

| Case | What must happen |
|---|---|
| `normal` | A completes and closes; B is actually admitted and moves under a new generation |
| `cancel-moving` | A moves about 0.5 m before exact-goal cancellation; native closure and fresh quiet are observed separately; no B is sent |
| `replace-moving` | Moving A is cancelled; closure/readiness permit B toward (0, -0.5); old A commands are rejected while B moves |
| `runtime-odometry-loss` | Advancing odometry disappears during motion; authority is withdrawn |
| `runtime-odometry-replay` | Repeated old odometry cannot renew observation freshness |
| `runtime-odometry-resume` | Odometry resumes after expiry; authority does not return automatically |
| `runtime-clock-loss` | Simulation clock observations stop advancing; authority is withdrawn |
| `runtime-stop-unreachable` | The native navigator is paused during motion and odometry is withheld; independent drive sealing still takes effect; native result stays unknown and B is denied |
| `runtime-stop-unreachable-withhold-drive-ack` | The drive seals physically but the Owner lacks its ACK; the Owner cannot claim drive isolation/quiet; B remains denied |

A fault case passes when the required conservative behavior and external evidence
are present. An unknown native result is expected in the last two cases. Container
destruction is cleanup after the check, never evidence of task settlement.

## Read the evidence

Start with `run.json` (case, local image identity, container exit/OOM state and
cleanup outcome and unique container name), then `verification.json` (scenario assertions), and
`owner.jsonl` (admission, UUID, cancellation, withdrawal and closure facts).
`packages.txt` records installed dependency versions. Diagnostic detail remains
in `container.log`, `launch.log`, `context-launcher.log`, `drive-native.jsonl`,
`producer-bridge.jsonl`, the native node logs and, where relevant,
`runtime-relay.jsonl` / `old-producer-injection.jsonl` / `b/`.

The image is built from a public ROS base and available apt packages. Nav2,
drive-plugin and model package upstream versions are constrained; native CMake
also checks Nav2 and drive compatibility exactly. Distribution rebuild suffixes,
base layers and transitive apt dependencies can change. This is a reproducible
source/build procedure, **not a byte-identical or permanently archived image**.
If a required upstream version leaves the repository, the build fails and needs
an explicit compatibility update. Do not silently relax the version checks.

The read-only `simulation-diagnostics.jsonl` samples raw `/clock`, `/scan`,
`/odom` and the map/odom transforms about once per wall-clock second, alongside
Gazebo process states and cgroup CPU counters where available. Each stream records
its last/highest timestamp, receipt age and the age since its highest timestamp
advanced; repeated or older timestamps do not reset that age. Absent streams are
omitted. A clock reset requires interpreting a new timeline; the diagnostic
high-water mark does not reset automatically. These are observations at a separate subscriber, not proof that a
controller received the same data. Raw odometry is upstream of fault injection.
`diagnostics.log` records observer failures; missing diagnostics do not grant or
revoke authority and do not override the scenario's existing verdict.

`gazebo-queries.jsonl` records each actual pose query's phase, elapsed time,
return code and bounded output. On timeout it samples the still-existing query
and Gazebo processes before killing/reaping the query. The A-closed query retains
its three-second wait, and checkers retain five seconds; there is no retry or
fallback to Owner/odometry coordinates. Diagnostic collection and cleanup can
add time after a timeout, within the outer container limit.

## Isolation and cleanup

The launcher creates one container with no network, no host devices, no added
capabilities and no host ROS access. It fixes CPU (2), memory (4 GB), PID (512)
and shared-memory (256 MB) limits, and mounts only the new output directory.
The process uses the caller's UID/GID. Source, messages and native binaries are
inside the image; no private build directories are mounted.

Each run gets a new deployment identity and one-use output claim. These are
trusted-launcher premises, not cryptographic authentication. The container
allows 300 seconds plus 10 seconds for forced termination; the host has its own
330-second wait limit. Ctrl-C removes only this invocation's container and keeps
logs. The local image remains available for the next run. If Docker becomes
unreachable, `run.json` reports failed cleanup; use the ID in `container.cid` to
inspect and remove that container after restoring Docker. If creation times out
before Docker returns an ID, cleanup is explicitly unconfirmed; use the unique
`container_name` in `run.json` to reconcile the daemon state. Host SIGKILL or power
loss cannot execute cleanup. Never attach this image to a physical robot.

To discard the image after finishing:

```sh
docker image rm robot-harness-simulation:humble
```

Do not use a global Docker prune: other tasks may need their images and volumes.

## If a run fails

- Build/network errors: preserve the build output and check access to Docker Hub
  and ROS/Ubuntu package repositories. Configure any required proxy locally;
  shell proxies do not necessarily configure the Docker daemon/build container.
  `build --ubuntu-mirror HTTPS_ARCHIVE_URL` optionally selects a reachable Ubuntu
  mirror (for example `https://mirrors.tuna.tsinghua.edu.cn/ubuntu`). Ubuntu archive
  signatures and exact native version checks stay enabled; ROS packages still
  come from the configured ROS repository. The default uses the official archives.
- Version mismatch: report the missing version and `packages.txt` if available;
  the native wrappers depend on the stated Humble APIs.
- Admission/TF failure: inspect `owner.jsonl`, `launch.log`, the B launch log
  and `simulation-diagnostics.jsonl`. Compare raw clock/scan/TF progress, while
  retaining the distinction between observer reception and local controller TF.
  Earlier research runs observed missing/stale map-to-odom transforms. This
  package does not claim those stability problems have been resolved.
- External pose-query timeout: inspect `gazebo-queries.jsonl` for phase, child
  state and server state, then correlate with the timing samples. The checker
  retains its five-second query limit;
  an earlier emulated run exceeded it. A later pass does not explain that failure.
- OOM or timeout: retain `run.json` and all logs. Do not count a partial run as
  success or retry over the same directory.

Owner survival, polling and a reachable independent drive channel remain
prerequisites. No Owner restart recovery, full network-partition guarantee,
hard stop deadline or physical stopping-distance assurance is established.

## Source layout

`simulate.py` owns the disposable container. `entrypoint.sh` bounds the container
run; `run_owner_handoff.sh` starts and supervises the fixture. `native_worker/`
adds close observations to the real Nav2 nodes/BT actions. `native_drive/` checks
scope/generation before applying motion; both drive and Owner use the existing
[`fixture_interfaces`](../fixture_interfaces/CMakeLists.txt) definitions.
The checks use Gazebo pose and runtime observations in addition to Owner output.
Before replacing Nav2's fresh inactive action servers, the wrappers require an
unknown-UUID result response from each server's own executor. This prevents
immediate replacement from cancelling a thread before its spin loop starts;
service discovery alone is insufficient. The bounded query submits no goal and
does not provide task completion or settlement evidence.
See [third-party notices](THIRD_PARTY_NOTICES.md) for the derived drive source;
the project's licensing decision remains pending.
