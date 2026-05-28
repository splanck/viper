# Game3D progress tracker

This directory tracks implementation progress for the 3D Next Level plan. The
plan files describe the target; these tracker files record delivery state,
tests, docs, waivers, and proof links for each item.

## Tracker files

| File | Purpose |
|---|---|
| `00-master.md` | Global gates, acceptance criteria, and release readiness |
| `01-phase-progress.md` | Phase-by-phase task and exit-criteria checklist |
| `02-decisions.md` | Phase 0 decisions that must be closed before broad implementation |
| `03-runtime-contracts.md` | Runtime contract additions from `runtime-changes.md` |
| `04-api-surface.md` | Public `Viper.Game3D` API surface from `api-spec.md` |
| `05-tests-docs-samples.md` | Required ctests, docs pages, starter, showcase, and bowling migration |

## Status legend

Use the same status words everywhere:

| Status | Meaning |
|---|---|
| `todo` | Not started |
| `in-progress` | Active implementation or review |
| `blocked` | Cannot proceed without a decision, dependency, or external fix |
| `done` | Implemented, tested, documented where required, and linked to proof |
| `waived` | Explicitly accepted gap with a linked reason and follow-up owner |

## Update rules

- Every implementation PR should update the tracker rows it touches.
- `done` requires implementation proof plus matching ctest/docs proof when the
  row requires them.
- Missing automation must be marked `waived`, never silently omitted.
- Keep stable IDs unchanged. Add new rows if the scope grows.
- Prefer links to commits, PRs, ctest names, docs paths, or issue IDs in
  `Proof / link`.
- If an API changes, update both `api-spec.md` and `04-api-surface.md`.
- If a runtime contract changes, update both `runtime-changes.md` and
  `03-runtime-contracts.md`.

## Row template

```markdown
| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| AREA-001 | Short item name | `file.md` section | todo |  |  |  |  |  |
```
