---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0036: Format And Frame Abbreviation Aliases

## Status

Accepted

## Context

The runtime overhaul naming policy expands ambiguous abbreviations in public
runtime leaves. Two remaining high-visibility areas used terse names:

- `Zanna.Text.Fmt.NumSci`, `NumPct`, and `BoolYN`
- `Zanna.Graphics.Canvas.SetDTMax` and `Zanna.Graphics3D.Canvas3D.SetDTMax`

The old names are compact but require users to already know the abbreviation.
Removing them would break existing examples and source.

## Decision

Add canonical aliases:

- `Zanna.Text.Fmt.Scientific(f64, i64) -> str`
- `Zanna.Text.Fmt.Percent(f64, i64) -> str`
- `Zanna.Text.Fmt.YesNo(i1) -> str`
- `Zanna.Graphics.Canvas.SetMaxDeltaTime(i64)`
- `Zanna.Graphics3D.Canvas3D.SetMaxDeltaTime(i64)`

Keep compatibility aliases:

- `NumSci`
- `NumPct`
- `BoolYN`
- `SetDTMax`

All aliases lower to the same runtime C implementations.

## Consequences

- Formatting and game-loop docs can use names that explain themselves.
- Existing source, examples, and IL remain compatible.
- Runtime API dumps expose both names for now.
- API audits should prefer the canonical names outside compatibility-specific
  coverage.
