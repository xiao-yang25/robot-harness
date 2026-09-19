# Testing environments

Use macOS for fast local Core development and Ubuntu for Linux build and
correctness coverage. Robot integration and deployment measurements require
their relevant target environments.

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
protection. The verified M2a and M2b/M2c Ubuntu results are recorded below.

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

Local macOS commands and the current SDK workaround are in the
[README](../README.md). A Linux container or VM on a Mac may also catch build
compatibility issues, but cannot replace target-host timing measurements.

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

Expected for the existing M2 implementation: nine tests in each configuration.
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
sink rejection preserves native success while reporting failed delivery.

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
| M2a: worker or sink becomes unavailable after initialization, before dispatch | Adapter makes no native submission; admitted authority is closed only with correlated non-submission and cleanup evidence; no retained request can execute later. A fresh caller request works after availability is restored |
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
This increment has not been pushed, so no matching GitHub CI run exists yet.
Provider generation changes, restart recovery, hard stopping deadlines and robot
effects remain outside these checks.

## Planned M3b and M3c checks

These checks implement the existing [M3 scope](DESIGN.md#planned-m3b-and-m3c-boundaries).
They are not registered tests or evidence of PASS. Preserve M1–M3a regressions;
add focused CTests with each implementation and run the existing macOS/Ubuntu
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

When the minimal task caller is added, register its normal multi-step completion,
goal change, failure/unknown handling and clean shutdown as integration checks.
The caller must use public interfaces. A dependent normal step needs the accepted
prior result and required settlement; replacing a cancelled goal instead follows
the declared handoff conditions and does not require a discarded result to be
accepted. Neither path changes Core's unassessed domain verdict.
Extend that same caller test with M3b/M3c as supported. A future separate Agent
repository must exercise the actual Harness dependency in a small cross-repository
test; passing each repository independently is insufficient. ROS dependencies and
native cross-path checks remain separate M4 work, not part of today's Core CI.

## PR review and evidence

The shared guide owns review policy; this section maps it to this repository.
Use the [PR template](../.github/pull_request_template.md) to record the proposed
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
| M3 replacement/recovery | Actual sink rejects held old output; unsettled conflicts block; provider/Core restart requires fresh observations and authority; invalid recovery stays closed | M3a same-host/same-binding operation replacement implemented; see [29-test evidence](#m3a-replacement-checks). Provider rebinding and restart/recovery remain planned |
| M4 ROS and cross-path behavior | Map supported normal, cancellation, loss, late-output and recovery paths to Ubuntu native observations and receipts | Planned; target access, dependencies, commands, cleanup and evidence entry must be supplied with this slice |
| Markdown / project instructions | Inspect diff, local links and anchors, code fences, personal-path/credential leakage, and affected command syntax; review any changed normative scope under applicable shared rules | Use the host's available documentation checks or targeted inspection; retain results in the current task record |

For Core admission, cancellation, settlement, recovery, and adapter ownership
changes, use the shared engineering guide's applicable independent-review rule;
the registered tests alone do not complete that review. Do not create another
project-specific copy of the review policy.

No robot target or credential is configured by these instructions. Missing Linux
or hardware evidence remains explicit; writing a test plan does not satisfy it.
Build output is local and ignored by Git. Do not add log archives, private host
details, or a second status registry merely to record a check.
