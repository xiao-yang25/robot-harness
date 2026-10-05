# Bounded Nav2 Session owner

Optional `robot_harness_nav2` is installed alongside the experimental Python
bridge. It owns ROS progress, native goal correlation, scoped closure and Core
request records. A separate caller uses `NavigationSession`; the owner starts
neither an Agent nor a model.

This profile supports only the isolated Ubuntu 22.04/Humble simulation with the
custom scoped workers and drive outlet in [simulation](../simulation/README.md).
It has fixed sites A/B, two prepared native contexts, one client connection and
an exclusive motion domain. A can settle with fresh B readiness; final B remains
pending. A generic Nav2 stack or real robot is not a supported substitute.

Local installed-owner normal consumption, focused lifecycle checks and the
standalone public host-CLI route passed for `scoped-two-context-nav2-shim-v1`.
It wraps the unchanged DWB controller with native Rotation Shim for large initial
heading differences; the original 0.5 m/10 s progress gate remains. Earlier bare-DWB
failure evidence retains its original profile; the exact cause remains unresolved.
Separate local probes observed protection after public cancel during B rotation
and actual client-process loss during B translation. Applied outlet closure,
sustained old-command rejection and external quiet were verified; abort still
reports stop unknown/settlement pending. These results do not qualify arbitrary
routes, a native reuse certificate, owner loss or a hard stop deadline.

