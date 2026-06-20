# BASIC IDE Language Services (Decision + Enablement)

**Status:** Verified — currently **disabled by design** (a decision, not a bug)
**Area:** `viperide/`, `src/tools/vbasic-server/`, `src/frontends/basic/`
**Effort:** M
**Roadmap fit:** v0.3.x P4 (ViperIDE)

## Problem

BASIC editing in ViperIDE is plain-text: a probe (`viperide/src/phase0_phase1_probe.zia`)
asserts `basicService.canGoToDefinition == false` and that `rename` is not executable, and
`main.zia` gates completion/diagnostics/hover on the active service's capability flags —
which are off for BASIC. So BASIC gets no completion, diagnostics, hover, or navigation
in the IDE, while Zia gets the full set.

This is a **deliberate scoping choice**, not a defect — but it leaves BASIC a
second-class editing experience. This plan frames the decision and the incremental path
to enable services if chosen.

## Current state (verified)

- ViperIDE language-service registry with per-language capability flags; BASIC flags off.
- A BASIC compiler bridge exists (`vbasic-server`, ~889 LOC of tests) but is not wired to
  the IDE's semantic features.
- The BASIC frontend has substantial sema + diagnostics infrastructure (~645 LOC of
  diagnostic code — *more* than Zia's single diagnostics file), so the analysis backend
  largely exists already.

## Decision

Recommend enabling BASIC IDE services **incrementally**, cheapest-first, because the
analysis already exists:

1. **Diagnostics** (cheapest — the frontend already produces them): surface compile
   diagnostics in the editor.
2. **Hover** (type/signature at cursor): reuse sema results.
3. **Completion** (members/keywords/identifiers).
4. **Definition / references / rename** (needs a BASIC symbol index — most work; do last).

Each step flips one capability flag and wires one bridge call. If the project prefers to
keep BASIC text-only for now, document that as an explicit decision and stop after
recording it.

## Implementation steps

1. Wire `vbasic-server`/`BasicCompilerBridge` into the IDE language-service registry,
   mirroring the Zia service wiring.
2. Enable diagnostics: flip `canDiagnose`, route frontend diagnostics → editor squiggles.
3. Enable hover: flip `canHover`, map cursor → sema type/signature.
4. Enable completion: flip `canComplete`, provide member/keyword/identifier candidates.
5. (Last) build a BASIC symbol index for definition/references/rename; flip those flags.
6. Update the probe expectations at each step (they currently assert *disabled*).

## Tests

- `src/tests/vbasic-server/`: add request/response cases for each enabled feature.
- ViperIDE probe(s): update `phase0_phase1_probe.zia` expectations as flags flip; add a
  probe asserting BASIC diagnostics now flow to the editor model.
- Regression: confirm Zia services and editor hot-path probes remain green.

## Documentation

- Update ViperIDE docs (`viperide/` README/plans) and `docs/feature-parity.md` to reflect
  BASIC IDE-service status as each flag is enabled.
- If the decision is to stay text-only, record that explicitly in the ViperIDE docs with
  the rationale (so it reads as a choice, not an omission).
- One release-notes line per shipped capability.

## Cross-platform

IDE/tooling only; no platform concerns beyond the existing IDE build.

## Risks / open questions

- **Symbol index scope:** definition/rename need a BASIC project index; gauge whether the
  existing bridge can supply ranges or whether a new index is required (the costly part).
- **Capability honesty:** never advertise a flag before its feature actually works — the
  probe-driven approach enforces this.
