# Contributing to Robot Harness

## Licensing status

Except for third-party material with its own notices, Robot Harness is licensed
under [MIT](LICENSE-MIT) OR [Apache-2.0](LICENSE-APACHE), at your option; see the
[project declaration](LICENSE).

By intentionally submitting a contribution for inclusion in this project, you
agree to license it under the same `MIT OR Apache-2.0` terms, without additional
terms. You retain your copyright; no copyright assignment or separate CLA is
required. You must have the right to provide the contribution under these terms.
Identify third-party material and preserve its original notices; discuss material
with different terms before inclusion. The existing
[Gazebo-derived sources](integrations/ros2/simulation/THIRD_PARTY_NOTICES.md)
retain their BSD terms.

## Where to start

Use the [documentation entry](docs/README.md) to run an example or locate the
relevant contract before proposing a change. Start with the path that fits:

| I want to… | Start here |
|---|---|
| Report a build, runtime or documentation problem | [Bug report](https://github.com/xiao-yang25/robot-harness/issues/new?template=bug_report.md) |
| Discuss a robot/backend integration or design change | [Proposal or integration question](https://github.com/xiao-yang25/robot-harness/issues/new?template=proposal.md) |
| Prepare a patch | [Build and test](#build-and-test), then [pull requests and review](#pull-requests-and-review) |

For a bug, include the revision, environment, minimal reproduction, expected
behavior and actual result. For a documentation issue, identify the page and the
step that was unclear. For an integration proposal, describe the native interface
and how completion, stopping and resource cleanup can be observed. Discuss
significant API or architecture changes before implementing them.

Keep discussion respectful and focused on the technical issue. The project has
no response-time commitment. Focused tests, clearer examples and documentation
are useful small patches.

## Adoption stages

The current experimental simulation results and documentation can support
technical discussion and feedback. A developer preview needs explicit licensing
and contribution terms, a pinned version, reproducible installation/tutorials,
known limitations and a feedback route. Licensing and contribution terms are now
specified above; a versioned preview and actual external reproduction remain
pending. This guide does not announce a release or expand support.

Preview preparation can proceed alongside the next embodiment/backend integration.
Invite initial reproduction attempts when the preview is ready; use their actual
feedback to improve onboarding. Broader outreach should follow delivery of a
second meaningfully different combination and resolution of blocking issues found
by external users. Neither internal CI nor maintainer reproduction is external
adoption evidence.

Use in a deployed system needs validation for that particular combination and
its native protections, failure/recovery behavior, compatibility and maintenance.
Simulation, research use and production guarantees remain distinct. Improve
examples, documentation, releases and contributor support with each increment;
a full platform matrix or mature ecosystem is not a prerequisite for an early
experimental preview.

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
not need a robot or an AI account. Core CI supplies Ubuntu Debug and ASan/UBSan
checks. The separate Humble job
compiles optional navigation examples and checks observation predicates; it does
not run Gazebo. ROS integration changes also need the applicable native evidence
from [ROS testing](docs/TESTING.md#optional-sequential-nav2-settlement). Record
which checks you ran locally and which remain pending.

For documentation-only changes, check relative links and headings, keep commands
consistent with the actual build, and execute new or changed runnable examples.
No particular maintainer-local documentation checker is required. Keep the
distinction between implemented behavior and planned features explicit.
For published guides or site tooling, run the [documentation build and checks](docs/WEBSITE.md#build-and-preview).

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