Installed business-Agent A→B normal consumption also has [local candidate evidence](../../../docs/TESTING.md#m6b-navigation-business-agent).
It preserves final pending settlement and does not qualify a general model-navigation stack.

Build the optional bridge in Linux with Python development headers and install
it into a prefix (see [Python bindings](../../../bindings/python/README.md)).
Use that **Linux** prefix with an existing qualified local simulation image:

```sh
python3 integrations/ros2/simulation/simulate.py session \
  --image YOUR_SCOPED_HUMBLE_IMAGE \
  --python-prefix /absolute/linux-install-prefix \
  --output /absolute/new-result-directory
```

The launcher mounts the current product simulation source, starts the scene,
then the installed `python3 -m robot_harness_nav2` owner and an independent
[public A/B example](../../../examples/navigation_requests.py). No research
source directory is needed. Container budget: 2 CPU, 4 GiB, 420 seconds,
linux/amd64, no external network. The existing image must contain `/native` and
`/drive` from the scoped simulation build. This command checks process completion;
physical task qualification uses a separate external observer/evaluator.

To run another trusted local Python client, `session` accepts `--client-script`
and an optional `--client-prefix`. The script receives the endpoint as its only
argument, runs in the same container and can write its report under `/output`.
Both inputs are mounted read-only; the prefix is available at `/client-prefix`
and is added to that client's Python path only, not to the owner or ROS processes.
These are executable operator-supplied components, not a sandbox or backend plugin.
Docker bind paths containing commas are refused.

`--caller-wait-seconds 45` explicitly forwards the existing owner idle setting.
Values must be finite and in `(0, 60]`. Omitting all three options preserves the
default request example and owner waits. Client failure propagates to the
launcher's existing exact-container cleanup; a zero exit still establishes only
process completion. The [business tutorial](https://github.com/xiao-yang25/robot-agent)
owns task strategy and its declared provider, rather than this launcher.

B context startup first waits for its native action endpoint within the existing
70-second preparation deadline, then queries lifecycle services with the existing
RPC budgets. This endpoint alone grants no authority; full readiness, fresh input
and the sealed motion outlet checks remain required before admission.

Readiness checks the owner, scene and bridge processes individually, including
when an endpoint file has just appeared. If any child exits, the supervisor
reports its role, PID and exit status and enters its existing cleanup path;
another live child cannot hide the failure until the 160-second endpoint deadline.
The child logs remain in the result directory. A B-preparation service failure
also records `context_preparation_failure` in `caller.jsonl`, with the service,
discovery or response phase, stage budget, elapsed time and native context exit
status. These diagnostics preserve the original exception and 5-second service /
70-second context budgets. They locate the failed wait, not its underlying cause.

Within that isolated scene, after sourcing Humble and the custom drive interface
setup, the owner can also be started directly. Preserve their Python search paths:

```sh
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" python3 -m robot_harness_nav2
```

A model caller may need more idle time than the deterministic example. The
owner defaults to 15 seconds before each admission and 10 seconds before final
client close. Explicit `--caller-wait-seconds` accepts finite values in `(0, 60]`
and applies to both idle waits. For a 30-second proposal and up to three 5-second
public RPC waits, this candidate uses 45 seconds. The in-container `navigation_session.sh` entry reads
`M6_CALLER_WAIT_SECONDS=45`; the host `simulate.py session` command runs the
deterministic example with defaults and does not forward this host environment
variable. To change an independent model caller's budget, configure the owner
explicitly within the isolated scene. This does not configure the model itself. Idle timeout
still aborts with uncertainty, and EOF/cancel remain serviced during the wait.
This changes neither native operation deadlines nor observation freshness,
physical closure predicates or final B settlement.

```sh
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" \
  python3 -m robot_harness_nav2 --caller-wait-seconds 45
```

After readiness, `/output/navigation-endpoint.json` identifies its private local
socket. The external client must run in the same container. The socket lives
outside shared output mounts, with directory mode 0700 and socket mode 0600.
The CLI rejects startup outside the required isolation flags before importing ROS.

In the default and finite-permission profiles, cancel, close and client EOF revoke authority and schedule native cancellation
and exact outlet closure. An intent response is not physical stop confirmation.
Cleanup reports unknown stop and retains pending settlement. Final process-tree
release belongs to the launcher/container boundary. Owner restart, general site
configuration, context replenishment and a hard stop limit are not implemented.

Finishing the native A/B sequence does not settle final B. An explicit final
cancel enters client shutdown handling even after the native result was accepted;
the successful result remains accepted while authority is revoked and settlement
stays pending. A normal close already received can still finish the terminal
wait. Neither path promotes an intent response to complete native cleanup.

Pure endpoint/package checks run with the Python test suite. With Humble and the
custom drive interfaces available, run the focused actual owner methods:

```sh
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" \
  python3 integrations/ros2/nav2_session/tests/linux_driver_checks.py
```

These use the real Core and synthetic ROS edges; they test scheduling and error
isolation, not physical stop. See [Runtime design](../../../docs/RUNTIME_SLICE.md#installed-bounded-nav2-owner)
and [Testing](../../../docs/TESTING.md#navigation-request-entry) for qualification limits.

### Explicit finite motion profile

Select `--profile scoped-two-context-nav2-owner-permit-v1` on the host `session`
command to use the installed finite-permission Owner with a rebuilt scoped
simulation image. The default remains `scoped-two-context-nav2-shim-v1`.
The image and install prefix must both contain the new private interfaces and
Owner code; a legacy image cannot provide the finite services.

```sh
python3 integrations/ros2/simulation/simulate.py session \
  --profile scoped-two-context-nav2-owner-permit-v1 \
  --image YOUR_REBUILT_SCOPED_HUMBLE_IMAGE \
  --python-prefix /absolute/linux-install-prefix \
  --output /absolute/new-result-directory
```

The Owner renews issue-stamped one-second permissions from its own loop. When
progress stops, the drive independently expires permission, rejects later motion
and attempts actual wheel zero application. A fault-closed instance cannot be
reopened. This protects the selected motion outlet; it does not prove a hard
physical stop deadline, stop other workers, recover an Owner, or settle Core.
See [design](../../../docs/DESIGN.md#finite-owner-motion-permission) and
[checks](../../../docs/TESTING.md#finite-owner-motion-permission). Existing
business-Agent consumers retain their declared default profile/version pin.

### Explicit cancellation settlement profile

`scoped-two-context-nav2-revision-v1` uses the existing shim and scoped workers.
It allows a publicly cancelled, confirmed accepted A to settle and release only
after actual native terminal, scoped closure, fresh quiet and fresh B readiness.
Native success racing cancel stays succeeded; cancel intent alone remains unknown.
Late unconfirmed acceptance, B cancel, EOF, global close, expiry or missing
evidence use unresolved shutdown, without B reuse. The shared closure deadline
is at most 15 seconds, clipped to the original operation and task deadlines.

Select it explicitly with a **new Linux install prefix** and a trusted public
Session caller. The standard A/B example still runs its normal sequence:

```sh
python3 integrations/ros2/simulation/simulate.py session \
  --profile scoped-two-context-nav2-revision-v1 \
  --image YOUR_SCOPED_HUMBLE_IMAGE \
  --python-prefix /absolute/new-linux-install-prefix \
  --client-script /absolute/your-session-caller.py \
  --caller-wait-seconds 45 --output /absolute/new-result-directory
```

The caller can query A's retained receipt; it must await `released` and acquire
a fresh observation reference before submitting B. Local installed controlled
Humble runs exercised moving-A cancel/reuse and actual paused-B refusal.
This requires a live, progressing Owner and does not include finite permission,
restart or a hard stop deadline. Agent instruction polling and model-guided
revision remain separate business features; the existing pairing is unchanged.
See [design](../../../docs/DESIGN.md#navigation-cancellation-settlement) and
[checks](../../../docs/TESTING.md#navigation-cancellation-settlement).

### Explicit planning-failure settlement profile

`scoped-two-context-nav2-failure-recovery-v1` can release a confirmed failed A
only after positive native planner/unsent-controller closure, drive ACK, fresh
quiet and fresh B readiness. An entered-but-unconfirmed child remains unknown;
missing or contradictory evidence cannot enable B. Normal A is unchanged,
while B failure, cancellation, EOF and global close use unresolved shutdown.

Use a fresh Linux install and a simulation image rebuilt from the same source:
select `--profile scoped-two-context-nav2-failure-recovery-v1` in the Session
command above. The trusted caller must inspect A's released failure receipt and
obtain a new observation reference before an explicitly allowed B submission.
The Owner does not choose or send a backup. This fixed, exclusive, live-Owner
profile does not inherit finite permission, arbitrary BT, restart or hard-stop
qualification. See [design](../../../docs/DESIGN.md#navigation-failure-settlement)
and [checks](../../../docs/TESTING.md#navigation-failure-settlement).

## Explicit recovery test scenes

The public launcher accepts `--scene normal` (default) or `--scene occupied-a`.
The latter requires `--profile scoped-two-context-nav2-failure-recovery-v1`;
other combinations fail before Docker or ROS starts. Build a current simulation
image: earlier images may lack the failure leaves or current drive interfaces.

`occupied-a` derives a static map from the installed TurtleBot3 world map,
marking a 0.8 m square around A occupied while keeping B and the start free.
It changes the planner map, not the physical world. The installed Owner consumes
actual `/map` before preparing B or exposing the request endpoint and checks
frame/geometry and occupied A/free B/free start. Its capabilities and measurements
use `turtlebot3-occupied-a-probe-v1`; normal remains `turtlebot3-world-v1`.
The retained `probe` identifier denotes this bounded test scene.

```sh
python3 integrations/ros2/simulation/simulate.py session \
  --image YOUR_CURRENT_SCOPED_HUMBLE_IMAGE \
  --python-prefix /absolute/new-linux-install-prefix \
  --profile scoped-two-context-nav2-failure-recovery-v1 --scene occupied-a \
  --client-script /absolute/trusted-recovery-client.py \
  --client-prefix /absolute/linux-agent-prefix --caller-wait-seconds 45 \
  --output /absolute/new-recovery-result-directory
```

Use a recovery-aware caller, such as the separate business Agent tutorial; the
default A/B request example does not make a business recovery decision. Scene
selection never allocates authority or skips native closure/quiet/B-readiness
checks. `navigation-scene.json` records the selection; `map-fixture.json` and
`occupied-a.yaml`/`.pgm` record preparation; `scene_map_consumed` in `caller.jsonl`
records the actual loaded map sample. These diagnostics alone do not prove the
entire map, physical obstacles, dynamic map updates or general recovery safety.
