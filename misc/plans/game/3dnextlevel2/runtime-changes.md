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

Every new public class also follows the 3D object ABI pattern:

- Add `RT_CLASS_BEGIN("Viper.<...>", ...)` to `runtime.def`.
- Append a permanent `RT_G3D_*_CLASS_ID` in `rt_graphics3d_ids.h`; never
  renumber or reuse existing negative sentinel ids.
- Validate handles through the existing `rt_g3d_checked_or_null` helpers.
- Provide disabled-graphics stubs and class docs/tests before marking the API
  complete.

Hard constraints (Core Principles): zero external dependencies, cross-platform on
day one via `rt_platform.h` / `PlatformCapabilities.hpp` (no raw
`_WIN32`/`__APPLE__`/`__linux__` outside adapters), VM/native determinism for
simulation, and software-backend correctness baseline with capability-gated GPU
parity. Every new system is **opt-in** and must not change bounded-scene behavior
when its flag is off.

**Current source layout (verified 2026-05-29).** The 3D runtime is organized
into per-domain subdirectories under `src/runtime/graphics/3d/`: `scene/`
(scene graph, transforms, raycast, VSCN), `physics/` (world, colliders, joints),
`nav/` (navmesh, agents, paths), `anim/` (controller, skeleton, morph targets),
`render/` (canvas, camera, mesh, material, light, post-FX, decals, sprites),
`world/` (terrain, water, vegetation, particles), `assets/` (glTF/FBX loaders),
`audio/`, and `backend/` (software + Metal/D3D11/OpenGL behind the `vgfx3d`
vtable). Only `rt_game3d.c` (the `Viper.Game3D` ergonomics layer) sits at the top
level. New subsystems land in the matching subdir (e.g. the spatial index in
`scene/`, the solver work in `physics/`, the navmesh baker in `nav/`); a job
runtime that is not 3D-specific belongs in `src/runtime/` proper, on
`rt_platform.h`. All file:line citations below use this layout.

---

## 0. Carryover runtime items (Phase C)

Close the prior plan's open runtime contracts first (see `carryover.md`).

- **Render-target finalization (CO-4 / R-FRAME-016/018).** Treat the existing
  `FinalizeFrame`/`ScreenshotFinal` surface as the public contract. Phase C
  verifies backend-specific behavior, keeps `BackendSupports("rt-finalize")`
  honest, and traps with a clear diagnostic when unsupported.
- **Lifetime diagnostics (CO-3 / R-LIFE-*).** Destroyed-world/entity and
  double-despawn diagnostics use the `Game3D.<Type>.<method>: <reason>` form;
  missing package assets produce terminal error handles; capability fallback
  reasons remain inspectable; imported-group despawn and cache-clear/reload
  churn are covered.
- **Optional getters (CO-6).** `Light3D.get_CastsShadows`/
  `Light3D.set_CastsShadows`
  (skip non-casters in shadow selection), material texture-presence getters.
- **Metal robustness probe (CO-7).** Metal skybox zero-vector fallback uses
  Canvas3D's `-Z` convention; fake-Metal unit coverage and the platform GPU
  smoke both exercise degenerate normals/tangents with a normal map.
- **Determinism/resize probes (CO-10).** `World3D.runFramesOnly` has a
  worker-count replay parity baseline; `Canvas3D.Resize` resizes the backend
  framebuffer and the next frame's active-output projection aspect without
  mutating the stored camera projection.
- **Phase-1 exit partials (CO-12).** Fixed-loop accumulator and spiral-guard
  behavior, common-sample no-`Mat4` guard, direct `Entity3D.FromNode` Zia
  coverage, packaged glTF hierarchy loading through `Assets3D.LoadModelAsset`,
  and destroyed-world diagnostics are covered.
- **Implicit-lighting decision (CO-5) and docs (CO-8)** are closed as listed in
  `carryover.md`.

Tests: cross-platform `-L graphics3d` green (CO-1); perf lane (CO-11);
render-target finalization unit + Zia probe; lifetime diagnostic probes.

---

## 1. Concurrency runtime (job/worker system)

**Current facts.** `World3D.workerCount` / `jobsEnabled` / `setWorkerCount`
exist and `World3D` now owns a lazy internal `ThreadPool` when a multi-worker
world has eligible work. `World3D.stepSimulation` uses that pool for animator
batches, and `test_rt_game3d` verifies single-worker vs. multi-worker parity for
root motion, state time, and animation events. The general worker substrate is
the existing `Viper.Threads.Pool` / `Viper.Threads.Parallel` runtime on
`rt_platform.h`; `g3d_3dnext2_surface_probe` verifies ordered parallel-map
replay plus `World3D.runFramesOnly` worker-count replay. The process-wide
model-cache mutex/condvar still only serializes the shared model cache;
the internal Graphics3D commit queue now provides the deterministic
worker-to-main-thread handoff for renderer-facing commits. The thread-safety
audit for Phase 1 keeps public 3D handle mutation main-thread-only: workers may
use `Viper.Threads.Pool` / `Viper.Threads.Parallel`, plain copied data, retained
ordered-map outputs, and commit-queue enqueue, while commit draining and handle
mutation stay on the main thread. `test_rt_g3d_commit_queue` covers the
worker-drain rejection path so worker jobs cannot apply renderer-facing commits
directly. Current glTF/GLB asset worker paths consume the queue for runtime
object commits, and the focused `scripts/g3d_tsan_concurrency_lane.sh` lane is
clean on Apple M4 Max for the worker pool, ordered merge, asset workers, and
main-thread commit queue.

