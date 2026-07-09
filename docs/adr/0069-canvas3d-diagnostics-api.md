# ADR 0069: Canvas3D Diagnostics API

Date: 2026-07-09
Status: Accepted

## Context

Graphics3D already exposes several Canvas3D counters for backend behavior,
lighting truncation, and draw submission. A few important fallback paths were
still only observable through visual differences or stderr:

- GPU backend selection could fall back to software, but scripts could not read
  the reason.
- The bounded instanced fallback path could skip excess transparent or rebased
  instances without a dropped-instance counter.
- The public `PollEvent()` ring could drop events during bursts without
  telemetry.
- Deferred mesh snapshots could fail because of the per-frame byte budget or
  allocation pressure without a public counter.
- Graphics-disabled builds had no Canvas3D equivalent of the 2D
  `Canvas.IsAvailable()` guard.

These are normal runtime diagnostics rather than exceptional failures. Changing
the existing void draw/event APIs to return results would be source-breaking, so
additive read-only accessors are the least disruptive shape.

## Decision

Add the following `Viper.Graphics3D.Canvas3D` surface:

- `IsAvailable() -> Boolean`
- `BackendFallbackReason -> String`
- `InstancedFallbackDroppedCount -> Integer`
- `EventDropCount -> Integer`
- `MeshSnapshotBytes -> Integer`
- `MeshSnapshotDropCount -> Integer`
- `MeshSnapshotDroppedBytes -> Integer`
- `MeshSnapshotBudgetBytes -> Integer`

The new values are neutral in graphics-disabled builds. Existing APIs and
properties remain unchanged.

## Consequences

- Tools and games can detect degraded visuals or dropped work without parsing
  stderr or guessing from frame output.
- Existing source remains compatible because the new API is additive.
- `Canvas3D.IsAvailable()` matches the established 2D Canvas guard pattern.
