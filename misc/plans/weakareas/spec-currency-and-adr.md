# Spec Currency & ADR Cadence

**Status:** Completed — normative spec metadata, ADR cadence, and drift checks are now
current.
**Area:** `docs/specs/`, `docs/il-guide.md`, `docs/adr/`, `CLAUDE.md`, `AGENTS.md`
**Effort:** S
**Roadmap fit:** v0.2.x hardening

## Problem

Three concrete issues:
1. `docs/specs/errors.md` and `docs/specs/numerics.md` are **`status: draft`** (verified)
   yet are normative for trap kinds and deterministic numerics.
2. `docs/il-guide.md` and `docs/specs/errors.md` now describe `resumetok` as a
   handler-provenance capability and link/align with ADR 0005, but the front-matter
   `last-verified` dates still lag the June 2026 ADR. The metadata no longer tells a
   reliable currency story.
3. The ADR set is small (5 ADRs) relative to CLAUDE.md's broad rule ("Changes require
   ADR, never silent divergence"); the rule and practice have diverged.

## Goal & scope

- **In:** Promote the two specs to `active` after a re-verify; update verification
  metadata for IL/EH docs; right-size the ADR trigger in CLAUDE.md/AGENTS.md so it is
  enforceable; optionally backfill ADRs for genuinely architectural recent changes.
- **Out:** Rewriting the error/numeric models; mass retroactive ADRs for routine work.

## Implementation steps

1. **Re-verify `errors.md`:** cross-check its trap-kind list against the runtime trap
   enum and the verifier; fix drift; flip to `status: active` with today's `last-verified`.
2. **Re-verify `numerics.md`:** cross-check deterministic-numeric rules against the VM +
   verifier (overflow opcodes, checked casts); fix drift; flip to `active`.
3. **ADR 0006** (or amendment): record the error/numeric spec promotion and the doc
   currency process, not a semantic rewrite, unless the re-verify finds drift.
4. **Update IL/EH verification metadata** after confirming `il-guide.md#reference` and
   `errors.md` match ADR 0005 and `src/vm/Trap.hpp`.
5. **Right-size the ADR rule in CLAUDE.md/AGENTS.md:** replace any blanket "every change"
   phrasing with a specific trigger — *IL opcode/grammar/verifier-rule changes,
   cross-layer dependencies, and runtime-ABI surface changes require an ADR* — so the
   rule is actually followable.

## Tests

- **Doc-vs-code consistency test:** add a test asserting the trap-kind list in
  `errors.md` matches the runtime trap enum (parse the doc table, compare to the enum) —
  so the spec can't silently drift again. The verifier/conformance tests already cover
  much of the numeric model; reference them from `numerics.md`.
- Grep assertion: no remaining `status: draft` on normative `docs/specs/*`.

## Documentation

- The specs themselves (status flip + content fixes), `docs/il-guide.md` metadata if
  needed, the ADR index, and `CLAUDE.md`/`AGENTS.md` ADR trigger text are all updated by
  this plan.
- One release-notes line ("IL error/numeric specs promoted to normative; EH spec synced
  with ADR 0005").

## Cross-platform

N/A (documentation/process), though the numerics spec underpins the cross-platform
determinism guarantee — the re-verify should confirm VM/native agreement claims still hold.

## Implementation notes

- `docs/specs/errors.md` and `docs/specs/numerics.md` are promoted to `status: active`
  with `last-verified: 2026-06-20`.
- `docs/il-guide.md` verification metadata is refreshed after confirming its EH model
  remains aligned with ADR 0005.
- `docs/adr/0006-spec-currency-and-adr-triggers.md` records the spec promotion and ADR
  trigger policy.
- `CLAUDE.md` and `AGENTS.md` now state the enforceable ADR trigger instead of a blanket
  rule.
- `test_tools_trap_kind_spec_consistency` checks doc/code trap-kind drift and rejects
  draft normative specs.

## Verification

- `ctest --test-dir build -R '^test_tools_trap_kind_spec_consistency$' --output-on-failure`
- `rg -n '^status: draft' docs/specs` should return no matches.

## Risks / open questions

- **Spec governance:** EH-section edits touch the normative `#reference`; route through the
  ADR so the change is traceable.
- **Drift recurrence:** the doc-vs-code consistency test is what prevents this from
  re-rotting; prioritize it over a one-time manual sync.
