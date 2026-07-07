# 04 — Engine: Swept CCD, Convex Hull Reduction, Kinematic Mesh Colliders, Query Capacity

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E.
> Shipped: E13 swept time-of-impact CCD — allocation-free `world3d_ccd_sweep_sphere_raw`
> (reuses the proven march+bisection sweep, static/kinematic targets, layer-mask + trigger
> aware) clips CCD bodies' substep translation at first impact with restitution-honoring
> reflection; `get_CcdToiCount` telemetry. Anti-tunneling proven by
> `tests/runtime/test_physics3d_ccd_toi.zia`: 3 speeds × 3 thicknesses (300 m/s vs 0.05 m
> included) all stopped, non-CCD control demonstrably tunnels, bouncy path reflects.
> E14 from-scratch quickhull (`rt_quickhull3d.c`: conflict-list construction, horizon
> stitching, outward-winding invariant) + farthest-point reduction;
> `Collider3D.NewConvexHullReduced(mesh, maxVerts)` re-hulls the reduced set and
> materializes an owned hull mesh with faces (raycastable). Property tests
> (`test_quickhull3d.cpp`): cube+noise → exactly 8 verts/12 faces, containment ≤1e-7,
> degenerate rejection, extreme preservation. `NewConvexHull` semantics unchanged (support
> over cloud == over hull). E15 kinematic triangle-mesh colliders: motion-mode guard
> relaxed to STATIC|KINEMATIC (local-space narrow phase transforms by body pose — no BVH
> refit needed for rigid motion); elevator test carries a resting crate. E16
> `Physics3DWorld.SetMaxQueryHits` (16–4096, world-owned scratch, TotalCount/Truncated
> contract verified at 16 and 64). Physics suites 14/14 green; runtime completeness +
> surface audit green (baseline bumps: contract files 804, stubs 889 — SoundSource3D from
> doc 02). Docs: physics3d.md (CCD guarantee, collider constructors, query capacity).
> Heightfields stay static-only (documented; no consumer).
> Eliminates constraints #3 (substep-only CCD, untested anti-tunneling), #4 (`NewConvexHull`
> uses the raw vertex cloud), #5 (mesh/heightfield colliders static-only), #6 (fixed 256-hit
> query cap). Consumers: 14-weapons-physics (grenades, rockets), 17-bosses (SHRIKE rockets,
> WARDEN charge), 21-levels-act3 (Foundry crushers, Spire elevators).

## 0. TL;DR

Make fast-moving physics trustworthy: **swept time-of-impact CCD** for `UseCcd` bodies with an
anti-tunneling regression suite; **real quickhull** with vertex budget for convex colliders;
**kinematic triangle-mesh colliders** with BVH refit (moving platforms/crushers/elevators made
of modeled geometry); **configurable query-hit capacity**. All from scratch (zero deps).

## 1. Current state (verified anchors)

- CCD today: global substep subdivision sized by the max per-body demand, with clamp
  diagnostics (`rt_physics3d_world.inc:198-254`); `set_UseCcd` per body (runtime.def ~14558);
  substep telemetry exposed. It is **integration refinement, not TOI** — a 90 m/s rivet vs a
  0.1 m wall can still tunnel at unlucky dt, and nothing tests it (only flag/config tests,
  `test_rt_game3d.cpp:4867,4883`).
- Solver: warm-started sequential impulse, islands + sleeping, `SolverIterations` 6 (1–64),
  `PositionIterations`, `ContactBeta`, `RestitutionThreshold` (physics3d.md:42-50); validated
  to hundreds of bodies.
- `Collider3D.NewConvexHull(mesh)` stores **all mesh vertices** as the GJK support cloud —
  no hull computation, no reduction (`rt_collider3d.c:666-669`); GJK/EPA narrow phase
  (physics3d.md:106).
- Triangle mesh: lazy BVH, **static-only** (dynamic forced static, `rt_collider3d.c:20,643,672`);
  heightfield same.
- Queries: `Raycast/RaycastAll/SweepSphere/SweepCapsule/OverlapSphere/OverlapAABB`
  (`rt_physics3d_query.c:432+,595-641`); results cap `PH3D_MAX_QUERY_HITS = 256` with
  `TotalCount`/`Truncated` reporting.
- Collision events with Enter/Stay/Exit + per-contact data are solid
  (`rt_physics3d_events.inc:107-170,235`).

## 2. Design

### E13 — Swept-TOI CCD
- Scope: **dynamic body vs static world** (mesh BVH, heightfield, static convex/primitive) —
  the tunneling class that matters for projectiles/players. Dynamic-vs-dynamic stays on the
  existing substep path (documented).
- Method: speculative sweep. For each `UseCcd` body whose |v|·dt > 0.5·(min collider extent):
  sweep its bounding sphere (fallback: capsule for capsules) along the frame displacement
  against the static BVH (reuse the existing sweep machinery in `rt_physics3d_query.c` —
  `SweepSphere` already marches the BVH). On hit at fraction t: advance body to t, zero the
  normal component of remaining motion (or reflect × restitution for bouncy bodies like Arc
  grenades), inject a contact into the solver at the TOI point so the event pipeline fires a
  normal Enter event with `NormalImpulse`.
- Iteration: up to 3 TOI advances per body per step (corner cases), then clamp — telemetry
  counter `get_CcdToiCount` alongside existing substep counters.
- Determinism: sweep order = body index order (stable); identical across VM/native.

