# ADR 0092: Surface-Driven Footsteps (SurfaceTable3D + Footsteps3D)

Date: 2026-07-11

## Status

Accepted

## Context

Footstep audio that matches the ground is the immersion detail whose absence
reads as unfinished. All the primitives existed — animator events, ground
raycasts, plan-20 surface tags, positional audio — but every game re-wired
them by hand.

## Decision

- **`SurfaceTable3D`**: one shared data object mapping `Game3D.Surfaces` ids
  to clip-variant sets (up to 8 per row, retained) plus a per-surface
  loudness scale (the plan-22 hearing stimulus input). Row 0 is the untyped
  fallback; an empty table is a silent no-op.
- **`Footsteps3D.New(entity, table)`**: installs on the entity's component
  slot and ticks in the world step right after animation (before scene
  sync). It consumes this frame's animator events whose names start with the
  configurable prefix (default `footstep`), rate-limits at 0.12 s, raycasts
  1.5 m down (`SetGroundMask`), resolves the surface row through the hit
  collider's tag, and plays a clip variant positioned at the entity through
  the world's audio system. Variant selection uses a per-component LCG with
  a fixed bind seed — replays are deterministic, verified by the probe.
  `StepCount`/`LastSurface` expose telemetry for tests and tools.

## Consequences

- Deferred (recorded): per-foot bone positions (`setFeetBones`), auto-detect
  fallback for un-authored rigs, VFX/decal dispatch rows, and the
  `reportSound` hearing hook — these bolt onto the same fire path once
  plan-22's perceiver exists.
- Test: `g3d_test_game3d_footsteps_probe` — a looping walk state with two
  authored `footstep_*` events fires at the authored rate, resolves the
  tagged floor's surface id, and step counts match across identical runs.