**Change.**

- Reuse the existing internal runtime worker pool and parallel APIs on
  `rt_platform.h` threads: `Viper.Threads.Pool`, `Viper.Threads.Parallel`, and
  their C runtime entry points.
- Add a **deterministic ordered-merge** primitive: results produced by workers
  are reduced into simulation state in a fixed, index-ordered sequence so output
  is independent of completion order.
- Add a **main-thread commit queue** for GPU resource creation/upload (consumed
  by §4). The internal queue is now present and covered by
  `test_rt_g3d_commit_queue`; §4 still needs to route decoded asset payloads
  through it.
- Make worker-touched paths thread-safe: Phase 1 worker paths are limited to
  the existing thread runtime, copied data, retained ordered-map results, and
  commit-queue enqueue. Graphics3D/Game3D handle validation and commit draining
  remain main-thread-only, with a negative CTest guarding worker-drain misuse.
  Future worker-touched asset loaders/upload queues reopen this audit under §4.
- Expose only small public controls (`World3D.workerCount`,
  `World3D.jobsEnabled`, `World3D.setWorkerCount`); game code does not author
  jobs in this milestone.

**Determinism contract.** Simulation (physics step, animation sampling,
gameplay) is deterministically scheduled. Workers do load/decode/cull/bake/
transcode only. `runFrames` must produce identical state with the pool enabled
vs. disabled, VM and native.

**Tests.** Parallel-map determinism; pool on/off `runFrames` parity; focused
race/stress via `scripts/g3d_tsan_concurrency_lane.sh`; VM/native parity via
the NL3-031 determinism lane (`test_codegen_env_is_native`, native-run Zia
promise tests, and `test_crosslayer_arith`); ADR if any VM/IL change is implied.

---

## 2. Floating origin and coordinate precision

**Current facts.** Scene-node transforms are `double`
(`scene/rt_scene3d_internal.h`), and Scene3D spatial/query/draw-candidate cull
AABBs now store transformed world bounds as `double`. Physics body position and
velocity integration state is double-precision. Game3D floating-origin frames
enable a Canvas3D camera-relative upload path for double `DrawMesh` matrices,
camera frame position/view translation, point/spot light positions, and
identity-matrix raw/generated vertex paths before they narrow to backend floats.
`Mesh3D.AddVertex` keeps an internal double-position sidecar for raw world-space
mesh draws, while standalone `Particles3D`, `Sprite3D`, and decal/billboard
geometry build camera-relative vertices for active frames. Caller-supplied float
instancing matrices and exact backend frustum tests still remain float
precision. Origin rebasing exists through `Scene3D.RebaseOrigin`
and the Game3D floating-origin path; that path now shifts camera, scene nodes,
physics bodies, explicit audio listener pose, and world-owned effect-registry
particles/decals. The cross-system hook is now exposed as
`World3D.rebaseOrigin(dx, dy, dz)`, guarded to run between frames, and routes
physics through `Physics3DWorld.RebaseOrigin` so body/contact/query state shifts
atomically. Standalone `Particles3D.RebaseOrigin` and `Sprite3D.RebaseOrigin`
cover raw generated paths outside Game3D ownership.

**Change.**

- Implement the Phase-0 strategy: periodic active-scene origin rebase and/or
  per-cell local origins with camera-relative model matrices computed at upload.
- Promote cull bounds and physics body integration state to the chosen precision;
  keep GPU upload `float` but relative to the camera/cell origin.
- Add atomic rebase hooks that shift nodes, physics bodies/contact/query state,
  audio listener state, particles, sprites, and decals between frames.
- Capability-gate via `World3D.floatingOrigin`; off means unchanged.

**Tests.** Far-origin Scene3D queries/raycasts at `1e9` preserve sub-float
node separation, Physics3D body integration at `1e9` preserves a `0.125` unit
step delta, and Canvas3D/Game3D tests prove floating-origin frames rebase the
camera frame payload, double `DrawMesh` model translation, raw identity-mesh
vertices, standalone particle/sprite generated vertices, and light position
while flag-off frames keep the bounded-scene upload path unchanged. Game3D
also captures registered particle/decal draw centers before and after a 50 km
rebase, proving live particle positions and cached decal geometry move with
the world. `test_rt_physics3d`, `test_rt_particles3d_contract`, and
`test_rt_canvas3d` cover standalone physics query/contact rebasing and
particle/sprite raw-path rebase hooks. `test_rt_game3d` now also renders the
same scene near origin and after a 50 km floating-origin rebase with frustum
culling enabled and disabled, verifies camera-relative upload on the far pass,
compares final-frame pixels within tolerance, and proves the bounded-scene
output remains byte-identical after toggling `floatingOrigin` back off.
`g3d_bounded_no_regression_probe` closes the broader bounded-sample gate by
running `walk_min.zia` once on the default bounded path and once with scale
flags explicitly off, then comparing final-frame pixels and captured state
exactly.

---

## 3. Spatial acceleration index

