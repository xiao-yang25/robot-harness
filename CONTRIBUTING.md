# Contributing to Robot Harness

## Licensing status

Licensing and code contribution terms are not yet finalized. This guide records
the development workflow; it does not grant a license or establish contribution
terms. Bug reports and design discussion can proceed, but confirm contribution
terms with the maintainer before submitting code or documentation patches. See
the [current licensing status](README.md#license).

## Where to start

Robot Harness is at an early development stage. See the [README](README.md) for
implemented behavior and the [design](docs/DESIGN.md) for scope and boundaries.
Start with a reproducible bug report, a documentation issue, or a concrete
integration question. Once contribution terms are settled, focused tests, clearer
examples and documentation are useful small patches.

Use [issues](https://github.com/xiao-yang25/robot-harness/issues) to report a bug
or discuss a proposal; the repository provides bug and proposal templates.
For a bug, include the revision, environment, reproduction
steps, expected behavior and actual result. Discuss significant API or architecture
changes before investing in an implementation. Keep discussions respectful and
focused on the technical issue.

## Build and test

Follow the [build instructions](README.md#build),
[example guide](examples/README.md), [testing guide](docs/TESTING.md), and
[C++ style guide](docs/CODING_STYLE.md).
These repository documents provide the contributor-facing instructions;
maintainer-specific local tools are not a prerequisite for participation.

Keep changes focused, preserve existing behavior, and add tests for meaningful
behavior changes. For a reproducible fix, demonstrate the failure before the fix
and the expected behavior afterward where practical. Report checks you could not
run. Build outputs and local environment details do not belong in the patch.

Run the normal example before changing behavior so you can compare observations.
For code changes, build with `BUILD_TESTING=ON` and run CTest from the build
directory. Linux-only recovery changes need Linux validation; the documented
Docker environment is sufficient for the current software-process checks. You do
not need a robot or an AI account. CI supplies Ubuntu Debug and ASan/UBSan checks;
record which checks you ran locally and which are still pending.

For documentation-only changes, check relative links and headings, keep commands
consistent with the actual build, and execute new or changed runnable examples.
No particular maintainer-local documentation checker is required. Keep the
distinction between implemented behavior and planned features explicit.

## Pull requests and review

Create a descriptive branch and open a pull request against `master`, using the
[PR template](.github/pull_request_template.md). Explain the problem, changed
behavior, validation and remaining limitations. Update affected documentation.
Ubuntu CI runs all registered tests normally and with ASan/UBSan.

Use a fork if you do not have repository write access. In your local clone of the
fork, a focused change can start with `git switch -c docs/clarify-example` (choose
a name matching the change). There is no required personal branch prefix or
commit-signing workflow. Use English commit messages and avoid unrelated cleanup.

Behavior changes involving authority, security/authorization, core public APIs,
ownership/lifecycle, concurrency/cancellation, persistent state/recovery,
data integrity or protocols require independent
review by someone other than the implementer.
Maintainers coordinate this review and record its scope and findings; contributors
can submit a PR with review pending. Address blocking findings and rerun affected
checks after changes. See [PR review and evidence](docs/TESTING.md#pr-review-and-evidence)
for how results are recorded.

AI-assisted contributions follow the same requirements: understand and check the
submitted changes, and report only tests and reviews that actually occurred.
Generated code or an AI review does not replace the required CI results.

## Before requesting review

- Describe the concrete problem and resulting behavior, with reproduction steps
  for a fix. Link the relevant issue when one exists; an issue is not required
  for every small clarification.
- Include meaningful tests for changed behavior, and update the affected usage,
  design or verification documentation.
- State the tested revision, actual results, remaining gaps and any required
  independent review still pending. Contributors need not arrange AI reviewers;
  the maintainer coordinates review and decides whether to merge.
- Check the diff for generated files, credentials and personal environment data.
  Share only the minimal relevant, redacted output when reporting a failure.

The project is experimental: caller interfaces may change, private headers are
not integration contracts, and no response-time or release-date commitment is
made. See [interfaces and compatibility](README.md#interfaces-and-compatibility).
