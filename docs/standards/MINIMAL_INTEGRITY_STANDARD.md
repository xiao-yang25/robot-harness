# Minimal Integrity and Proportional Assurance Standard

> Status: **ACCEPTED — D-068，2026-08-31**
> Scope: all future documents、designs、tooling and experiment executions
> Principle: every check must protect a distinct failure mode；digest proves byte integrity，direct
> observation and focused tests prove correctness

## 1. Decision

Robot Harness adopts a minimal integrity model。New work must not add a digest merely because an
artifact has a field that could be hashed。A digest is justified only when bytes cross a trust
boundary、must be compared with an already accepted immutable object，or must be checked after
transfer/storage。

This standard supersedes the checksum、per-file digest、repeated independent rehash and large
assertion-registry mechanics in D-060–D-067 for **new work and new executions**。It does not rewrite
or invalidate already sealed evidence；historical bundles remain verifiable under the contract that
created them。Numeric values、state semantics、runtime predicates、sentinel thresholds、barrier
rules and authorization boundaries are unchanged。

## 2. Integrity Budget：exception boundaries，not a checklist

The following are the only places where a new SHA-256 may be justified。They are a ceiling，not a
required set。The default is **no project-computed digest** unless bytes actually cross an external
storage/transfer boundary and corruption would otherwise be undetectable：

| Boundary | Default treatment | When a digest is justified |
|---|---|---|
| Container image identity | Read the immutable digest already supplied by the container runtime；do not recompute image bytes | Only when importing an image through an untrusted transport without a verified runtime identity |
| Accepted numeric profile | Compare the accepted profile ID、version and consumed numeric fields directly | Only when the profile bytes are transferred independently of their authority store |
| Runtime policy | Compare policy ID、version and predicates directly | Only when policy bytes cross an independent trust boundary |
| Source | Use reviewed read-only source plus actual executable/cmdline/process observations | Only for a release artifact crossing a trust boundary；never as a routine per-attempt snapshot |
| Final evidence archive | One SHA-256 produced by the packager and checked once by the receiver | Default and sufficient transfer-integrity check |

For the current applicability qualification，the selected budget is **one digest total：the final
evidence archive**。Docker's existing `sha256:...` image identity is compared as a supplied runtime
value and is not recomputed by project tooling。Profile and policy use stable IDs plus direct field
validation。No source snapshot、source byte count or source aggregate digest is produced。

If a boundary is not crossed，or byte identity is not needed，do not hash it。Within one workflow the same immutable object must not be rehashed by each
producer、auditor、sealer and verifier merely to restate the same fact。

MD5 is prohibited：it adds another mechanism without providing a useful boundary in this project。
Short IDs may be derived for display，but never grant authority and need not be independently
verified。

## 3. Requirements that remain strict without hashes

The following continue to be validated directly from raw facts and schemas：

- boot ID、clocksource、clock continuity and suspend detection；
- actual image、pull policy、mounts、tmpfs run root and process lifecycle；
- observer/control/treatment CPU affinity and role placement；
- PRE/POST ordering、cardinality and run/attempt identity；
- the per-boot sentinel、lateness threshold and registered derivation；
- `APPLICABLE / MISMATCH / UNRESOLVED` state and reason mapping；
- closed barrier、empty pre-release treatment ledger and release ordering；
- archive path safety、required-file presence、parseability and the fields needed for adjudication。

A digest match must never substitute for these checks。Conversely，a missing redundant per-file
digest must not make otherwise complete raw evidence inconclusive。

## 4. Disallowed patterns for new work

New documents、designs and tooling must not require：

- per-sample、per-row or per-raw-evidence-file hashes；
- routine source snapshots、source byte counts or executable/entrypoint digest chains for each
  attempt；
- the same immutable input to be hashed repeatedly in builder/auditor/sealer/transfer stages；
- both internal checksum manifests and an external archive checksum for the same transfer purpose；
- bundle IDs、directory names or acceptance status derived solely from additional digest layers；
- large fixed assertion registries whose only behavior is to repeat one aggregate PASS；
- a new acceptance manifest or lock solely to bind another acceptance manifest or lock。

Independent review remains required where it catches a different class of error，but independence
must come from separate parsing、predicate recomputation、negative tests or observation—not from
rehashing identical bytes。

## 5. Proportional verification model

The same rule applies to non-hash checks：repeating a check is justified only when the second
consumer observes a different trust boundary or can catch a different implementation failure。

### 5.1 One owner per responsibility

The default execution chain is：

```text
collector records raw facts
  → evaluator validates schemas/predicates and derives state/reasons
  → one independent decision check covers safety-critical disposition
  → packager creates the final archive
  → receiver verifies archive integrity and path safety
```

