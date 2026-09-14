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
protection. This configuration still needs a successful Ubuntu run for M2a.

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

The five CTest entries contain multiple behavior checks, not just five
assertions. A successful test executable may print nothing; use CTest's exit
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
Core and host tests; the M2b/M2c rows remain planned, not executed checks.
The M2a implementation based on `fc283ec` passed all five CTests on
macOS with AppleClang, both normally and with AddressSanitizer and
UndefinedBehaviorSanitizer. The original pre-dispatch refusal reproducer failed
on M1 and passed on M2a, executing only the new request with actual result `9`.
Independent code review passed for that implementation. A separate probe checked
actual sink classification, deferred non-submission, retained failure controls,
and shutdown with an unavailable sink. Ubuntu validation remains pending; the
earlier M1 Linux run does not validate M2a. The host task record retains the
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
| Ubuntu Core workflow | Parse workflow YAML, inspect its commands/permissions and diff; after push, inspect the completed `Core on Ubuntu` job for the tested commit | M0 passed at `0be526a`; M1 passed at `7eb0e76`; see the linked Actions results above |
| M1 normal action/events | Check fresh initialization, active host with not-ready worker, one complete sample operation, a sequential second operation, synchronous/deferred callbacks, rejected input, duplicate/wrong-operation evidence and clean fixture shutdown; compare actual worker submissions and sink results with layered receipts | Implemented: `tests/authority_gate_tests.cpp` and `tests/sample_execution_tests.cpp`; macOS and Ubuntu results passed within the coverage above |
| M2 failure/cancel | Follow the M2 planned checks above: explicit failure closure, cancel ACK before settlement, expiry, missing/partial-effect evidence; no unearned success or conflicting redispatch | M2a implemented in Core tests and `tests/m2a_failure_tests.cpp`, plus the failure example; M2b/M2c remain planned |
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
