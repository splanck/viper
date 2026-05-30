# 3D Next Level 2 progress tracker

This directory tracks implementation progress for the 3D scale-tier plan. The
plan files (`../README.md`, `../review.md`, `../roadmap.md`,
`../runtime-changes.md`, `../api-spec.md`, `../carryover.md`) describe the
target; these files record delivery state, tests, docs, waivers, and proof
links.

## Tracker files

| File | Purpose |
|---|---|
| `00-master.md` | Global gates, phase status, acceptance criteria |
| `01-phase-progress.md` | Phase-by-phase task and exit-criteria checklist (incl. Phase C carryover) |
| `02-decisions.md` | Phase-0 cross-cutting decisions to close before broad work |
| `03-runtime-contracts.md` | Runtime contract additions from `runtime-changes.md` |
| `04-api-surface.md` | Public surface from `api-spec.md` |
| `05-tests-docs-samples.md` | Required ctests, docs pages, fixtures, vertical slice |
| `06-waivers.md` | Explicitly accepted gaps + re-waivers carried from `3Dnextlevel` |

## Status legend

| Status | Meaning |
|---|---|
| `todo` | Not started |
| `in-progress` | Active implementation or review |
| `partial` | A shippable slice landed, but the row's full scope still has remaining work |
| `blocked` | Cannot proceed without a decision, dependency, or external fix |
| `done` | Implemented, tested, documented where required, linked to proof |
| `waived` | Explicitly accepted gap with a linked reason and re-open condition |

## Update rules

- Every implementation PR updates the tracker rows it touches.
- `done` requires implementation proof plus matching ctest/docs proof.
- Missing automation is `waived`, never silently omitted.
- Keep stable IDs unchanged; add rows if scope grows.
- If an API changes, update both `../api-spec.md` and `04-api-surface.md`; if a
  runtime contract changes, update both `../runtime-changes.md` and
  `03-runtime-contracts.md`. New public classes must also update the class-id
  ABI row and append, not renumber, `RT_G3D_*_CLASS_ID` sentinels.
- Every scale item's `done` must link a before/after perf number (Core Principle:
  measure scale work).

## Row template

```markdown
| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| AREA-001 | Short item name | `file.md` section | todo |  |  |  |  |  |
```