**Current facts.** `Scene3D` now maintains an internal BVH spatial index over
visible drawable scene nodes. Transform-only dirties refit the existing tree,
while hierarchy, visibility, mesh, LOD, and impostor changes rebuild it lazily;
`QueryAABB`, `QuerySphere`, `RaycastNodes`, and `Draw` use indexed candidates
with deterministic flat-walk fallback/parity coverage. Draw still performs the
exact selected-LOD/impostor frustum test before submitting. `test_rt_scene3d`
includes a generated 10k drawable-node grid that verifies BVH shape, transform
refit, isolated spatial queries narrowing to one candidate, and indexed draw
culling considering less than 10% of total drawable nodes before falling back to
the exact frustum test. Physics broadphase remains a separate body-centric
single-axis sweep-and-prune sorted by min-X each step (`world3d_detect_contacts`);
the sibling structure is intentional because physics pair generation must cover
non-render bodies, layer/mask filtering, static-static rejection, trigger state,
and contact-event identity.
A per-mesh physics BVH exists (`physics_bvh_*` fields built in `render/rt_mesh3d.c`)
and now drives raycasts plus mesh-vs-sphere/capsule/box/convex-hull narrow-phase
candidate pruning. The named `PHYSICS_MESH_BVH_TARGET` fixture covers 16 static
mesh tiles and builds only the one overlapping tile BVH after the body-centric
broadphase selects candidates. The narrow-phase keeps a full triangle scan
fallback if BVH construction or traversal cannot complete.

**Change.**

- Maintain the BVH over the scene graph with dirty/refit under double-precision
  transforms.
- Add `rt_scene3d_query_visible(frustum, out)` and generic spatial queries
  (point/AABB/sphere/ray) the index serves.
- Replace the draw walk with an index query (old path behind the flag for parity).
- Publish a shared spatial-query contract while keeping the sibling physics
  broadphase tuned for solver needs and matching query semantics.

**Tests.** Cull/query equality vs. flat-walk reference (no popping); generated
10k-node candidate-scaling fixture plus release-lane wall-clock timing;
broadphase parity within tolerance.

---

## 4. Async asset loading and GPU upload

**Current facts.** `Assets3D.LoadModel` is still synchronous, while
`Assets3D.Preload` and `Assets3D.PreloadAsset` now schedule the template-mode
async worker path and rely on `World3D.tick` / simulation steps to drain the
asset commit queue with the existing per-frame item budget plus the
`Assets3D.SetUploadBudget(bytes)` decoded-texture byte budget. Phase 4 now has the public
`Viper.Game3D.AssetHandle3D` class plus `LoadModelAsync`,
`LoadModelAssetAsync`, `LoadModelTemplateAsync`, and
`LoadModelTemplateAssetAsync`; these return pending handles that service the
process-wide asset commit queue on observation. Valid uncached model requests
now stage glTF/GLB root bytes plus external, data URI, and bufferView-backed
buffer/image payloads on the existing thread pool, worker-decode PNG, BMP, JPEG, and GIF
image payloads into raw RGBA POD blobs, prepare decoded RGBA POD into `Pixels`
across bounded main-thread commit slices, and worker-decode static, skinned, and morph-target glTF
triangle topology primitives (lists/strips/fans) with positions, optional
normals, sparse accessor overrides, JOINTS/WEIGHTS attributes, and morph deltas into raw
`Mesh3D`/`MorphTarget3D` POD, reject missing
required buffer payloads, accessor-range overruns, and corrupt required
PNG/BMP/JPEG/GIF texture image payloads during preload, and publish terminal
handle state after building runtime objects through the §1 main-thread commit
queue. Blocking glTF loads now reject the same corrupt required data-URI
texture image payloads instead of silently dropping the material map.
Non-glTF formats still enqueue a main-thread load request. `Preload` and
`PreloadAsset` use the same path for background filesystem and package-aware
template warming. Cached
template hits and preflight failures can still complete immediately on observation, and
`cancel()` is terminal before worker completion. `SetResidencyBudget`,
`GetResidentBytes`, `SetResidencyHint`, and `Evict` control the shared template
cache with conservative byte estimates and priority/distance-aware eviction; the
estimate now includes decoded material texture pixels so textured templates
cannot bypass the cache budget as mesh-only entries, and blocking/async
load-clear churn can assert the shared resident-byte counter returns to zero.
`SetUploadBudget` controls async commit drains by decoded RGBA image bytes;
zero pauses positive-cost decoded texture slices, negative restores unlimited
drains, and positive budgets advance large decoded images across multiple queue
drains before final publish. `Canvas3D.SetTextureUploadBudget` now caps
Pixels-backed 2D material/cubemap upload rows and native compressed
`TextureAsset3D` mip submissions per backend frame on Metal/OpenGL/D3D11;
negative restores unlimited upload, `0` pauses new uploads, and positive
sub-row budgets still advance one row for liveness.
`Canvas3D.TextureUploadBytes` records the actual texture payload bytes uploaded
into backend storage by the latest ended frame, and
`Canvas3D.TextureUploadPendingBytes` reports queued material-texture/cubemap row
bytes plus native compressed mip bytes still waiting for budget. Cubemap uploads
use the same Metal/OpenGL/D3D11 row-budget path as Pixels-backed 2D material
textures; shared backend pending-byte helpers prove queued row/native bytes
return to zero when final slices drain, and cache hits plus software fallback
report `0`.
`ClearCache` advances a cache generation so older in-flight preload jobs cannot
repopulate the cache after the clear. The open-world streaming hitch probe
records blocking-vs-async template timing, proves zero upload budget holds a
positive-cost async commit pending until a positive budget is restored, and
asserts resident bytes return to zero after cache churn. File and
asset lookup failures, unsupported model extensions, and malformed glTF JSON now
complete as terminal non-cancel error handles. Its native-compressed CTest lane
now also proves capable backend upload budgeting by recording the active GPU
backend/format, zero-budget pending bytes, release time, and upload bytes.

