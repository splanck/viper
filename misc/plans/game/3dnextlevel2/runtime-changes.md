# Runtime additions

The 3D scale tier is built in the normal Viper C runtime on top of the existing
`Viper.Graphics3D` / `Viper.Game3D` / backend layers. These are not a renderer
rewrite; they define behavior the public API can rely on. Every new runtime
function follows the established pattern (as in `3Dnextlevel/runtime-changes.md`):

1. `RT_FUNC(...)` / `RT_METHOD(...)` / `RT_PROP(...)` in `src/il/runtime/runtime.def`.
2. C declaration in the relevant header.
3. Real implementation **and** a `#ifdef VIPER_ENABLE_GRAPHICS` disabled-graphics
   stub.
4. `./scripts/check_runtime_completeness.sh` green.

Hard constraints (Core Principles): zero external dependencies, cross-platform on
day one via `rt_platform.h` / `PlatformCapabilities.hpp` (no raw
`_WIN32`/`__APPLE__`/`__linux__` outside adapters), VM/native determinism for
simulation, and software-backend correctness baseline with capability-gated GPU
parity. Every new system is **opt-in** and must not change bounded-scene behavior
when its flag is off.

---

## 0. Carryover runtime items (Phase C)

Close the prior plan's open runtime contracts first (see `carryover.md`).

- **Render-target finalization (CO-4 / R-FRAME-016/018).** Define and implement
  `FinalizeFrame`/`ScreenshotFinal`/`Flip` for render-target-backed frames, or
  add an explicit capability string (`BackendSupports("rt-finalize")`) and trap
  with a clear diagnostic when unsupported.
- **Lifetime diagnostics (CO-3 / R-LIFE-*).** Add destroyed-world/entity,
  double-despawn, missing-package-asset, and capability-fallback diagnostics in
  the `Game3D.<Type>.<method>: <reason>` form; add leak/retention probes.
- **Optional getters (CO-6).** `Light3D.get_CastsShadows`/`SetCastsShadows`
  (skip non-casters in shadow selection), material texture-presence getters.
- **Determinism/resize (CO-10), implicit-lighting decision (CO-5), Metal probe
  (CO-7), docs (CO-8)** as listed in `carryover.md`.

Tests: cross-platform `-L graphics3d` green (CO-1); perf lane (CO-11);
render-target finalization unit + Zia probe; lifetime diagnostic probes.

---

## 1. Concurrency runtime (job/worker system)

**Current facts.** No worker threads exist in `src/runtime/graphics/3d/` (grep:
zero `pthread_create`/`std::thread`). The only sync primitive is a dormant
model-cache mutex/condvar (`rt_game3d.c:439`). Everything is main-thread-only
(`graphics3d-architecture.md:485`).

**Change.**

- Add a runtime worker pool + job API on `rt_platform.h` threads:
  `rt_job_submit`, `rt_job_wait`, a parallel-for, and futures/handles.
- Add a **deterministic ordered-merge** primitive: results produced by workers
  are reduced into simulation state in a fixed, index-ordered sequence so output
  is independent of completion order.
- Add a **main-thread commit queue** for GPU resource creation/upload (consumed
  by §4).
- Make worker-touched paths thread-safe: audit `rt_obj_new_i64`, pool allocators
  (`rt_pool`), string interning, and Graphics3D handle validation.
- Expose minimal control: `World3D.get_WorkerCount`, enable/disable; internal
  first.

**Determinism contract.** Simulation (physics step, animation sampling,
gameplay) is deterministically scheduled. Workers do load/decode/cull/bake/
transcode only. `runFrames` must produce identical state with the pool enabled
vs. disabled, VM and native.

**Tests.** Parallel-map determinism; race/stress (TSan where available); pool
on/off `runFrames` parity; ADR if any VM/IL change is implied.

---

## 2. Floating origin and coordinate precision

**Current facts.** Scene-node transforms are `double` (`rt_scene3d_internal.h:77`)
but cull AABBs are `float` (line 102), mesh vertices and the GPU pipeline are
`float`, and there is no origin rebasing. Precision degrades a few km from origin.

**Change.**

- Implement the Phase-0 strategy: periodic active-scene origin rebase and/or
  per-cell local origins with camera-relative model matrices computed at upload.
- Promote cull bounds and physics body integration state to the chosen precision;
  keep GPU upload `float` but relative to the camera/cell origin.
- Add an atomic `rt_scene3d_rebase_origin(delta)` that shifts nodes, physics
  bodies, audio sources, particles, and in-flight queries between frames.
- Capability-gate via `World3D.set_FloatingOrigin(enabled)`; off ⇒ unchanged.

**Tests.** Far-origin (50 km) vs. near-origin render/physics within tolerance;
rebase determinism under `runFrames`; bounded-scene byte-equality with flag off.

---

## 3. Spatial acceleration index

