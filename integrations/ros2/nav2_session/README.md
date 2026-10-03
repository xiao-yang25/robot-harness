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

Cancel, close and client EOF revoke authority and schedule native cancellation
and exact outlet closure. An intent response is not physical stop confirmation.
Cleanup reports unknown stop and retains pending settlement. Final process-tree
release belongs to the launcher/container boundary. Owner restart, general site
configuration, context replenishment and a hard stop limit are not implemented.

Pure endpoint/package checks run with the Python test suite. With Humble and the
custom drive interfaces available, run the focused actual owner methods:

```sh
PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}" \
  python3 integrations/ros2/nav2_session/tests/linux_driver_checks.py
```

These use the real Core and synthetic ROS edges; they test scheduling and error
isolation, not physical stop. See [Runtime design](../../../docs/RUNTIME_SLICE.md#installed-bounded-nav2-owner)
and [Testing](../../../docs/TESTING.md#navigation-request-entry) for qualification limits.
