# Minimal Integrity and Proportional Assurance Standard

This is the product application of accepted research decision **D-068
(2026-08-31)**. It preserves the applicable constraints without importing a
particular host, boot, qualification run, or historical authorization state into
product setup. The original research standard and sealed evidence remain in the
research workspace. This document does not supersede their numeric values,
runtime predicates, state semantics, or authorization boundaries.

## Integrity boundaries

Default to **no project-computed digest** for local development. Do not introduce
MD5. A digest establishes byte integrity, not semantic correctness.

| Object or boundary | Required treatment |
|---|---|
| Container image identity | Read the digest supplied by the container runtime; do not rehash image bytes |
| Accepted profile or runtime policy | Compare stable IDs, versions, and consumed numeric values or predicates directly |
| Local source and execution | Use reviewed source and relevant runtime observations; no routine source snapshots, byte counts, or aggregate source hashes |
| Final evidence archive crossing a transfer boundary | One SHA-256 computed by the packager and verified once by the receiver |

These boundaries are a ceiling, not a checklist. Do not add a digest where no
byte-identity trust boundary requires it. Do not add per-sample/per-raw-file digest
chains, an internal checksum manifest duplicating the final archive check, or
repeated hashes of the same immutable input in several stages.

Any proposed additional digest must have an explicit **Integrity Budget**:
the concrete trust boundary, object, producer, verifier, reason existing direct
checks or the final archive digest are insufficient, and how repeated computation
is prevented. An exception requires an explicit decision grounded in a new
failure mode; "more rigorous" is not sufficient. Merely having a digest field
does not make it required.

## Semantic correctness

Validate schemas, consumed values, runtime facts, state transitions, and safety
predicates directly. Relevant lifecycle, clock continuity, placement, ordering,
and admission facts retain their checks when an experiment actually depends on
them. A matching digest cannot substitute for those facts.

At external boundaries, validate required semantic fields, types, enums, and
invariants. Unknown optional metadata is not an error unless it collides with a
reserved semantic namespace. Do not give internal intermediates separate closed
schemas merely because they are files. Require exact cardinality or ordering only
when it changes the decision.

Missing, conflicting, or unparseable evidence remains unresolved. A direct
runtime-policy violation rejects the affected attempt. A whole qualification is
invalidated only by an observed change to its boot-level stable facts; cosmetic
names, writable staging files, and unrelated optional metadata are not such facts.

## Proportional checks and evidence

For an experiment that produces a safety-critical decision and transfers evidence,
use one collector, one evaluator, one focused independent safety-decision check,
one packager, and one transfer-boundary check. Each has a distinct responsibility:

- The collector records raw facts; the evaluator validates and derives the result.
- The independent decision check covers the safety-critical disposition through
  independent parsing, predicate recomputation, or observation, not rehashing.
- The packager does not re-adjudicate the result. The receiver checks transfer
  integrity and path safety, loads the decision, and performs only the focused
  safety-critical admission checks needed by its boundary.

This experiment chain does not require every local build/test to produce an
archive. It also does not replace code review required by the applicable shared
engineering guide: decision checking and code review cover different objects.

Test semantic branches and representative boundaries. Avoid large fixed assertion
registries, exhaustive per-leaf mutation suites, and repeated full evaluators.
Counts summarize diagnostics; they are not an acceptance target. Keep one current
authority reference per concern rather than chains of acceptance/contract locks.

Only a final archive needs immutable/read-only treatment. Staging may remain
writable until packaging succeeds. Retain failed attempts under distinct attempt
identities instead of repairing sealed evidence in place. Reference reusable
authority inputs rather than copying them into every evidence bundle.

Where the research admission protocol applies, local release depends on the
evaluator, focused independent decision check, closed barrier, and empty treatment
ledger; it does not wait for remote archive transfer. A packaging failure excludes
the run as invalid evidence but does not retroactively turn a correctly fenced
admission into a safety violation. Product effect-domain release continues to use
the settlement requirements in [Design](../DESIGN.md), not archive delivery.

## Historical evidence and applicability

Do not rewrite sealed D-060–D-067 evidence or reinterpret it as new product
validation. Historical compatibility fields may remain readable, but redundant
digest repetitions, fixed assertion counts, and lock chains must not become
requirements for new work.

The original D-068 qualification's selected budget remains one final-archive
digest. Its accepted 3,000-sample sentinel, 50 ms threshold, clock evidence,
runtime predicates, and attempt-specific authority remain in the research
standard; they are not universal product defaults. A new qualification must use
that applicable source rather than reconstructing an execution procedure from
this product summary. Its absence is a gap for that experiment, not for an
ordinary Core build.