**Current facts.** `draw_node` walks every child every frame
(`rt_scene3d.c:2100`); cull is O(total nodes). Physics broadphase is a separate
single-axis SAP re-sorted each step (`rt_physics3d.c:3662`). Mesh BVH exists but
is raycast-only.

**Change.**

- Implement the Phase-0 index (loose octree / BVH / grid) over the scene graph,
  maintained incrementally (dirty/refit) under double-precision transforms.
- Add `rt_scene3d_query_visible(frustum, out)` and generic spatial queries
  (point/AABB/sphere/ray) the index serves.
- Replace the draw walk with an index query (old path behind the flag for parity).
- Where Phase 0 unified them, route physics broadphase + `Scene3D`/physics
  raycast/overlap/sweep through the index; otherwise keep a sibling physics tree
  built with the same code.

**Tests.** Cull/query equality vs. flat-walk reference (no popping); recorded
scaling on the 10k-node fixture; broadphase parity within tolerance.

---

## 4. Async asset loading and GPU upload

**Current facts.** `Assets3D.LoadModel`/`Preload` are synchronous
(`rt_game3d.c:3733/3785`); file read + parse + GPU build run on the caller thread.
Loading mid-gameplay hitches the main thread.

**Change.**

- Move file read + glTF/FBX/image decode onto §1 workers; create/upload GPU
  resources on the §1 main-thread commit queue with a per-frame budget.
- Add `Assets3D.LoadModelAsync(path) -> handle`, residency state, and ref-counted
  resources with an eviction policy (LRU/distance).
- Convert `Preload` to a real background warm.
- Add texture/mesh mip/LOD residency hooks consumed by §5/§6/§11.

**Backend notes.** Resource creation stays on the backend's owning thread;
workers produce CPU-side decoded payloads only. Capability-gate where a backend
cannot defer uploads.

**Tests.** Hitch-budget assertion; async==blocking resource equality;
residency counters return to baseline after churn (no leak).

---

## 5. World partition and terrain streaming

**Current facts.** Terrain is one heightmap hard-capped at 4096²
(`rt_terrain3d.c:216`); chunks are interior subdivisions, not streamable tiles.
Scene/`.vscn` loaders are whole-file (`rt_scene3d_vscn.c`). No load/unload path.

**Change.**

- Add a terrain tile grid: many `Terrain3D` tiles with shared edges and LOD-seam
  stitching, streamed via §4; lift the single-heightmap ceiling.
- Add scene cells: `rt_scene3d` subtree load/unload (with physics + nav) keyed to
  player position via §1/§3/§4.
- Define the streamed container: extend VSCN with tile/cell indexing or add a
  tiled side-format (no new top-level format). Add a bake hook ViperIDE targets.
- Wire per-tile heightfield colliders to terrain tiles.

**Tests.** Tile/cell stream in/out across boundaries within hitch budget; seam
check; bounded-memory traversal of a >4 km² world; scripted-traversal determinism.

---

## 6. Visibility scaling (occlusion + auto-LOD/HLOD)

**Current facts.** Only frustum cull + front-to-back sort; "not full
occlusion-query or Hi-Z" (`rt_canvas3d.h:443`). Per-node discrete LOD exists
(`rt_scene3d.c:2012`) but is manual; no HLOD/impostors. `SetOcclusionCulling` is
a frustum-cull alias.

**Change.**

- Implement occlusion culling: software-rasterized depth occluders (baseline) and
  portal/PVS for interiors, over the §3 index; capability-gate per backend.
- Add automatic LOD selection (screen-space error / auto-generated LODs) and
  HLOD/impostor proxies for distant clusters and vegetation.
- Reconcile `SetOcclusionCulling` (CO-8) to select real occlusion culling while
  retaining frustum-only behavior under `SetFrustumCulling`.

**Tests.** Occluded-draw skip counts on a dense fixture; LOD/impostor stability
vs. full-detail reference within tolerance.

---

## 7. Lighting scaling (clustered/forward+ and cascaded shadows)

**Current facts.** Forward renderer, `VGFX3D_MAX_LIGHTS 16`,
`VGFX3D_MAX_SHADOW_LIGHTS 2` (`rt_canvas3d_internal.h:308-309`).

**Change.**

- Implement clustered/forward+ light culling (froxel grid; per-cluster light
  lists) raising effective light count while keeping the existing forward
  shaders. Software backend gets a correct reference path.
- Implement cascaded shadow maps for the primary directional light; lift the
  shadow-caster cap where backends allow; honor `Light3D.CastsShadows` (CO-6).
- Capability-gate to current 16-light forward behavior on unsupported backends.

**Backend notes.** Cluster build is CPU-side and shared; per-backend upload of
cluster light-index buffers. CSM cascade count is capability-driven.

**Tests.** >16-light correctness vs. naive forward (within tolerance); CSM
stability (no acne/peter-panning beyond tolerance); fallback path.

---

## 8. Physics depth