**Change.**

- Move file read + glTF/FBX/image decode onto §1 workers; create/upload GPU
  resources on the §1 main-thread commit queue with a per-frame budget.
- Add `Assets3D.LoadModelAsync(path) -> Viper.Game3D.AssetHandle3D`,
  `LoadModelAssetAsync`, `LoadModelTemplateAsync`, and
  `LoadModelTemplateAssetAsync`; the handle exposes lower/camel Game3D members
  (`ready`, `progress`, `getEntity`, `getTemplate`, `cancel`, `error`).
  The deferred-handle baseline, worker scheduling, main-thread commit
  publication, missing-path load-error handles, unsupported extension errors,
  and malformed glTF JSON recovery are in place; glTF/GLB root bytes plus
  external, data URI, and bufferView-backed buffer/image payloads now stage on
  workers before main-thread runtime construction, with worker-side required
  buffer, accessor-range, and corrupt required texture-image validation. PNG, BMP, JPEG, and GIF image payloads are also
  decoded on the worker into raw RGBA POD blobs that the commit side prepares into `Pixels`
  objects across bounded slices with one final content-generation touch, and static/skinned/morph-target glTF triangle topology primitives (lists/strips/fans, including sparse accessors)
  with positions, optional normals, sparse accessor overrides, JOINTS/WEIGHTS
  attributes, and morph deltas are decoded into raw `Mesh3D`/`MorphTarget3D` POD
  that the commit side installs directly into runtime mesh objects, regenerating
  missing normals during commit, remapping skin joints after skeleton import, and
  attaching morph targets. The async commit queue is now cost-aware, and
  `Assets3D.SetUploadBudget` gates decoded RGBA image preparation slices.
  `Canvas3D.SetTextureUploadBudget` row-slices Metal/OpenGL/D3D11
  Pixels-backed 2D material texture and cubemap uploads,
  `TextureUploadPendingBytes` exposes queued material and cubemap row bytes,
  and `TextureUploadBytes` exposes current-frame backend texture upload bytes
  for material and cubemap rows. `Preload`
  and `PreloadAsset` now start filesystem and package-aware template async paths
  immediately, and world frame/simulation ticks drain queued asset commits, so
  cache warming no longer calls the blocking loader or traps synchronously on
  corrupt glTF. Corrupt required PNG/BMP/JPEG/GIF texture payloads now become
  terminal async/blocking load failures. The first cache
  residency controls are in place using a byte budget over `ModelTemplate`
  entries, including decoded material texture pixels. `Assets3D.SetResidencyHint`
  adds explicit priority/distance eviction metadata so higher-priority and
  nearer templates survive budget pressure before lower-priority or farther
  entries, with LRU retained as the tie-breaker; `Assets3D.GetResidentBytes`
  exposes the shared resident-byte counter for blocking/async load-clear churn
  assertions; pre-clear preload jobs are generation-guarded so they cannot
  repopulate a cleared cache.
- Convert `Preload` to a real background warm. Implemented for filesystem and
  package-aware templates via the existing async worker + main-thread commit queue.
- Texture and mesh residency hooks are now in place: `TextureAsset3D.SetResidentMipRange`
  controls mip residency, `Mesh3D.Resident` / `ResidentBytes` expose mesh-payload
  residency, and `SceneNode3D.SetLodResident` / `GetLodResidentBytes` let LOD
  detail be demoted while retaining the node/model handles.

**Backend notes.** Resource creation stays on the backend's owning thread;
workers produce CPU-side decoded payloads only. Capability-gate where a backend
cannot defer uploads.

**Tests.** Hitch-budget assertion; async==blocking resource equality;
residency counters return to baseline after churn (no leak). Current coverage
includes `g3d_openworld_slice_streaming_hitch_probe`, which emits a `HITCH:`
line with blocking-vs-async timing and verifies zero-upload-budget gating plus
resident-byte cache churn. `g3d_openworld_slice_streaming_hitch_native_compressed_probe`
opts into the same script's GPU lane and records the bounded native-compressed
backend upload release, raw-vs-compressed RAM/VRAM reduction, and final-frame
texture tolerance.

---

## 5. World partition and terrain streaming

