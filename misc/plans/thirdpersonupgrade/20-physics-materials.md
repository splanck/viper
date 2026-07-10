# Plan 20 — Per-Collider Physics Materials, Surface Tags, Body User Data

## 1. Objective & scope

Three small physics-surface additions with outsized gameplay reach: per-collider friction/restitution overrides (ice patch on a stone floor without a second body), a **surface-type tag** on colliders (the key plan 23's footsteps, impact VFX, and decal selection all index on), and an opaque **user-data slot** on bodies (clean physics-hit → gameplay-object resolution). Plus a name↔id surface registry.

**In scope:** (a) `Collider3D` friction/restitution/surfaceType; (b) `Physics3DBody` userData; (c) surface exposure on hits and contact events; (d) `Game3D.Surfaces` registry; (e) terrain/streamed-tile surface plumbing (manifest `material` → surface tag).
**Out of scope:** friction combine-mode options (geometric mean fixed, documented), per-triangle mesh-collider materials (per-collider granularity v1; compound colliders give multi-material bodies).

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **Zero user-data/surface hits in `physics/*.h`** (verified grep). Friction/restitution live on the *body*: `rt_body3d_set_friction/restitution` (`rt_physics3d.h:297-303`).
- **Colliders are reusable shape objects** — "Collider3D is a reusable shape object; Physics3DBody owns one active collider at a time" (`rt_collider3d.h:12-13`); compound colliders own children with local transforms (`:51-56,94-100`). Per-collider material must respect sharing (a collider used by two bodies shares its material — that's the feature, same as sharing the shape).
- **Contact pipeline:** narrowphase produces contacts consumed by the solver (`rt_physics3d_solver.c`) using body friction/restitution; events expose colliders per side (`rt_collision_event3d_get_collider_a/b`, `rt_physics3d.h:213-216`) and hits expose the specific collider (`rt_physics_hit3d_get_collider`, `:184`) — so *which collider* is already known everywhere; only the material lookup is missing.
- **Manifest metadata already names surfaces:** cells/tiles carry `material` strings surfaced via `getCellMaterial/getTerrainTileMaterial` (`game3d.md` §WorldStream3D) — currently inspection-only; this plan connects them to generated heightfield colliders.
- **Registry precedent:** small process-global name registries exist (`Viper.Input.Action`, mix groups `Audio.RegisterGroup`).

## 3. Design

### 3.1 Collider material fields

`Collider3D` gains `friction` (−1 = unset), `restitution` (−1 = unset), `surface_type` (i64, 0 = untyped). Effective contact values: per-side value = collider override if set, else body value; pair combine unchanged (existing solver combine rules — geometric mean for friction, max for restitution; confirm exact current combine at write time and preserve it). Compound children resolve to the *child* collider's material (narrowphase already tracks the child for events).

### 3.2 Body user data

`rt_body3d_set_user_data(body, i64)` / getter. Opaque to the runtime. Documented pattern: store a game-side handle/id; `Entity3D.attachBody` does **not** claim it (the entity↔body registry already exists; user data is for raw-physics users and gameplay tags like "breakable").

### 3.3 Surface exposure

- `PhysicsHit3D.get_SurfaceType` (from the hit collider, compound-child resolved).
- `CollisionEvent3D.get_SurfaceTypeA/B`.
- Heightfield convenience: `rt_collider3d_sample_heightfield_raw` callers (character ground checks) — expose `Character3D.GetGroundSurfaceType()` reading the ground body's collider tag (plan 03's retained ground body makes this one lookup).
- Streamed tiles: `mountTiledTerrain` maps the manifest `material` string through the registry (`Surfaces.Register`) onto the generated heightfield collider's `surface_type`; cells' `material` applies to colliders the cell spawns where applicable (root-entity bodies).

### 3.4 `Game3D.Surfaces` registry

Process-global, static class (leaf `Surfaces`): `Register(name) -> i64` (idempotent, stable ids from 1, ≤ 255 with trap past that), `NameOf(id) -> str` ("" unknown), `IdOf(name) -> i64` (0 unknown), `Count`. Ids are session-scoped; persistence stores names (plan 23's tables key by id at runtime, by name in data files).

## 4. Implementation steps

1. Collider fields + setters/getters + effective-value resolution in the solver contact path; C unit tests (ice-override box slides farther than body-default box).
2. Body user data + tests.
3. Hit/event surface exposure + compound-child resolution test.
4. `Surfaces` registry (+ thread-safe init per the established `PTHREAD_MUTEX_INITIALIZER`/`InitOnceExecuteOnce` pattern).
5. Terrain/tile manifest plumbing + `Character3D.GetGroundSurfaceType` (needs plan 03's ground body; ship after it or behind a body-parameter overload).
6. runtime.def + audits + ADR + docs (`physics3d.md` §Collider3D/§Physics3DBody updates, `game3d.md` registry section).
7. Zia probe `g3d_test_game3d_surfaces_probe` (registry + tagged floor + raycast surface readback).

## 5. Public API changes (runtime.def)

```
Collider3D: RT_PROP("Friction","f64",…) RT_PROP("Restitution","f64",…) RT_PROP("SurfaceType","i64",…)
Physics3DBody: RT_PROP("UserData","i64",…)
PhysicsHit3D: RT_PROP("SurfaceType","i64",get)
CollisionEvent3D: RT_PROP("SurfaceTypeA","i64",get) RT_PROP("SurfaceTypeB","i64",get)
Character3D: RT_METHOD("GetGroundSurfaceType","i64(obj)",…)
RT_CLASS_BEGIN("Viper.Game3D.Surfaces", Game3DSurfaces, "none", none)   /* static class */
    RT_METHOD("Register","i64(str)",…) RT_METHOD("NameOf","str(i64)",…)
    RT_METHOD("IdOf","i64(str)",…)     RT_PROP("Count","i64",get)
RT_CLASS_END()
```

Static-only class uses the `"none"` instance-type convention (runtime-completeness rule). Leaf `Surfaces` unique. ADR `00xx-physics-surface-materials.md`.

## 6. Tests

- **Override (C unit):** identical boxes, one with collider friction 0.02 override, launched equally on a flat floor — override box travels ≥ 3× farther (fail-before: no per-collider value).
- **Restitution override:** bouncy-collider sphere rebounds higher than body-default twin.
- **Compound:** compound body with rubber + ice children — contact on each child reports that child's surface type and uses its friction (event assert).
- **Unset fallback:** collider with unset values behaves byte-identically to today (solver replay of an existing physics test).
- **Registry:** Register idempotent; 256th distinct name traps; NameOf/IdOf round-trip; concurrent first-Register safe (TSAN lane).
- **Terrain:** manifest `"material":"grass"` tile ⇒ ground raycast reports `Surfaces.IdOf("grass")`; character ground surface matches.

## 7. Verification gates

Full build + ctest; **existing physics suites must stay bit-identical** (unset defaults preserve all behavior — the headline gate); TSAN lane for the registry; determinism gate; `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **Combine-rule preservation:** read the solver's exact current friction/restitution combination before landing and encode it in a pinned test — the override must slot into the per-side value, never change the combine math.
- **Collider sharing semantics:** material-on-shape means two bodies sharing a collider share its material — intended and documented; games wanting per-body materials clone the collider (cheap).
- **Registry ids are session-scoped** — data files must store names; plan 23 enforces this in its table loaders.
- 255-surface cap is deliberate (fits future packed contact fields); trap early rather than silently truncate.
