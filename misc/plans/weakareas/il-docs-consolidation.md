# IL Documentation Consolidation

**Status:** Verified real (three overlapping IL docs with unclear authority)
**Area:** `docs/il-guide.md`, `docs/il-quickstart.md`, `docs/il-reference.md`
**Effort:** S
**Roadmap fit:** v0.2.x hardening / contributor onboarding

## Problem

Three IL documents overlap with an unclear hierarchy:
- `docs/il-guide.md` (the normative spec; claims to consolidate the others)
- `docs/il-quickstart.md` (intro tour — overlaps the guide's opening)
- `docs/il-reference.md` (instruction catalog — overlaps the guide's reference section)

A new contributor reads basic-block structure three times and can't tell which doc is
authoritative.

## ⚠️ Governance constraint

`docs/il-guide.md` is **spec-authoritative**, and CLAUDE.md states: *"Never modify
`/docs/il-guide.md#reference` without an ADR."* Therefore consolidation that touches the
normative reference content **requires an ADR** (coordinate with `spec-currency-and-adr.md`,
which already proposes ADR 0006). Structural/editorial merging that preserves the
normative text still warrants an ADR note for traceability.

## Goal & scope

- **In:** Make `il-guide.md` the single authority; fold the quickstart in as an
  introductory chapter and the reference in as an appendix; replace the two standalone
  files with short redirect stubs. Preserve all unique content and stable anchors.
- **Out:** Rewriting IL semantics; changing the spec itself (that path is ADR-governed).

## Implementation steps

1. **Diff the three docs** to inventory unique vs duplicated content (don't lose
   anything — quickstart examples and reference edge-notes must survive).
2. Draft an **ADR** recording the consolidation decision and confirming no normative
   change to `#reference` semantics (just structure/location).
3. Merge: quickstart → "Chapter 1: A first IL program"; reference → "Appendix A:
   Instruction catalog"; dedupe the basic-block/structure material to one place.
4. Replace `il-quickstart.md` and `il-reference.md` with redirect stubs (one paragraph +
   link to the canonical section) — or archive with a tombstone header.
5. **Fix inbound links:** grep `docs/`, `examples/`, source comments, and `README`s for
   references to the old files/anchors and update them.

## Tests

- Link/anchor integrity: run (or add to) a docs link-check over `docs/` and assert no
  broken intra-doc anchors after the merge. If `scripts/check_bible_consistency.sh` (or a
  similar checker) can be extended to validate IL-doc anchors, do so.
- Grep assertion: zero remaining references to removed anchors outside the redirect stubs.

## Documentation

- Update `docs/README.md` navigation and any contributor onboarding path to point at the
  single IL authority.
- Note the consolidation (and the ADR) in the release notes as one line.
- Align with the contributor-onboarding-roadmap idea (single "read this for IL" entry).

## Cross-platform

N/A (documentation).

## Risks / open questions

- **Spec governance:** keep the normative `#reference` text byte-stable through the move so
  the ADR can certify "no semantic change"; do editorial cleanup as a clearly-separate,
  reviewed step.
- **External bookmarks:** preserve old anchors (or provide redirects) so existing links
  don't rot.
