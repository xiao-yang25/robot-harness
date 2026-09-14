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
```

Local macOS commands and the current SDK workaround are in the
[README](../README.md). A Linux container or VM on a Mac may also catch build
compatibility issues, but cannot replace target-host timing measurements.

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

The three CTest entries contain multiple behavior checks, not just three
assertions. A successful test executable may print nothing; use CTest's exit
status and failed-check messages. Hand-inspecting the normal example does not
replace Core negative-evidence tests or establish robot motion safety.

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

## Change-to-check mapping

| Changed scope | Check entry and expected result | Status / evidence location |
|---|---|---|
| Core, sample fixture, example and CMake | README configure/build commands; CTest runs `robot_harness.authority_gate`, `robot_harness.sample_execution`, and `robot_harness.normal_execution_example` | Registered in `tests/CMakeLists.txt`; local CTest output and `build/Testing/Temporary/LastTest.log`, or corresponding custom build directory |
| C++ formatting and naming | Follow [Coding style](CODING_STYLE.md); run clang-format on changed C++ files and review names | Local formatter check; not currently a CI job or behavior test |
| Ubuntu Core workflow | Parse workflow YAML, inspect its commands/permissions and diff; after push, inspect the completed `Core on Ubuntu` job for the tested commit | M0 passed at `0be526a`; M1 passed at `7eb0e76`; see the linked Actions results above |
| M1 normal action/events | Check fresh initialization, active host with not-ready worker, one complete sample operation, a sequential second operation, synchronous/deferred callbacks, rejected input, duplicate/wrong-operation evidence and clean fixture shutdown; compare actual worker submissions and sink results with layered receipts | Implemented: `tests/authority_gate_tests.cpp` and `tests/sample_execution_tests.cpp`; macOS and Ubuntu results passed within the coverage above |
| M2 failure/cancel | Native rejection/failure, cancel ACK before settlement, expired deadline, missing/partial-effect evidence; no unearned success or conflicting redispatch | Planned; extend the M1 Core/native CTest suite |
| M3 replacement/recovery | Actual sink rejects held old output; unsettled conflicts block; provider/Core restart requires fresh observations and authority; invalid recovery stays closed | Planned; no replacement or recovery implementation exists |
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
