---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0032: Math Bits Full-Name Aliases

## Status

Accepted

## Context

`Viper.Math.Bits` exposed several terse helper names:

- `LeadZ`
- `TrailZ`
- `Rotl`
- `Rotr`
- `Ushr`

These names are compact, but they make the public API less readable for users
who are not already familiar with compiler or VM bit-operation mnemonics. The
runtime overhaul naming policy prefers plain English for public leaves while
preserving standard domain terms.

## Decision

Add canonical full-name aliases:

- `LeadZ` -> `CountLeadingZeros`
- `TrailZ` -> `CountTrailingZeros`
- `Rotl` -> `RotateLeft`
- `Rotr` -> `RotateRight`
- `Ushr` -> `ShiftRightLogical`

The existing names remain available for compatibility and generated IL stability.
Both name sets lower to the same runtime C implementations.

## Consequences

- New docs and examples can teach readable bit operations without sacrificing
  low-level power.
- Existing source and IL continue to work unchanged.
- Runtime API dumps expose both name sets until a future compatibility cleanup
  decides whether terse aliases should be hidden from ordinary discovery.
- API audits should prefer the full-name methods except when documenting legacy
  compatibility.
