# ADR 0091: Per-Collider Physics Materials, Surface Tags, Body User Data

Date: 2026-07-11

## Status

Accepted

## Context

Friction/restitution lived only on bodies (no ice patch without a second
body), nothing tagged what a surface *is* (footsteps/VFX/decals had no key),
and physics hits could not resolve back to gameplay objects without
game-side registries.

## Decision

- **Collider materials:** `Collider3D` gains `Friction`/`Restitution`
  overrides (-1/unset = the body's value applies) and a `SurfaceType` tag.
  The contact solver resolves per-side effective values through the
  contact's leaf collider (compound children included) via internal
  `rt_collider3d_effective_*_raw` helpers; the pair combine rules are
  unchanged (geometric-mean friction, min restitution).
- **Body user data:** `Physics3DBody.UserData` — an opaque i64 the runtime
  never reads; the clean hit→gameplay-object hop for raw-physics users.
- **Surface exposure:** `PhysicsHit3D.SurfaceType` and
  `CollisionEvent3D.SurfaceTypeA/B` read the leaf collider's tag.
- **`Game3D.Surfaces`:** a process-global, mutex-guarded name↔id registry
  (idempotent `Register`, stable ids from 1, 255 budget with a trap,
  `NameOf`/`IdOf`/`Count`). Ids are session-scoped; data files persist names.

## Consequences

- Plan 23 (footsteps) keys its tables on these ids; streamed-tile manifest
  `material`→tag plumbing and `Character3D.GetGroundSurfaceType` ride the
  follow-up alongside it.
- The material-struct change surfaced another test-mirror desync
  (`RTParticles3DContractTests` StubMaterial) — synced; the mirror trap list
  grows.
- Test: `g3d_test_game3d_surfaces_probe` (registry idempotence/round-trip,
  tagged-floor raycast surface readback, user-data round-trip); the 760-test
  physics suite is regression-green with the solver change.
