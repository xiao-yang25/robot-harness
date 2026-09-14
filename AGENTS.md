# Repository Instructions

## Integrity checks

- All new documents、designs、tooling and experiments must follow the
  [Minimal Integrity and Proportional Assurance Standard](docs/standards/MINIMAL_INTEGRITY_STANDARD.md)（D-068）。
- Do not introduce MD5。
- Default to no project-computed digest。For the current applicability path，compute SHA-256 only
  once for the final archive and verify it once after transfer。Docker's supplied image digest is an
  observed identity，not a request to rehash image bytes。
- Compare profile/policy IDs and consumed values directly。Do not create routine source snapshots、
  source byte counts or aggregate source hashes。Any future extra digest requires a concrete new
  trust boundary and an explicit Integrity Budget。
- Do not add per-sample/per-raw-file digest chains or rehash the same immutable input at multiple
  stages。
- Hashes prove byte integrity，not semantic correctness。Validate schemas、runtime facts、state
  transitions and safety predicates directly。
- Do not make multiple stages repeat the same semantic adjudication。Use one evaluator、one focused
  independent safety-decision check、one packager and one boundary transfer check。
- Closed schemas apply to required semantic fields at external boundaries；unknown optional metadata
  is not a failure。Do not freeze large assertion registries or exhaustive per-leaf mutation suites。
- Use one current authority reference per concern。Do not add acceptance/contract/implementation
  lock layers that only bind one another。
- A design that adds a hash must include an `Integrity Budget` with its boundary、object、producer、
  verifier and reason。Exceptions require an explicit decision。
- Do not rewrite sealed historical evidence to conform to a newer integrity standard。
