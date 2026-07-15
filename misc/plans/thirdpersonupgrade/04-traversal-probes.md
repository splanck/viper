# Plan 04 — Traversal Probes: Ledge / Vault / Clearance Queries

## 1. Objective & scope

Give climb/mantle/vault gameplay a first-class query API instead of the raycast forests games hand-roll. Three composed shape-cast probes on `Physics3DWorld` return structured results (grab point, surface normal, standing room) that pair with existing root-motion animation to build traversal.

**In scope:** (a) `ProbeLedge`, `ProbeVault`, `ProbeClearance` on `Physics3DWorld`; (b) result class `Viper.Graphics3D.LedgeHit3D`; (c) doc recipes tying probes to root-motion mantling.
**Out of scope:** any traversal state machine or animation authoring (game-side); ledge *shimmy* path building (future).

**Zero external dependencies — absolute.** Pure composition of existing sweeps/raycasts.

## 2. Current state (verified anchors)

- **Building blocks exist:** `rt_world3d_sweep_capsule(world, a, b, radius, delta, mask)` and `rt_world3d_sweep_sphere` (`rt_physics3d.h:171-174`), `rt_world3d_raycast` (`:165`); hits carry point/normal/fraction/started-penetrating (`:180-196`).
- **No probe/ledge API exists** anywhere in `physics/` (grep: no ledge/vault/mantle hits).
- **Character capsule state** is queryable (`rt_collider3d_get_radius_raw/get_height_raw`, `rt_collider3d.h:86-90`) so Game3D sugar can default probe dimensions from the bound character.
- **Root motion already works end-to-end:** `AnimController3D.SetRootMotionBone` + `SyncMode.NodeFromAnimRootMotion` consume per-frame deltas deterministically (`game3d.md` §Animator3D) — a mantle clip with root motion moves the entity; the probe supplies the target validation.
- **Result-object precedent:** `PhysicsHit3D` is a GC-managed plain result handle (`rt_physics3d.h:26` ownership note); `LedgeHit3D` follows the same shape.
- **Class IDs:** allocate next free from `rt_graphics3d_ids.h` at write time (after plans 01/02: `-0x60304D`).

## 3. Design

### 3.1 `ProbeLedge`

`ProbeLedge(origin, forward, radius, maxHeight, maxDepth, mask) -> LedgeHit3D | null`

1. **Wall sweep:** capsule (character-sized, caller-provided radius) swept `forward × maxDepth` from `origin`. No hit ⇒ null (nothing to grab).
2. **Top sweep:** from a point above the wall contact (`wallPoint + up × maxHeight + forward × (radius + skin)`), sweep a sphere straight **down** `maxHeight`. First hit = candidate ledge top; its normal must be walkable-ish (`normal.y ≥ 0.6`) else null (overhang).
3. **Standing room:** clearance test at the ledge top (§3.3) with the caller's capsule dims; result recorded in `hasStandingRoom` (a hang-only ledge is still a valid result — games decide).
4. Result: `grabPoint` (ledge edge point: wall contact XZ at top-hit height), `surfaceNormal` (top hit), `wallNormal` (wall hit), `height` (top-hit Y − origin Y), `hasStandingRoom`.

### 3.2 `ProbeVault`

`ProbeVault(origin, forward, radius, maxHeight, maxThickness, mask) -> LedgeHit3D | null`
Steps 1–2 as above, then a **far-side** down-sweep at `wallPoint + forward × (maxThickness + radius)`: requires ground within `maxHeight + drop tolerance` on the far side (vault needs a landing). Result reuses `LedgeHit3D` with `landingPoint` populated (origin-level far-side ground point) — one result class, two null-able vector fields.

### 3.3 `ProbeClearance`

`ProbeClearance(position, radius, height, mask) -> i1` — capsule overlap test at an explicit pose; internally the same temporarily-placed narrowphase test pattern the character controller uses (`character3d_test_position`, `rt_physics3d_character.c`), but expressed through a reusable scratch collider (`rt_collider3d_reset_*_raw` reuse pattern) rather than the controller body. Also the §3.1 step-3 primitive.

### 3.4 Game3D sugar

