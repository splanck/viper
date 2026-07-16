---
status: active
audience: contributors
last-verified: 2026-07-16
---

# ADR 0031: Core Parse Double Aliases

## Status

Superseded — the anticipated breaking cleanup (public-surface standardization)
retired the `TryNum` / `NumOr` compatibility spellings from the registry. Only the
canonical `TryDouble` / `DoubleOr` names remain; source or IL that names `TryNum` /
`NumOr` no longer resolves. See the "Consequences" note that contemplated this
retirement (VDOC-233).

## Context

The runtime overhaul naming policy prefers plain, type-specific names over
compressed abbreviations. `Viper.Core.Parse.TryNum` and
`Viper.Core.Parse.NumOr` both operate on `f64` values, but `Num` is less clear
than the rest of the parse surface:

- `TryInt` names the target integer type family.
- `TryBool` names the target boolean type.
- `IntOr` and `BoolOr` name their concrete fallback types.

Removing the old names would break existing source and generated IL, so the
overhaul must be additive.

## Decision

Add canonical aliases:

- `Viper.Core.Parse.TryDouble(str) -> Option`
- `Viper.Core.Parse.DoubleOr(str, f64) -> f64`

Keep the existing names:

- `Viper.Core.Parse.TryNum`
- `Viper.Core.Parse.NumOr`

Both name sets lower to the same runtime implementations and carry equivalent
ownership metadata. New docs and examples should prefer `TryDouble` and
`DoubleOr`; compatibility sections may mention `TryNum` and `NumOr`.

## Consequences

- Existing programs continue to compile and run.
- New users see the concrete double-parse intent immediately.
- Runtime API dumps expose both names until a future breaking cleanup can decide
  whether compatibility aliases should be hidden or retired.
- API audits should treat `TryDouble` and `DoubleOr` as canonical and flag new
  examples that teach `TryNum` or `NumOr` without a compatibility reason.
