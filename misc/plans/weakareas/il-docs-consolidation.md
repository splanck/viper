# IL Documentation Consolidation

**Status:** Completed — IL documentation authority is explicit, spec governance is tied
to ADR 0006, and drift is guarded by CTest.
**Area:** `docs/il-guide.md`, `docs/il-quickstart.md`, `docs/il-reference.md`
**Effort:** S
**Roadmap fit:** v0.2.x hardening / contributor onboarding

## Problem

Three IL documents overlap:

- `docs/il-guide.md` is the normative IL v0.3.0 guide; `#reference` is authoritative.
- `docs/il-quickstart.md` is an intro tour.
- `docs/il-reference.md` is an instruction catalog with examples.

The hierarchy is implied but not enforced. A new contributor can still read basic-block
structure three times and miss which file controls semantics when copies disagree.

## ⚠️ Governance constraint

`docs/il-guide.md` is **spec-authoritative**, and CLAUDE.md states: *"Never modify
`/docs/il-guide.md#reference` without an ADR."* Therefore consolidation that touches the
normative reference content **requires an ADR** (coordinate with `spec-currency-and-adr.md`,
which already proposes ADR 0006). Structural/editorial merging that preserves the
normative text still warrants an ADR note for traceability.

## Goal & scope

- **In:** Add an explicit authority map, reduce semantic duplication, and add drift/link
  checks. Keep `il-guide.md#reference` as the single normative target.
- **Out:** Moving or rewriting normative reference text without an ADR; deleting useful
  quickstart/reference material just to reduce file count.

## Implementation steps

1. Add a short authority banner to all three docs:
   - `il-guide.md#reference` = normative semantics.
   - `il-reference.md` = catalog/explanatory examples, must link back to the guide for
     semantics.
   - `il-quickstart.md` = tutorial, must not define semantics independently.
2. Diff the three docs to inventory duplicated semantic claims and replace duplicates
   outside `il-guide.md#reference` with links or clearly non-normative summaries.
3. Draft an ADR only if moving/merging normative `#reference` text or changing the
   authority structure. A simple banner/link cleanup can be done as docs maintenance.
4. Fix inbound links: grep `docs/`, `examples/`, source comments, and `README`s for
   references to stale anchors and update them.
5. Add/extend a docs link/anchor check and a grep-based drift check for duplicated
   "normative" claims outside the guide.

## Tests

- Link/anchor integrity: run (or add to) a docs link-check over `docs/` and assert no
  broken intra-doc anchors after the merge. If `scripts/check_bible_consistency.sh` (or a
  similar checker) can be extended to validate IL-doc anchors, do so.
- Grep assertion: no doc outside `il-guide.md` claims to be the normative IL reference
  except as a link back to `il-guide.md#reference`.

## Documentation

- Update `docs/README.md` navigation and any contributor onboarding path to point at the
  guide as the single IL authority, with quickstart/reference as supporting docs.
- Note the consolidation (and the ADR) in the release notes as one line.
- Align with the contributor-onboarding-roadmap idea (single "read this for IL" entry).

## Cross-platform

N/A (documentation).

## Implementation notes

- `docs/README.md`, `docs/il-guide.md`, `docs/il-quickstart.md`, and
  `docs/il-reference.md` now make the IL guide the authoritative entry point while
  keeping quickstart/reference material supporting and non-conflicting.
- `docs/adr/0006-spec-currency-and-adr-triggers.md` records the spec currency and ADR
  trigger decision.
- `docs/specs/errors.md` and `docs/specs/numerics.md` are active normative specs with
  refreshed verification metadata.
- `test_tools_trap_kind_spec_consistency` checks the documented trap-kind table against
  the runtime enum and fails on draft normative specs.

## Verification

- `ctest --test-dir build -R '^test_tools_trap_kind_spec_consistency$' --output-on-failure`
- `rg -n '^status: draft' docs/specs` should return no matches.

## Risks / open questions

- **Spec governance:** avoid moving normative `#reference` text unless an ADR explicitly
  records the no-semantic-change move.
- **External bookmarks:** preserve old anchors (or provide redirects) so existing links
  don't rot.
