---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0030: Runtime Memory and GC Namespaces

## Status

Accepted

## Context

`Zanna.Memory` currently mixes two different concepts:

- manual retain/release hooks that are sharp, low-level ownership tools.
- cycle-collector diagnostics and tuning under `Zanna.Memory.GC`.

The runtime overhaul plan calls for sharp lifetime tools to live under an
explicit unsafe namespace and runtime tuning controls to live under
`Zanna.Runtime`. Existing source and IL must continue to work.

## Decision

Add compatibility-preserving aliases:

- `Zanna.Runtime.Unsafe.Retain`
- `Zanna.Runtime.Unsafe.Release`
- `Zanna.Runtime.Unsafe.RetainStr`
- `Zanna.Runtime.Unsafe.ReleaseStr`
- `Zanna.Runtime.GC.Collect`
- `Zanna.Runtime.GC.TrackedCount`
- `Zanna.Runtime.GC.TotalCollected`
- `Zanna.Runtime.GC.PassCount`
- `Zanna.Runtime.GC.SetThreshold`
- `Zanna.Runtime.GC.GetThreshold`

The existing `Zanna.Memory.*` and `Zanna.Memory.GC.*` functions remain
registered and keep their behavior.

## Consequences

Documentation and new examples should prefer `Zanna.Runtime.Unsafe` for manual
reference-count manipulation and `Zanna.Runtime.GC` for cycle-collector
diagnostics. The older `Zanna.Memory` namespaces are compatibility surfaces, not
the preferred public shape for new code.
