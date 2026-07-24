---
status: active
audience: contributors
last-verified: 2026-07-20
---

# ADR 0143: Generate Runtime Class Inheritance Metadata

## Status

Accepted (2026-07-20)

## Context

Frontend runtime-method resolution needs to inherit methods from runtime base
classes. The shared resolver previously encoded every concrete GUI Widget class
in a hand-maintained array. That list could drift whenever the generated runtime
catalog gained or renamed a control, and the catalog itself could not express
the relationship.

Copying Widget methods into every class would inflate generated data and make
override and diagnostic behavior ambiguous. Inferring inheritance from names,
constructors, or the opaque layout string would be equally fragile.

## Decision

`RT_CLASS_BEGIN` accepts an optional fifth `base_name` argument. `rtgen`
preserves that fully-qualified name in `RuntimeClasses.inc`, and
`il::runtime::RuntimeClass` exposes it as immutable `baseQName` metadata.
Four-argument class definitions remain source-compatible and produce an empty
base name.

The frontend-neutral runtime method resolver walks this generated base chain
for method lookup and diagnostics. GUI controls explicitly declare
`Zanna.GUI.Widget` as their base; no frontend owns a parallel subclass list.
Generation validates that each non-empty base names a declared runtime class and
that base chains contain no cycles.

## Consequences

- Runtime class inheritance has one declarative source of truth.
- New Widget-derived controls inherit methods without frontend edits.
- The change affects generated compiler metadata only. It does not modify the
  runtime C ABI, IL grammar, verifier rules, or serialized IL.
- `rtgen` and runtime-catalog tests cover unknown bases, cycles, and inherited
  method lookup.