**Current facts.** Terrain is one heightmap hard-capped at 4096²
(`world/rt_terrain3d.c:216`; `Terrain3D.New` traps outside `[2, 4096]`); chunks
are interior subdivisions, not streamable tiles. Scene/`.vscn` loaders are
whole-file (`scene/rt_scene3d_vscn.c`). `Viper.Game3D.WorldStream3D` now exists
with center/radii/budget setters, manifest mount points, `update`, resident
counters, and a lazy `World3D.stream` owned handle. `mountCells` now parses a
VSCN streaming manifest with `cells[]` entries (`name`, `path`, `center`,
`radius`, `bytes`), resolves payload paths relative to the manifest, and
loads/unloads `.vscn` scene subtrees around the stream center through the
deterministic `WorldStream3D.update(dt)` load budget.
Resident VSCN cells now replace the manifest byte estimate with measured scene
resource residency from authored base meshes, resident LOD meshes, impostor
meshes, materials, and resident material textures; resident cells remeasure after
mesh/LOD residency changes, while unloaded cells still expose the manifest
`bytes` value for planning and inspection.
`mountTiledTerrain` now parses `tiles[]` manifests with the same metadata shape
plus optional `width`, `depth`, `scale`, and `heightmap`, drives deterministic
terrain tile residency around the stream center, instantiates `Terrain3D`
payloads for resident tiles, applies `viper-heightmap-v1` normalized height
sidecars through `Terrain3D.SetHeightmap`, and exposes them through
`WorldStream3D.getResidentTerrainTile(index)`. Resident terrain tiles render
through `World3D.drawScene` at their manifest-centered world positions using a
default terrain material. Full matching edges between adjacent resident tiles
are stitched in world-height space before LOD meshes are drawn, cached terrain
LOD chunks are invalidated, and collider/nav sources are rebuilt from the
stitched height grid. Resident tiles also spawn invisible static
heightfield-collider entities named
`<tile>_heightfield_collider` into the owning `World3D`, and unload those
bodies with tile residency. They also spawn hidden mesh-only
`<tile>_navmesh_source` nodes so `World3D.bakeNavMesh` includes active streamed
terrain. `WorldStream3D.update(dt)` now applies a deterministic per-update load
budget and reports deferred desired payloads through `pendingRequestCount`;
mount/radius/budget setters still recompute immediately for setup and editor
inspection. Both cell and terrain manifests now parse authored `material`,
`layer`/`collisionLayer`, `collisionMask` or nested `collision`, `navArea`,
`traversalCost`, and optional `sidecar` / `binarySidecar` metadata, expose it
through typed inspection hooks, and apply layer/mask/enabled data to spawned
cell roots and generated terrain heightfield colliders. Worker-backed
decode/upload and streamed nav bake/export tooling remain future layers.

**Change.**

- Extend scene cells from current `.vscn` subtree load/unload to include nav
  binding keyed to player position via §1/§3/§4.
- Expand terrain streaming from manifest-driven heightmapped `Terrain3D`
  payloads, deterministic staged update requests, and stitched adjacent tile
  edges into worker-backed payload decode/upload.
- Define the streamed container as a VSCN streaming manifest/extension with
  tile/cell indexing. Optional binary sidecars may hold payload data referenced
  by the VSCN manifest, but do not introduce a new general scene format. Add a
  bake hook ViperIDE targets.
- Expand per-tile collider/nav binding from parsed authored metadata into
  bake/export tooling that can feed retained per-tile nav source data.

**Tests.** Cell stream in/out across boundaries, manifest-driven terrain tile
payload residency, height sidecar loading, heightfield collider body residency,
rendered terrain-tile submission, streamed material/collision/nav metadata
inspection, stitched LOD-seam repair, a 9600-unit / >4 km2 multi-tile proof
beyond the single-heightmap cap, and a named all-quadrant traversal hitch/memory
proof are covered in
`test_rt_game3d`; the same unit lane now asserts staged stream updates and
`pendingRequestCount` while a desired terrain tile waits behind the per-frame
load budget; `g3d_openworld_slice_long_traversal` repeatedly churns all four
quadrants with deterministic replay and records max visit time / max resident
bytes; `g3d_openworld_slice_probe` verifies the world-scoped nav bake includes
streamed terrain. Remaining Phase 5 depth is streamed nav bake/export tooling;
runtime retained tile rebuilds are covered under Phase 9.

---

## 6. Visibility scaling (occlusion + auto-LOD/HLOD)

**Current facts.** `SetFrustumCulling` is frustum-only. `SetOcclusionCulling`
now enables frustum rejection plus a conservative CPU coverage/depth grid over
Scene3D BVH-selected opaque draw candidates before Canvas3D sorting, with raw
Canvas3D draws retaining the sorted-queue fallback. Scene3D now also supports
authored interior visibility zones and portal/PVS links that skip drawables in
unreachable zones; the open-world slice records the authored city/forest
draw/fill-proxy reduction proof. GPU occlusion-query / Hi-Z acceleration is
still pending. Per-node discrete LOD exists, and now has
screen-error authored-LOD selection plus generated textured impostor proxies via
`SceneNode3D.SetAutoLOD` and `SetImpostor`. `Canvas3D.OccludedDrawCount`
reports the latest visibility skip count, and `Canvas3D.OcclusionCandidateCount`
reports the latest CPU grid workload.

**Change.**

- Keep extending portal/PVS authoring as interior content grows; the portable
  software-rasterized depth baseline, index-fed candidate selection, authored
  zone/portal PVS, and named city/forest reduction fixture are in place, while
  GPU occlusion queries remain optional backend accelerators.
- Add automatic LOD selection for authored LODs (screen-space error) plus
  HLOD/impostor proxies for distant clusters and vegetation. Auto-generating new
  mesh simplification LODs is a later stretch item unless explicitly scoped.
- Reconcile `SetOcclusionCulling` (CO-8) to select real occlusion culling while
  retaining frustum-only behavior under `SetFrustumCulling`.

**Tests.** Occluded-draw skip counts on generated dense fixtures; authored
city/forest draw/fill-proxy reduction with software final-frame parity;
LOD/impostor stability vs. full-detail reference within tolerance.

---

## 7. Lighting scaling (clustered/forward+ and cascaded shadows)