The packager does not re-adjudicate semantics。The receiver does not rerun the complete evaluator。
It verifies transfer integrity，loads the recorded decision and performs only the small set of
safety-critical admission checks needed by the consumer。

### 5.2 Schema strictness

- External/untrusted inputs require parseability、required fields、types、enums and safety
  invariants。
- Unknown metadata fields are ignored unless they collide with a reserved semantic namespace。
- Internal intermediate files do not need distinct closed schemas when they are never consumed
  outside the same tool boundary。
- Exact cardinality/order is required only where it changes semantics，such as the configured
  sentinel samples、clock triplet selection and PRE-before-POST lifecycle。JSON key order and harmless
  metadata order are not adjudication conditions。

### 5.3 Reviews、assertions and tests

- Use a concise result with state、reason codes and the failed predicate。Do not require a large
  fixed assertion registry that merely mirrors one aggregate result。
- One independent implementation is required only for the final safety-critical disposition，not
  for collectors、packagers and transfer code independently reimplementing the whole pipeline。
- Test semantic branches and representative boundary classes。Do not require mutation of every
  leaf、every field permutation or every equivalent malformed encoding after the parser behavior is
  already covered。
- Counts such as `19/19` or `22/22` are diagnostic summaries，not authority and not an acceptance
  goal。

### 5.4 Authority and packaging

- Maintain one current authority reference per concern。Do not create chains of review lock →
  contract lock → implementation lock → acceptance lock when a decision ID、versioned artifact and
  one retained digest already identify the authority。
- Only the final archive needs immutable/read-only treatment。Staging directories and intermediate
  files may remain writable until successful packaging；failed attempts are retained under a new
  attempt identity rather than repaired in place。
- A per-run admission barrier release does not wait for remote archive transfer。It requires the local evaluator PASS、
  the one independent safety decision PASS、current barrier CLOSED and treatment ledger empty。
  Packaging/transfer then records the run。If final evidence packaging fails，the run is
  `INVALID_EVIDENCE` and cannot contribute to Gate A，but the failure does not retroactively turn a
  correctly fenced local admission into a safety violation。
- Reusable authority inputs are referenced，not copied into every attempt bundle。

### 5.5 Failure severity

- A direct runtime-policy violation rejects the current attempt。
- Missing、conflicting or unparsable evidence yields `UNRESOLVED`。
- A whole qualification is invalidated only when a boot-level stable fact is directly observed to
  have changed，not because one attempt has a bad path、PID、optional metadata field or incomplete
  file。
- Cosmetic naming、directory-ID mismatch、file mode on non-final staging data and unknown optional
  metadata are not standalone reasons to reject otherwise valid evidence。

## 6. Required design discipline

Any future document or design that introduces a hash must include a short **Integrity Budget** that
states：

1. the exact trust boundary；
2. the single object being protected；
3. who computes it and who verifies it；
4. why schema validation、an existing aggregate digest or the final archive digest is insufficient；
5. how repeated computation is prevented。

If this justification is absent，the hash requirement is non-normative and must be removed during
review。An exception outside the listed boundary categories requires an explicit new decision with a measured
failure mode；“more rigorous” by itself is not sufficient justification。

## 7. Applicability migration

- Preserve D-060–D-067 documents、locks、vectors and sealed evidence as historical records。
- Existing tooling may retain legacy digest fields for backward-readable evidence，but future
  execution acceptance must not depend on redundant legacy digest repetitions、fixed assertion
  counts、multi-lock chains or repeated full-pipeline revalidation。
- Per-boot qualification is a regression preflight，not a second calibration。Replace the historical
  10-minute / 60,000-sample sentinel with exactly 3,000 consecutive 10 ms samples（30 seconds）。
  The accepted 50 ms maximum-lateness threshold is unchanged；any exceedance still fails closed。
- Qualification clock evidence uses three PRE and three POST triplets；admission uses three current
  triplets。The minimum-width selection and suspend/discontinuity comparison are unchanged。Separate
  four-point continuity wrappers are unnecessary when host PRE/POST、clock PRE/POST and sentinel
  first/last timestamps provide the same ordering facts。
- The D-068 qualification path now uses only the final archive digest，3,000-sample sentinel、3+3
  clock evidence and the direct runtime predicates in section 3。Its focused local suite passes；a
  real attempt remains a separate execution action。
- The prepared remote directory
  `qualification-d067-20260831a` (located through the research workspace record)
  is classified `PREPARATION_SUPERSEDED`。It contains no formal container、sentinel or
  qualification result and must not be promoted into evidence。
- No calibration rerun is required。D-061 numeric values remain accepted；Gate A、schedules and
  B0/B1/H remain separately unauthorized。
