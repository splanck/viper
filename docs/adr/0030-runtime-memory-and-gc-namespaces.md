---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0030: Runtime Memory and GC Namespaces

## Status

Accepted

## Context

`Viper.Memory` currently mixes two different concepts:

- manual retain/release hooks that are sharp, low-level ownership tools.
- cycle-collector diagnostics and tuning under `Viper.Memory.GC`.

The runtime overhaul plan calls for sharp lifetime tools to live under an
explicit unsafe namespace and runtime tuning controls to live under
`Viper.Runtime`. Existing source and IL must continue to work.

## Decision

Add compatibility-preserving aliases:

- `Viper.Runtime.Unsafe.Retain`
- `Viper.Runtime.Unsafe.Release`
- `Viper.Runtime.Unsafe.RetainStr`
- `Viper.Runtime.Unsafe.ReleaseStr`
- `Viper.Runtime.GC.Collect`
- `Viper.Runtime.GC.TrackedCount`
- `Viper.Runtime.GC.TotalCollected`
- `Viper.Runtime.GC.PassCount`
- `Viper.Runtime.GC.SetThreshold`
- `Viper.Runtime.GC.GetThreshold`

The existing `Viper.Memory.*` and `Viper.Memory.GC.*` functions remain
registered and keep their behavior.

## Consequences

Documentation and new examples should prefer `Viper.Runtime.Unsafe` for manual
reference-count manipulation and `Viper.Runtime.GC` for cycle-collector
diagnostics. The older `Viper.Memory` namespaces are compatibility surfaces, not
the preferred public shape for new code.