**Current facts.** Forward fallback budget is `VGFX3D_FORWARD_LIGHT_LIMIT 16`;
the bounded many-light payload table is `VGFX3D_MAX_LIGHTS 64`; shadow caster
and cascade slots are capped by `VGFX3D_MAX_SHADOW_LIGHTS 4`
(`render/rt_canvas3d_internal.h`). The software backend advertises
`BackendSupports("clustered-lighting")` as the correctness baseline and submits
>16 active lights when `Canvas3D.SetClusteredLighting(true)` is enabled. Real
Metal, D3D11, and OpenGL backend vtables also advertise the bounded 64-light
GPU upload/shader path; fake test backends with GPU-like names remain
unsupported. The software backend and real platform GPU vtables with shadow
hooks advertise `BackendSupports("shadow-csm")`.

**Change.**

- Implement clustered/forward+ many-light support raising effective light count
  while keeping the existing forward shaders. Software backend gets a correct
  reference path; real GPU backends enable the bounded 64-light payload.
- Implement cascaded shadow maps for the primary directional light; lift the
  shadow-caster cap where backends allow; honor `Light3D.CastsShadows` (CO-6).
- Capability-gate to current 16-light forward behavior on unsupported backends.

**Backend notes.** The many-light payload is CPU-side and shared; per-backend
upload uses the existing Metal, D3D11, and OpenGL light tables sized to
`VGFX3D_MAX_LIGHTS`. CSM cascade count is capability-driven; primary-directional
cascades use contiguous shadow slots plus camera-depth split metadata in
`vgfx3d_light_params_t`.

**Tests.** >16-light correctness/cost vs. the 16-light forward fallback; CSM
stability (no acne/peter-panning beyond tolerance); fallback path. Current
evidence: `test_rt_canvas3d`, `test_rt_canvas3d_gpu_paths`, and
`g3d_openworld_slice_gpu_smoke` with local Metal Release baseline.

---

## 8. Physics depth

**Current facts.** The contact solver is now a detect-once, warm-started
sequential-impulse solver: per-manifold-point normal+friction impulses are
accumulated and persisted across frames (via `previous_contacts`, matched by
point index with a normal-agreement guard), seeded each step, friction-cone
clamped, with restitution applied only on the first frame of contact, a split
positional correction with a max-correction clamp, and post-solve sleep islands
(wake propagation across contacts) that freeze settled stacks. A 5-box stack
rests stably and bit-deterministically. AABB pairs and rotated box face contacts
expose bounded multi-point manifolds, while edge-style box contacts and other
non-box shape pairs still report one representative point. `SolverIterations`
drives the contact and joint velocity passes. Mesh-like
collider pairs against spheres, capsules, boxes, and convex hulls now traverse
the per-mesh BVH for candidate triangles and fall back to the full scan if the
BVH path is unavailable. `NewConvexHull` now uses a support-point GJK/EPA path
for convex-hull-vs-convex-hull and convex-hull-vs-simple pairs, including
contained primitive contacts, separated-overlapping-AABB simple pairs, and a
named 32-pair mixed-shape convex target. Hinge/rope joints and a
`SixDofJoint3D` now
exist as `Viper.Graphics3D.*` classes with
`RT_JOINT_HINGE`, `RT_JOINT_ROPE`, and `RT_JOINT_SIXDOF` type codes. SixDof
linear limits project frame-anchor separation and angular limits project
relative joint-frame pose angles from the creation relative orientation, with
velocity stops for locked axes and motion pushing past pose limits.
Dynamic bodies expose manual sleep/wake and idle auto-sleep, and the unit lane
now includes a sparse 321-body step stress, contact/event storage growth
coverage, and a named 257-body / 32-pile island-batched resting target that
publishes active island/body/contact telemetry.

**Change.**

- Extend multi-point contact manifolds beyond AABB pairs and add a warm-started
  sequential-impulse solver so bodies stack/rest. Axis-aligned and rotated
  face-contact box pairs now use bounded clipped manifolds.
  Existing raw `Viper.Graphics3D.CollisionEvent3D` contact accessors
  (`ContactCount`, `GetContact`, `GetContactPoint`, `GetContactNormal`,
  `GetContactSeparation`) must return the full manifold; add matching
  lower/camel `Viper.Game3D.Collision3DEvent` convenience accessors.
- Drive mesh narrow-phase paths through the chosen body-centric physics
  broadphase and then the per-mesh BVH; keep the brute-force triangle loop only
  as a correctness fallback.
- Keep the GJK/EPA convex path shape-accurate without API changes; remaining
  convex work is polish/perf depth beyond the named mixed-shape target.
- Tune scale-aware sleep thresholds if larger-world tests require it.

**Tests.** Box-stack/pile rest stability; manifold correctness; GJK vs. analytic;
hinge/rope/6DOF behavior; named island-batched body-count perf fixture;
`runFrames` determinism.

---

## 9. Navigation and AI depth