**Current facts.** One contact point per pair (`physics3d.md:145`); un-iterated,
non-warm-started solver (`rt_physics3d.c:4421` loop is joints only); brute-force
O(triangles) mesh narrow-phase (BVH raycast-only); no GJK ("convex hull" is
triangle soup, `rt_collider3d.c:569`); hinge/rope joints documented but absent
(`rt_physics3d.h:80`; only distance/spring in `rt_joints3d.c`).

**Change.**

- Implement multi-point contact manifolds + an iterated, warm-started
  sequential-impulse solver (configurable iterations) so bodies stack/rest.
- Drive mesh narrow-phase through the per-mesh BVH and/or §3 index; remove the
  brute-force triangle loop.
- Implement GJK/EPA convex-vs-convex; make `NewConvexHull` real convex collision.
- Implement hinge and rope joints (close the doc overclaim) and a 6DOF/
  configurable joint; add solver islands + scaled sleeping.

**Tests.** Box-stack/pile rest stability; manifold correctness; GJK vs. analytic;
hinge/rope/6DOF behavior; body-count perf fixture; `runFrames` determinism.

---

## 9. Navigation and AI depth

**Current facts.** Navmesh is baked from one provided mesh
(`rt_navmesh3d.c:222`); A* + simplified string-pull (`:663`); `agent_radius`
stored but never applied (`:80`); no autogen, off-mesh links, avoidance, or tiled
mesh; O(triangles) find-tri (`:434`); per-agent 4 Hz repath.

**Change.**

- Implement navmesh auto-generation from arbitrary geometry (voxelize → walkable
  regions → contour → mesh; from-scratch Recast-style).
- Implement tiled/streamable navmesh aligned to §5 cells, runtime tile rebuild,
  and dynamic obstacle carving.
- Implement off-mesh links and apply `agent_radius` corridor erosion.
- Implement local avoidance (ORCA/RVO-style) and pathfinding acceleration
  (spatial find-tri, path cache, time-sliced/hierarchical A*), parallel via §1.

**Tests.** Autogen vs. hand-baked equivalence; tile rebuild on obstacle change;
avoidance interpenetration bounds; agent-count perf fixture.

---

## 10. Animation depth (IK, additive, blend trees, retargeting)

**Current facts.** 4-bone skinning, 256 bones; state machine + crossfade + root
motion present; "additive" layers are actually masked replace-blends
(`rt_animcontroller3d.c:823`); no IK, blend trees, or retargeting.

**Change.**

- Implement IK: two-bone (foot placement), look-at/aim, and FABRIK chain, applied
  to the bone palette before skinning.
- Implement true additive layers (reference-pose subtraction) alongside the
  existing overlay blend.
- Implement parametric blend trees / 1D-2D blendspaces over `AnimBlend3D`.
- Implement cross-skeleton retargeting (humanoid bone mapping).
- Add animation LOD (rate/bone-count reduction by §3 distance).

**Tests.** IK target-error bounds on slope/stairs + look-at; additive vs.
replace correctness; retarget visual check; anim-LOD parity within tolerance.

---

## 11. Asset pipeline depth

**Current facts.** Textures decode to raw RGBA8 only (no BC/DXT/ASTC/ETC2/KTX2);
glTF imports no cameras and only the active scene (`rt_gltf.c`); no Draco/meshopt;
OBJ ignores `.mtl` (`rt_mesh3d.c:2165`).

**Change.**

- Implement GPU texture compression upload (BC/DXT desktop, ASTC/ETC2 where
  supported) with a software-decode reference; capability-gate per backend.
- Implement KTX2 + Basis-universal transcode (from scratch) and streaming mip
  residency via §4.
- Add glTF camera import + multi-scene selection; add Draco/meshopt decode.
- Backlog: OBJ `.mtl` handling.

**Tests.** Compressed vs. raw within tolerance per backend; KTX2/Draco/meshopt
load; glTF camera + secondary-scene import; recorded VRAM reduction.

---

## 12. Vertical-slice and tooling hooks

- Add runtime inspection hooks the ViperIDE level editor will consume: cell/tile
  enumeration, residency state, navmesh bake trigger, and perf counters
  (`World3D` getters; internal `rt_*` introspection). Editor built elsewhere.
- Ensure the slice's systems all expose capability strings and degrade safely.

**Tests.** Slice smoke + deterministic replay + software visual baseline; perf
baselines recorded cross-platform.

---

## Validation for every runtime change

- `./scripts/check_runtime_completeness.sh`.
- Build + `ctest --test-dir build -L graphics3d` green on macOS, Windows, Linux
  (CO-1); no macOS-only paths.
- `./scripts/lint_platform_policy.sh` + `./scripts/run_cross_platform_smoke.sh`
  for platform-sensitive work.
- A before/after performance number on a committed Phase-0 fixture for any scale
  feature.
- `runFrames` VM/native determinism parity for any simulation-touching change.
- A software-backend correctness path for any new visual feature; GPU parity
  capability-gated + smoke-tested.
- Each new public runtime function has a success-path and a negative/capability
  ctest, or a named waiver in `progress/06-waivers.md`.
