# ADR 0006: Spec Currency and ADR Triggers

Date: 2026-06-20

Context

`docs/specs/errors.md` and `docs/specs/numerics.md` are referenced as
normative by the IL guide and contributor documentation, but their front matter
still marked them as draft. `docs/il-guide.md` and `docs/specs/errors.md` also
carry the ADR 0005 resume-token model, while their verification metadata lagged
behind that decision.

The contributor guides used broad wording that made it sound like every change
requires an ADR. In practice, routine tests, bug fixes, examples, and
documentation updates do not need architecture records. The rule needs to be
specific enough to enforce without weakening spec-first development.

Decision

- Promote `docs/specs/errors.md` and `docs/specs/numerics.md` to active
  normative specs after re-verifying them against the implementation.
- Treat the IL guide, error spec, and numeric spec front matter as currency
  metadata: update `last-verified` when the document is checked against the
  referenced implementation or ADR.
- Require an ADR for changes that alter one of these architecture contracts:
  IL opcodes, IL grammar, IL verifier legality rules, cross-layer dependencies,
  or runtime C ABI surface.
- Do not require an ADR for routine implementation, test, example, or
  non-normative documentation edits that preserve those contracts.
- Add a CTest guard that compares the trap-kind table in `docs/specs/errors.md`
  against `src/vm/Trap.hpp` and fails if any normative `docs/specs/*.md` file
  remains draft.

Consequences

Pros:

- The public spec metadata now reflects documents that are already treated as
  normative by the compiler and docs.
- Trap-kind drift becomes test-visible instead of relying on manual review.
- The ADR rule remains strict for architectural contracts while staying usable
  for ordinary maintenance.

Cons:

- Spec-frontmatter updates now carry process meaning and need deliberate review.
- Future new `docs/specs/*.md` files must start active only when they are truly
  normative, or use a different location until promoted.

Alternatives

- Keep broad "all changes need ADR" wording. This preserves maximum caution but
  does not match current practice and is too noisy to enforce.
- Leave the specs as draft. This avoids a metadata change but contradicts the IL
  guide, contributor guide, and runtime behaviour.

Spec Impact

No IL grammar, opcode, verifier, or runtime ABI semantics change in this ADR.
The error and numeric specs are promoted to active status with content corrected
to match the current implementation.

Migration Plan

1. Promote `docs/specs/errors.md` and `docs/specs/numerics.md` to active.
2. Update `docs/il-guide.md` verification metadata after confirming its EH
   contract matches ADR 0005.
3. Add the trap-kind/spec-status consistency CTest.
4. Update contributor-facing ADR trigger wording and the ADR index.
