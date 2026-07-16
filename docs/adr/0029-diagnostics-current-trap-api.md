---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0029: Diagnostics Current Trap API

## Status

Accepted

## Context

The runtime exposes low-level `Viper.Error.*` hooks for throw-message storage and
trap fields. Those functions are useful to generated code and compatibility
tests, but they make mutable runtime internals look like ordinary application
APIs. The runtime overhaul plan calls for a modern, read-only diagnostics
surface that preserves the existing hooks while giving applications a clear
public contract.

## Decision

Add `Viper.Diagnostics.CurrentTrap() -> Option<TrapInfo>`.

`CurrentTrap()` returns `None` when the current thread has no recorded trap
metadata. Otherwise it returns a `Viper.Diagnostics.TrapInfo` snapshot with
read-only properties:

- `Kind`
- `Code`
- `Ip`
- `Line`
- `KindName`
- `Message`
- `Location`

The existing `Viper.Error.SetThrowMsg`, `ClearThrowMsg`, `SetTrapFields`,
`RaiseKind`, and field getters remain registered for compatibility and generated
code paths. Documentation should prefer `CurrentTrap()` for application-facing
diagnostics.

## Consequences

Tools and applications can inspect trap metadata without mutating global trap
state or calling a cluster of individual getters. The `Option` return shape also
matches the runtime API overhaul policy for expected absence.
