# Runtime contract tracker

Tracks every runtime contract from `../runtime-changes.md`. All rows start `todo`.

## 0. Carryover runtime items (Phase C)

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-CARRY-001 | Render-target finalization or `BackendSupports("rt-finalize")` | §0 / CO-4 | todo |  |  |  |  | R-FRAME-016/018 |
| R-CARRY-002 | Lifetime/diagnostics (`Game3D.<Type>.<method>`) + leak probes | §0 / CO-3 | todo |  |  |  |  | R-LIFE-* |
| R-CARRY-003 | `Light3D.CastsShadows` + material texture-presence getters | §0 / CO-6 | todo |  |  |  |  | feeds Phase 7 |
| R-CARRY-004 | Implicit-lighting decision + conditional gating/count | §0 / CO-5 | todo |  |  |  |  | D-007, R-LIGHT-004/005/009 |
| R-CARRY-005 | Metal robustness guards + probe | §0 / CO-7 | todo |  |  |  |  | R-METAL-006 |

## 1. Concurrency runtime

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-JOB-001 | Worker pool + `rt_job_submit`/`wait`/parallel-for on `rt_platform.h` | §1 | todo |  |  |  |  |  |
| R-JOB-002 | Deterministic ordered-merge primitive | §1 | todo |  |  |  |  | GATE-002 |
| R-JOB-003 | Main-thread commit/upload queue | §1 | todo |  |  |  |  | used by §4 |
| R-JOB-004 | Thread-safe allocator/GC/handle paths | §1 | todo |  |  |  |  |  |
| R-JOB-005 | `World3D` worker-count/enable getters | §1 | todo |  |  |  |  | internal-first |

## 2. Floating origin

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ORIG-001 | `rt_scene3d_rebase_origin` (nodes/bodies/audio/particles/queries) | §2 | todo |  |  |  |  |  |
| R-ORIG-002 | Promote cull bounds + physics state precision | §2 | todo |  |  |  |  |  |
| R-ORIG-003 | Camera-relative upload path | §2 | todo |  |  |  |  | GPU stays float |
| R-ORIG-004 | `World3D.set_FloatingOrigin` capability gate | §2 | todo |  |  |  |  | off = unchanged |

## 3. Spatial index

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-IDX-001 | Index over scene graph, incremental refit | §3 | todo |  |  |  |  |  |
| R-IDX-002 | `Scene3D.query*` + `raycastNodes` | §3 | todo |  |  |  |  |  |
| R-IDX-003 | Replace draw walk with index query (flag-gated) | §3 | todo |  |  |  |  |  |
| R-IDX-004 | Route physics broadphase/queries through index | §3 | todo |  |  |  |  | per D-IDX |

## 4. Async asset + upload

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ASYNC-001 | Worker decode + budgeted main-thread upload | §4 | todo |  |  |  |  |  |
| R-ASYNC-002 | `LoadModelAsync` + residency + eviction | §4 | todo |  |  |  |  |  |
| R-ASYNC-003 | Texture/mesh mip/LOD residency hooks | §4 | todo |  |  |  |  |  |

## 5. World partition + terrain streaming

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-STREAM-001 | Terrain tile grid + seam stitching (lift 4096² cap) | §5 | todo |  |  |  |  |  |
| R-STREAM-002 | Scene cell load/unload (subtree+physics+nav) | §5 | todo |  |  |  |  |  |
| R-STREAM-003 | Streamed container + bake hook | §5 | todo |  |  |  |  | D-STREAM |
| R-STREAM-004 | Per-tile heightfield colliders | §5 | todo |  |  |  |  |  |

## 6. Visibility

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-VIS-001 | Occlusion culling (software depth / portal-PVS) | §6 | todo |  |  |  |  |  |
| R-VIS-002 | Auto-LOD + HLOD/impostors | §6 | todo |  |  |  |  |  |
| R-VIS-003 | Reconcile `SetOcclusionCulling` alias | §6 | todo |  |  |  |  | CO-8 |

## 7. Lighting

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-LIT-001 | Clustered/forward+ light culling | §7 | todo |  |  |  |  | keep forward shaders |
| R-LIT-002 | Cascaded shadow maps + shadow-caster cap lift | §7 | todo |  |  |  |  | uses CastsShadows |
| R-LIT-003 | Capability fallback to 16-light forward | §7 | todo |  |  |  |  |  |

## 8. Physics depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-PHYS-001 | Contact manifolds + iterated warm-started solver | §8 | todo |  |  |  |  | replaces 1-contact |
| R-PHYS-002 | BVH/index mesh narrow-phase | §8 | todo |  |  |  |  | removes brute force |
| R-PHYS-003 | GJK/EPA convex-vs-convex | §8 | todo |  |  |  |  |  |
| R-PHYS-004 | Hinge/rope/6DOF joints | §8 | todo |  |  |  |  | closes overclaim |
| R-PHYS-005 | Solver islands + scaled sleeping | §8 | todo |  |  |  |  |  |

## 9. Navigation depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-NAV-001 | Navmesh auto-generation (voxelize→mesh) | §9 | todo |  |  |  |  |  |
| R-NAV-002 | Tiled/streamable navmesh + tile rebuild + carving | §9 | todo |  |  |  |  |  |
| R-NAV-003 | Off-mesh links + `agent_radius` corridors | §9 | todo |  |  |  |  |  |
| R-NAV-004 | Local avoidance + pathfinding acceleration | §9 | todo |  |  |  |  |  |

## 10. Animation depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ANIM-001 | IK: two-bone + look-at + FABRIK | §10 | todo |  |  |  |  |  |
| R-ANIM-002 | True additive layers | §10 | todo |  |  |  |  |  |
| R-ANIM-003 | Blend trees / blendspaces | §10 | todo |  |  |  |  |  |
| R-ANIM-004 | Retargeting + animation LOD | §10 | todo |  |  |  |  |  |

## 11. Asset pipeline depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-TEX-001 | GPU texture compression upload + software decode | §11 | todo |  |  |  |  |  |
| R-TEX-002 | KTX2 + Basis transcode + streaming mips | §11 | todo |  |  |  |  | from scratch |
| R-TEX-003 | glTF camera + multi-scene + Draco/meshopt | §11 | todo |  |  |  |  |  |

## Validation (every change)

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| R-VAL-001 | `check_runtime_completeness.sh` green | Validation | todo |  |  |
| R-VAL-002 | Build + `-L graphics3d` green mac/win/linux | Validation | todo |  | CO-1 |
| R-VAL-003 | `lint_platform_policy.sh` + `run_cross_platform_smoke.sh` | Validation | todo |  |  |
| R-VAL-004 | Before/after perf number per scale feature | Validation | todo |  | GATE-004 |
| R-VAL-005 | `runFrames` VM/native determinism per sim change | Validation | todo |  | GATE-002 |
| R-VAL-006 | Software path + capability-gated GPU per visual feature | Validation | todo |  | GATE-005 |
| R-VAL-007 | Success + negative/capability ctest per new fn, or waiver | Validation | todo |  |  |