**Current facts.** Navmesh can still be baked from one provided mesh, and
`NavMesh3D.Bake(scene, ...)` now flattens `Mesh3D`-bearing `Scene3D` nodes through
their world transforms before using the voxel baker. `BakeTiled` accepts the
final API shape and retains voxel-cell source height/walkability plus corner
mappings for each tile; `RebuildTile` refreshes only the requested tile's
geometry, heights, and blocked state without a whole-scene voxel pass.
`agent_radius` erodes voxel-baked walkable cells and gates shared-edge portals
for direct `Build(mesh, ...)` meshes. A* uses the query grid for point location
and a simplified string-pull, with per-agent 4 Hz repath.
`NavMesh3D.AddOffMeshLink` stores authored endpoint pairs that resolve to
walkable triangles and are used as directed/bidirectional graph edges during
A*. Off-mesh links now carry kind, traversal-cost, and state metadata through
`SetOffMeshLinkMetadata` and getter APIs, and link costs contribute to A*.
`NavMesh3D.AddObstacle` stores finite AABB obstacles; tiled bakes re-carve only
overlapped tiles while non-tiled meshes refilter preserved source triangles, and
carving tests the exact triangle XZ footprint against the obstacle volume instead
of triangle AABB overlap. `RemoveObstacle` and `UpdateObstacle` edit that authored
obstacle list through the same tiled/non-tiled paths. `SetArea` tags polygons
with area names and traversal-cost multipliers, `GetArea`/`GetTraversalCost`
query that metadata, and `LastPathCost` reports the weighted latest path cost.
`NavAgent3D` now has opt-in same-NavMesh reciprocal velocity-obstacle avoidance
via `AvoidanceEnabled`/`AvoidanceRadius`; it scans nearby spatial-grid peers,
predicts collision risk over a bounded horizon, and chooses a deterministic
speed-preserving candidate velocity. The named Release target records
`NAVAGENT_CROWD_TARGET: agents=200 frames=180 update_us=564686
min_pair_distance=1.142 crossed=170`.

**Change.**

- Implement navmesh auto-generation from arbitrary geometry (voxelize → walkable
  regions → contour → mesh; from-scratch Recast-style).
- Implement tiled/streamable navmesh aligned to §5 cells, real tile ownership,
  exact polygon-footprint carving, and eventually clipped sub-polygon carving.
- Extend off-mesh link metadata into traversal animation/state-machine hooks and
  replace the current portal-width gate with full `agent_radius`
  polygon/corridor erosion.
- Implement local avoidance (RVO-style) and pathfinding acceleration (spatial
  find-tri landed; path cache and time-sliced/hierarchical A* remain future
  refinements), parallel via §1.

**Tests.** Autogen vs. hand-baked equivalence; tile rebuild on obstacle change;
avoidance interpenetration bounds; agent-count perf fixture.

---

## 10. Animation depth (IK, additive, blend trees, retargeting)

**Current facts.** 4-bone skinning, 256 bones; state machine + crossfade + root
motion present. Existing `PlayLayer` remains a masked replace-blend for
compatibility, while `PlayLayerAdditive` and `CrossfadeLayerAdditive` now perform
true reference-pose subtraction: `(overlayPose - bindPose) * weight` is composed
over the current base pose for masked bones. `BlendTree3D` now provides 1D/2D parametric
blendspaces over `AnimBlend3D`, and `Canvas3D.DrawMeshBlended` accepts the tree
directly. `AnimController3D.SetBlendTree` and
`Game3D.Animator3D.setBlendTree` now let a tree drive the controller base pose
before overlays are composed. `SetAnimationLOD` now provides deterministic
update-rate throttling for distant/low-priority controllers.
`Animation3D.Retarget` now copies compatible local-space channels by bone name
first and by index fallback. `IKSolver3D` now provides two-bone, look-at, and
FABRIK baseline solvers with target/weight controls, controller binding through
`AnimController3D.SetIKSolver`, and the `Game3D.Animator3D.setIKSolver` wrapper.
The open-world slice now covers imported skinned glTF play/crossfade plus
LookAt IK binding as a software visual sample, and validates a terrain-sampled
two-bone foot target through the same CTest. The same slice also loads committed
GLB and WAV package-asset fixtures through `Assets3D.LoadModelAsset` and
`Sound3D.loadAsset`. Humanoid/proportional retarget
mapping, pole-vector control, terrain-aware foot orientation, and a visible
foot-planted skinned character sample remain.

**Change.**

- Extend IK beyond the baseline: pole vectors, terrain foot orientation, richer
  aim constraints, and IK-specific character visual samples.
- Extend true additive layers with authored additive samples and visual coverage.
- Implement parametric blend trees / 1D-2D blendspaces over `AnimBlend3D`.
- Implement cross-skeleton retargeting (humanoid bone mapping).
- Add animation LOD (rate/bone-count reduction by §3 distance).

**Tests.** IK target-error bounds on slope/stairs + look-at; additive vs.
replace correctness; retarget visual check; anim-LOD parity within tolerance.

---

## 11. Asset pipeline depth

**Current facts.** Textures decode to raw RGBA8 only (no BC/DXT/ASTC/ETC2/KTX2);
glTF imports the active scene, secondary scene roots, and scene-local camera
nodes (`assets/rt_gltf.c`; it also imports `KHR_lights_punctual` and several
material KHR extensions).
`Model3D` now has immutable scene-indexed public APIs
(`SceneCount`, `GetCameraCount`, `GetCamera`, `GetSceneName`,
`InstantiateSceneAt`) with active/default and secondary glTF scene behavior
pinned by ctests.
No Draco/meshopt; OBJ ignores `.mtl` (geometry-only import,
`render/rt_mesh3d.c:1172`; `mtllib`/`usemtl` skipped at `:2175`).

**Change.**