### E14 — Quickhull + reduction
- `Collider3D.NewConvexHullReduced(mesh, maxVerts)`: from-scratch 3D quickhull
  (initial tetra from extreme points → face conflict lists → horizon expansion), then vertex
  reduction while hull vertex count > maxVerts: iteratively remove the vertex whose removal
  minimizes volume error (re-hull local patch). Default maxVerts 32, clamp 8–255.
- Also route existing `NewConvexHull` through quickhull (no reduction) — eliminates the
  "support cloud includes interior vertices" cost silently for all callers; behavior identical
  (support function over hull == over cloud), perf strictly better.
- Store precomputed face normals for fast support hints; keep GJK/EPA unchanged.

### E15 — Kinematic mesh colliders
- Allow `set_Kinematic(true)` bodies to carry `NewMesh` colliders: BVH stored in local space;
  world queries/narrow-phase transform rays/supports into local space by the body transform
  (no refit needed for rigid motion — **transform, don't rebuild**). The static-only guard at
  `rt_collider3d.c:643` relaxes to "static or kinematic"; dynamic (mass-driven) mesh bodies
  remain rejected (trap message updated).
- Contacts vs kinematic mesh reuse the static mesh narrow-phase with the body's velocity fed
  into relative-velocity terms (platforms carry riders correctly via existing friction).
- Heightfield stays static (documented; no consumer).

### E16 — Query capacity
- `Physics3DWorld.SetMaxQueryHits(i64)` (16–4096, default 256): reallocates the world's query
  scratch buffers; lists keep `TotalCount`/`Truncated` semantics. Per-world, set at init.

## 3. New API surface (runtime.def)

```text
Viper.Graphics3D.Collider3D.NewConvexHullReduced(obj,i64)  obj(obj,i64)
Viper.Graphics3D.Physics3DWorld.SetMaxQueryHits(i64)       void(obj,i64)
Viper.Graphics3D.Physics3DWorld.get_CcdToiCount            i64(obj)
```
(E13/E15 are behavioral upgrades under existing APIs — `set_UseCcd`, `set_Kinematic` + mesh
colliders — no new surface beyond telemetry.)

## 4. Files

| File | Change |
|---|---|
| `src/runtime/graphics/3d/physics/rt_physics3d_world.inc` | TOI pass before integration; telemetry |
| `src/runtime/graphics/3d/physics/rt_physics3d_query.c` | sweep reuse hooks; SetMaxQueryHits buffers |
| `src/runtime/graphics/3d/physics/rt_physics3d_contacts.inc` | TOI contact injection; kinematic-mesh relative velocity |
| `src/runtime/graphics/3d/physics/rt_physics3d_raycast.c` | local-space BVH transform path for kinematic mesh |
| `src/runtime/graphics/3d/physics/rt_collider3d.c` | quickhull module call, reduced ctor, kinematic-mesh guard relax |
| `src/runtime/graphics/3d/physics/rt_quickhull3d.c/.h` (new) | quickhull + reduction (pure, unit-testable) |
| `src/il/runtime/runtime.def`, `docs/viperlib/graphics/physics3d.md` | surface + docs (CCD semantics section) |
| `src/tests/unit/test_physics3d_ccd.cpp` (new), `test_quickhull3d.cpp` (new), extend `test_physics3d_queries.zia` | tests |

New `rt_*.c/.h` files bump `source_health_audit` `runtime_api_contract_files` → update
`scripts/source_health_baseline.tsv`.

## 5. Tests (fail-before/pass-after — the CCD suite is the headline)

1. **Anti-tunneling matrix**: sphere r=0.05 at speeds {30,90,300} m/s vs walls {0.05,0.2,1.0} m
   thick at dt {1/30,1/60,1/240} → 27 cases; with `UseCcd` **zero pass-throughs** (baseline
   run documents which cases tunnel today → fail-before evidence). Bouncy variant reflects.
2. TOI event: fast body reports exactly one Enter event at the wall with sane `NormalImpulse`
   and hit position within 1 cm of the surface.
3. Determinism: 500-step CCD scene checksum identical across two runs and VM vs native.
4. Quickhull: cube-with-interior-points mesh (1,000 verts) → hull has exactly 8 verts;
   random point clouds → all input points inside hull (ε); degenerate coplanar input → 2D hull
   handled (no trap); reduction to 16 verts keeps volume within 5 % of full hull on the
   Stanford-bunny-style test mesh (procedural).
5. Kinematic mesh: platform mesh moving 2 m/s carries a resting box (friction), crusher pushes
   a sphere without tunneling (combined with CCD), raycast against moving mesh hits at the
   transformed location.
6. `SetMaxQueryHits(1024)`: 600-body overlap returns 600 with `Truncated == false`;
   default 256 still truncates with flag.
7. Perf guard: 257-body resting-pile soak (existing scenario) within 10 % of baseline step
   time with CCD pass enabled but no fast bodies (early-out works).

## 6. Verification gate

`ctest -R 'physics3d|quickhull'` + `-L graphics3d` green → determinism checksum probe VM==native
→ runtime completeness/surface audits → full no-skip build incl. `-L slow`. Docs updated with
the CCD contract (what is and isn't swept). Consumers unblocked: 14-weapons-physics switches
Arc/rocket bodies to `UseCcd` with no game-side sweep workarounds; 22-world-systems uses
kinematic mesh crushers/elevators directly from GLB collision meshes (reduced hulls for
dynamic props via `NewConvexHullReduced`).
