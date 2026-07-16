---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR-0045: Boxed Value-Type Unsafe API

## Status

Accepted

## Context

`Viper.Core.Box.ValueType` and `ValueTypeAddField` are runtime/compiler hooks
used when inline value-type payloads are copied into heap storage. They are
powerful and necessary, but ordinary user-facing `Box` helpers otherwise deal
with safe primitive boxing and unboxing.

Keeping value-type allocation and managed-field registration next to
`Box.I64`, `Box.Str`, and `Box.ToI64Option` makes them look like everyday
application APIs.

## Decision

Expose canonical unsafe names:

- `Viper.Runtime.Unsafe.ValueType(size)`
- `Viper.Runtime.Unsafe.ValueTypeAddField(obj, offset, kind, retainNow)`

The existing `Viper.Core.Box.ValueType`,
`Viper.Core.Box.ValueTypeAddField`, and `Viper.Core.ValueType.AddField` rows
remain available for compatibility and carry migration targets in the runtime
API dump.

## Consequences

- Ordinary `Viper.Core.Box` docs can focus on primitive boxing, safe
  comparisons, and `Option` unboxing helpers.
- Compiler/runtime interop keeps the full value-type payload feature.
- Audits can classify direct boxed value-type construction as unsafe unless it
  appears in compiler, runtime, or explicitly low-level examples.
