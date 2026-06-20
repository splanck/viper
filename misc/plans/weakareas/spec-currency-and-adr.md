# Spec Currency & ADR Cadence

**Status:** Verified real (two normative specs marked `draft`; il-guide predates ADR 0005;
ADR rule and practice have drifted)
**Area:** `docs/specs/`, `docs/il-guide.md`, `docs/adr/`, `CLAUDE.md`
**Effort:** S
**Roadmap fit:** v0.2.x hardening

## Problem

Three concrete issues:
1. `docs/specs/errors.md` and `docs/specs/numerics.md` are **`status: draft`** (verified)
   yet are normative for trap kinds and deterministic numerics.
2. `docs/il-guide.md` was last verified **before ADR 0005** (resume-token provenance,
   2026-06-12) and does not reflect that `resumetok` is a handler-provenance capability,
   not an ordinary value — so hand-written IL exception handlers can violate undocumented
   verifier rules.
3. The ADR set is small (5 ADRs) relative to CLAUDE.md's broad rule ("Changes require
   ADR, never silent divergence"); the rule and practice have diverged.

## Goal & scope

- **In:** Promote the two specs to `active` after a re-verify; sync the il-guide EH
  section with ADR 0005 (ADR-governed); right-size the ADR trigger in CLAUDE.md so it's
  enforceable; optionally backfill ADRs for genuinely architectural recent changes.
- **Out:** Rewriting the error/numeric models; mass retroactive ADRs for routine work.

## Implementation steps

1. **Re-verify `errors.md`:** cross-check its trap-kind list against the runtime trap
   enum and the verifier; fix drift; flip to `status: active` with today's `last-verified`.
2. **Re-verify `numerics.md`:** cross-check deterministic-numeric rules against the VM +
   verifier (overflow opcodes, checked casts); fix drift; flip to `active`.
3. **ADR 0006** (or amendment): record the error/numeric model as canonical and link the
   now-active specs.
4. **Sync il-guide EH section** to ADR 0005 (resumetok-as-capability + the provenance
   verifier rules). Because this is the normative spec, do it as an ADR-governed change
   (coordinate with `il-docs-consolidation.md`).
5. **Right-size the ADR rule in CLAUDE.md:** replace the blanket "every change" with a
   specific trigger — *IL opcode/grammar/verifier-rule changes, cross-layer dependencies,
   and runtime-ABI surface changes require an ADR* — so the rule is actually followable.

## Tests

- **Doc-vs-code consistency test:** add a test asserting the trap-kind list in
  `errors.md` matches the runtime trap enum (parse the doc table, compare to the enum) —
  so the spec can't silently drift again. The verifier/conformance tests already cover
  much of the numeric model; reference them from `numerics.md`.
- Grep assertion: no remaining `status: draft` on normative `docs/specs/*`.

## Documentation

- The specs themselves (status flip + content fixes), `docs/il-guide.md` (EH sync), the
  ADR index, and `CLAUDE.md` (ADR trigger) are all updated by this plan.
- One release-notes line ("IL error/numeric specs promoted to normative; EH spec synced
  with ADR 0005").

## Cross-platform

N/A (documentation/process), though the numerics spec underpins the cross-platform
determinism guarantee — the re-verify should confirm VM/native agreement claims still hold.

## Risks / open questions

- **Spec governance:** EH-section edits touch the normative `#reference`; route through the
  ADR so the change is traceable.
- **Drift recurrence:** the doc-vs-code consistency test is what prevents this from
  re-rotting; prioritize it over a one-time manual sync.
