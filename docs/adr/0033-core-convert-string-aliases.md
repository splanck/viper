---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0033: Core Convert String Aliases

## Status

Accepted

## Context

`Zanna.Core.Convert` exposed integer and double formatting as
`ToString_Int` and `ToString_Double`. The underscore form is inconsistent with
the rest of the public runtime API and with the overhaul rule that public leaves
should use PascalCase without separators.

The existing names appear in generated IL and older source, so removing them
would be unnecessarily disruptive.

## Decision

Add canonical aliases:

- `Zanna.Core.Convert.ToStringInt(i64) -> str`
- `Zanna.Core.Convert.ToStringDouble(f64) -> str`

Keep the existing compatibility aliases:

- `Zanna.Core.Convert.ToString_Int`
- `Zanna.Core.Convert.ToString_Double`

Both name sets lower to the same runtime C implementations and return owned
runtime strings.

## Consequences

- New docs and examples can avoid underscore-style public method names.
- Existing source, golden IL, and generated output remain valid.
- Runtime API dumps expose both names for now.
- Future API audits should prefer `ToStringInt` and `ToStringDouble` except in
  compatibility-specific fixtures.