`CharacterController3D.probeLedge(maxHeight)` / `probeVault(maxHeight, maxThickness)` — default origin/forward/radius from the character pose and facing; forwards to the world probes. Keeps common gameplay one-call.

## 4. Implementation steps

1. `LedgeHit3D` result class (fields + getters, GC-managed like `PhysicsHit3D`) + class ID.
2. `ProbeClearance` (scratch-collider overlap) + C unit tests (clear, blocked-by-ceiling, blocked-by-wall).
3. `ProbeLedge` composition + unit tests (ledge found on 1 m wall; overhang rejected; too-tall rejected; `hasStandingRoom` false under low ceiling).
4. `ProbeVault` far-side landing logic + tests (thin wall vaultable; thick wall not; missing far ground not).
5. Game3D sugar on `CharacterController3D`.
6. runtime.def + audits + ADR + docs (`physics3d.md` new §Traversal Probes with a root-motion mantle recipe).
7. Zia probe `g3d_test_game3d_traversal_probe` (deterministic: walk to wall, probe, teleport-mantle, verify final position).

## 5. Public API changes (runtime.def)

```
RT_FUNC(G3dWorldProbeLedge,  rt_world3d_probe_ledge,  "Viper.Graphics3D.PhysicsWorld3D.ProbeLedge",
        "obj(obj,obj<Viper.Math.Vec3>,obj<Viper.Math.Vec3>,f64,f64,f64,i64)")
RT_FUNC(G3dWorldProbeVault,  rt_world3d_probe_vault,  "Viper.Graphics3D.PhysicsWorld3D.ProbeVault",  "obj(...)" )
RT_FUNC(G3dWorldProbeClear,  rt_world3d_probe_clearance, "Viper.Graphics3D.PhysicsWorld3D.ProbeClearance", "i1(obj,obj<Viper.Math.Vec3>,f64,f64,i64)")
RT_CLASS_BEGIN("Viper.Graphics3D.LedgeHit3D", G3dLedgeHit3D, "obj", none)
    RT_PROP("GrabPoint","obj<Viper.Math.Vec3>", get) RT_PROP("SurfaceNormal","obj<Viper.Math.Vec3>", get)
    RT_PROP("WallNormal","obj<Viper.Math.Vec3>", get) RT_PROP("Height","f64", get)
    RT_PROP("HasStandingRoom","i1", get)            RT_PROP("LandingPoint","obj<Viper.Math.Vec3>", get /* null for ledge probes */)
RT_CLASS_END()
```

Plus `CharacterController3D` `probeLedge/probeVault` methods. Leaf `LedgeHit3D` unique. ADR `00xx-physics3d-traversal-probes.md`.

## 6. Tests

- **Ledge (C unit):** Given a 1.0 m box wall — When `ProbeLedge(origin, +Z, 0.3, 2.0, 1.0, All)` — Then non-null, `height ≈ 1.0 ± cell`, `surfaceNormal.y > 0.9`, `hasStandingRoom` true (fail-before: no API).
- **Overhang:** tilted top face (normal.y 0.3) ⇒ null.
- **Vault:** 1 m tall × 0.4 m thick wall ⇒ non-null with far-side `landingPoint`; 3 m thick ⇒ null.
- **Clearance:** capsule pose inside a 1.2 m gap with 1.8 m height ⇒ false; 1.0 m height ⇒ true.
- **Mask:** wall on excluded layer ⇒ null.
- **Zia recipe probe:** probe + root-motion-style teleport lands the character on the ledge top, grounded true, deterministic replay ×2 identical.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits after def change; `-L slow` before phase close. No shader/backend work; no determinism-gate rerun needed beyond standard suites (probes are pure queries).

## 8. Risks & constraints

- **Scratch-collider reuse** must be per-world (not global) for thread cleanliness; follow the internal query-reuse pattern noted in `rt_collider3d.h` ("internal query reuse").
- **Tuning constants** (walkable-normal 0.6, skin epsilon) are documented in the header and tested by ordering asserts, not exact geometry, to keep goldens stable.
- **Probe cost:** 2–3 sweeps per call; intended for event-driven use (on jump press near a wall), not per-frame polling — say so in docs.
- Result vectors are world-space at probe time; callers must re-probe after movement (results are not live handles).