- Implement GPU texture compression upload (BC/DXT desktop, ASTC/ETC2 where
  supported) with a software-decode reference; capability-gate per backend.
- Implement KTX2/precompressed block loading and streaming mip residency via §4.
  Support files whose payload already matches backend-supported block formats
  and fall back to software RGBA when needed.
- Add glTF camera import + explicit multi-scene queries/instantiation. Keep
  cached `Model3D` assets immutable: do not add a mutable `SelectScene`; add
  scene-indexed camera access and `Model3D.InstantiateSceneAt(index)` instead.
  Current slices import scene-local perspective/orthographic cameras and
  secondary glTF scene roots.
- Extend existing `Material3D.SetTexture` / `SetAlbedoMap` / `SetNormalMap` /
  `SetSpecularMap` / `SetEmissiveMap` validation to accept
  `Viper.Graphics3D.TextureAsset3D`; do not add duplicate `Set*Asset` methods.
  Current slices are implemented for KTX2 metadata, declared mip-range byte
  telemetry through `SetResidentMipRange`, RGBA8 fallback binding, BC3/BC7
  software fallbacks, representative ETC2 RGBA8/EAC fallback, and ASTC LDR
  void-extent fallback. Precompressed BC3/BC7/ASTC/ETC2 KTX2 payloads are
  retained as native mip blocks with block dimensions/bytes for backend upload
  wiring. Materials retain accepted `TextureAsset3D` sources and resolve the
  active RGBA8 fallback during draw submission, so later `SetResidentMipRange`
  calls affect already-bound materials; unsupported ETC2/ASTC blocks expose
  format/resolution/mip metadata, residency byte counts, and native mip payload
  views, but must wait for backend upload support before material binding.
- Treat Basis-universal supercompression transcode, Draco decode, and meshopt
  decode as Phase 11b/stretch import-depth work, not Phase 12 blockers.
- Backlog: OBJ `.mtl` handling.

**Tests.** Compressed vs. raw within tolerance per backend; KTX2/precompressed
load; glTF camera + secondary-scene import; recorded VRAM reduction. Optional
Phase 11b tests cover Basis/Draco/meshopt when those decoders land.

---

## 12. Vertical-slice and tooling hooks

- Add runtime inspection hooks the ViperIDE level editor will consume: cell/tile
  enumeration, residency state, navmesh bake trigger, and perf counters
  (`World3D` getters; internal `rt_*` introspection). Editor built elsewhere.
- Ensure the slice's systems all expose capability strings and degrade safely.

**Tests.** Slice smoke + deterministic replay + software visual baseline; perf
baselines recorded per reference platform.

Implemented slice: `examples/3d/openworld_slice/` now provides a software-backed
vertical-slice smoke project with `WorldStream3D` cell and heightmapped terrain
manifests, resident `Terrain3D` tile access, `AssetHandle3D` async model completion,
visible RGBA8/BC7 KTX2 `TextureAsset3D` fixture usage, character/physics/nav-agent
stepping, imported skinned glTF play/crossfade plus LookAt IK binding,
committed GLB/WAV fixture loading, visible terrain-sampled TwoBone foot IK proof, committed
software final-frame baseline comparison, capability-gated GPU backend smoke,
all-four-quadrant bounded-residency traversal, software frame-loop perf
telemetry through `g3d_openworld_slice_perf_probe` and reusable
`g3d_openworld_slice_perf_harness` metric parsing,
package dry-run, and two-run deterministic replay through
`g3d_openworld_slice_probe`. `examples/3d/openworld_slice/baselines/perf_macos_apple_m4_max.md`
records the current named local Release software and Metal measurements. `WorldStream3D`
also exposes editor/debug inspection hooks for parsed cell and terrain-tile
counts, names, terrain heightmap sidecar paths, centers, resident flags, and
byte estimates via lower/camel `getCell*` and `getTerrainTile*` methods.
`World3D` now exposes lower/camel
runtime counters for `entityCount`, `bodyCount`, `drawCount`,
`visibleNodeCount`, `occludedDrawCount`, and `streamResidentBytes`, with
`Canvas3D.DrawCount` providing the underlying draw-submission counter. The
world-scoped `bakeNavMesh` and `bakeTiledNavMesh` hooks wrap the current
`Scene3D` into the existing `NavMesh3D` bakers so editor workflows can trigger a
navigation bake from one Game3D handle.

---

## Validation for every runtime change

- `./scripts/check_runtime_completeness.sh`.
- Build + `ctest --test-dir build -L graphics3d` green on macOS, Windows, Linux
  (CO-1); no macOS-only paths.
- `./scripts/lint_platform_policy.sh` + `./scripts/run_cross_platform_smoke.sh`
  for platform-sensitive work.
- A before/after performance number on a committed Phase-0 fixture for any scale
  feature.
- `runFrames` worker-count parity plus VM/native determinism parity for any
  simulation-touching change. Rerun the NL3-031 focused gate:
  `g3d_3dnext2_surface_probe`, `test_rt_game3d`,
  `test_codegen_env_is_native`, the native-run Zia promise tests, and
  `test_crosslayer_arith`.
- A software-backend correctness path for any new visual feature; GPU parity
  capability-gated + smoke-tested.
- Each new public runtime function/class has a success-path and a negative/
  capability ctest, plus class-id/`runtime.def` completeness for new classes, or
  a named waiver in `progress/06-waivers.md`.
