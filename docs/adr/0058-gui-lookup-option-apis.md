---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0058: GUI Lookup Option APIs

Date: 2026-07-02
Status: Accepted

## Context

Several preview GUI helpers expose absence through Boolean, NULL, or structured
sentinel records:

- `FindBar.FindNext()` / `FindPrev()` return `0` for no match.
- `TestHarness.FindById()` / `FindByName()` / `FindByType()` return a Map with
  `found=false`.
- `CommandRegistry.Find()` returns `null` when no command matches.

All three patterns work, but they make absence handling inconsistent across the
GUI namespace.

## Decision

Add Option-returning lookup variants:

- `FindBar.FindNextOption()` / `FindPrevOption()` -> `Option[Integer]`
- `TestHarness.FindByIdOption()` / `FindByNameOption()` / `FindByTypeOption()`
  -> `Option[Map]`
- `CommandRegistry.FindOption()` -> `Option[Command]`

The existing APIs remain available for compatibility. Runtime API metadata marks
them as legacy and points callers to the Option variants.

## Consequences

- New GUI code can use one absence shape across search bars, harness lookups, and
  command registries.
- Existing preview GUI callers keep their current behavior.
- Agent-facing API dumps now advertise the migration path instead of relying on
  naming heuristics.
