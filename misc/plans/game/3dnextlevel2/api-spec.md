# Scale tier — runtime API specification

> Draft. New/changed runtime-backed surface for `Viper.Graphics3D` and
> `Viper.Game3D`, registered through `runtime.def`. Zia blocks show the intended
> consumer shape. Much of this plan is **internal C** (job system, spatial index,
> solver, baker, transcoder, floating-origin); those expose only small control/
> capability getters. Signatures are proposals to confirm in each phase's spike.

## Conventions

- **Opt-in + capability-gated.** Every system is off by default and queryable;
  with flags off, behavior equals today's bounded-scene path. Use
  `Canvas3D.BackendSupports(name)` and new `World3D` capability getters before
  enabling optional paths.
- **Viper naming.** Public class names are fully qualified in this spec and
  follow existing runtime style: `Viper.Game3D.WorldStream3D`,
  `Viper.Game3D.AssetHandle3D`, `Viper.Graphics3D.IKSolver3D`,
  `Viper.Graphics3D.SixDofJoint3D`. Low-level `Viper.Graphics3D` methods and
  properties stay PascalCase (`QueryAABB`, `ContactCount`). Stateful
  `Viper.Game3D` objects follow the existing ergonomic lower/camel style
  (`World3D.tick`, `Entity3D.worldPosition`, `BodyDef.get_restitution`), while
  static Game3D helper namespaces keep PascalCase factories (`Assets3D.LoadModel`,
  `Prefab.Box`). Properties surface to Zia/BASIC through existing `get_`/`set_`
  bindings.
- **New class registrations.** Any new public class must add an `RT_CLASS_BEGIN`
  entry, a permanent appended class-id sentinel in `rt_graphics3d_ids.h`, real +
  disabled-graphics stubs, docs, and ctests before the API row can be marked
  done. Never renumber existing `RT_G3D_*_CLASS_ID` values.
- **Determinism first.** Simulation stays deterministic; APIs that offload work
  do so transparently and never change `runFrames` results.
- **Escape hatches preserved.** All existing raw `Viper.Graphics3D.*` access
  remains; new wrappers never hide the primitive.
- **Errors** trap as `Game3D.<Type>.<method>: <reason>` /
  `Graphics3D.<Type>.<method>: <reason>`; no duplicated deep validation.
- **Units/angles/colors/time** unchanged from `3Dnextlevel/api-spec.md`.

## Carryover surface (Phase C)

```zia
// CO-4 render-target finalization is already represented by existing methods:
// Canvas3D.FinalizeFrame(), ScreenshotFinal(), and BackendSupports(name).
// Backends that cannot finalize render targets must report false for
// BackendSupports("rt-finalize") and trap before mutation with a clear message.
// CO-5 implicit fallback lighting decision: no automatic fallback lights.
// Canvas3D.SetDefaultLighting() remains explicit opt-in setup; ClearLights() plus
// zero ambient stays dark.
// CO-6 per-light shadow control
expose Boolean Light3D.CastsShadows;        // get/set; skip non-casters in shadow pass
expose Boolean Material3D.HasTexture;       // read; base-color/albedo slot
expose Boolean Material3D.HasNormalMap;     // read
expose Boolean Material3D.HasSpecularMap;   // read
expose Boolean Material3D.HasEmissiveMap;   // read
expose Boolean Material3D.HasMetallicRoughnessMap; // read
expose Boolean Material3D.HasAOMap;         // read
expose Boolean Material3D.HasEnvMap;        // read
// CO-12 import bridge exists and remains canonical:
// Entity3D.FromNode(root: SceneNode3D) wraps a raw hierarchy as a group entity.
// CO-3 diagnostics surface is behaviour, not new methods: destroyed/double-despawn
// traps use Game3D.<Type>.<method>: <reason>; missing asset handles expose
// AssetHandle3D.error; capability fallback reasons are inspectable.
```

## Concurrency control — `Viper.Game3D.World3D` (mostly internal)

```zia
expose Integer World3D.workerCount;          // read; configured at New or via env
expose Boolean World3D.jobsEnabled;          // read; true when workerCount > 1
expose func World3D.setWorkerCount(n: Integer)
expose Integer World3D.entityCount;          // editor/perf counter
expose Integer World3D.bodyCount;            // editor/perf counter
expose Integer World3D.drawCount;            // latest main 3D draw submissions
expose Integer World3D.visibleNodeCount;     // latest Scene3D draw telemetry
expose Integer World3D.occludedDrawCount;    // latest Canvas3D visibility telemetry
expose Integer World3D.streamResidentBytes;  // owned stream residency bytes
```
The job/parallel-for/commit-queue primitives are internal C (`rt_job_*`); game
code does not author jobs directly in this milestone.

Implemented slice: `World3D` now owns a lazy internal worker pool when
`workerCount > 1`; `stepSimulation` uses it for eligible animator batches while
preserving single-worker vs. multi-worker root-motion, state-time, and event
parity. `World3D` also exposes lower/camel aggregate counters for editor/perf
inspection over its entity registry, physics body list, latest render telemetry,
and owned stream residency. The generic `rt_job_*`/commit-queue layer remains
planned for async loading and GPU upload.

## Large coordinates — `Viper.Game3D.World3D` / `Viper.Graphics3D.Scene3D`

```zia
expose Boolean World3D.floatingOrigin;       // get/set; enable origin rebasing
expose Vec3 World3D.worldOrigin;             // current rebase offset (read)
expose func World3D.setOriginRebaseThreshold(meters: Float)
expose func World3D.rebaseOrigin(dx: Float, dy: Float, dz: Float)
expose func Scene3D.RebaseOrigin(dx: Float, dy: Float, dz: Float)
expose func Physics3DWorld.RebaseOrigin(dx: Float, dy: Float, dz: Float)
expose func Particles3D.RebaseOrigin(dx: Float, dy: Float, dz: Float)
expose func Sprite3D.RebaseOrigin(dx: Float, dy: Float, dz: Float)
```

`World3D.floatingOrigin` is also the internal capability gate for
camera-relative Canvas3D upload during `World3D.beginFrame`: camera frame
payloads, double `DrawMesh` model matrices, and point/spot lights are narrowed
to backend floats after subtracting the active camera origin. Programmatic
`Mesh3D.AddVertex` positions preserve double precision for identity-matrix raw
mesh draws, and generated `Particles3D` / `Sprite3D` / decal billboard vertices
are built camera-relative before upload. The flag-off path remains the
bounded-scene absolute upload path. `World3D.rebaseOrigin` uses the same
between-frames cross-system boundary as the automatic threshold rebase; it
shifts scene roots, physics bodies and contact/query state, camera, explicit
listener, and world-owned effects together. Existing query/event objects are
pre-rebase snapshots, so callers should query again after the boundary.

## Spatial queries — `Viper.Graphics3D.Scene3D`

```zia
// index-backed; results match the flat-walk reference
expose func Scene3D.QueryAABB(min: Vec3, max: Vec3) -> Seq[SceneNode3D]
expose func Scene3D.QuerySphere(center: Vec3, radius: Float) -> Seq[SceneNode3D]
expose func Scene3D.RaycastNodes(origin: Vec3, dir: Vec3, maxDist: Float) -> SceneNode3D?
expose Integer Scene3D.VisibleNodeCount;     // last-frame, for Debug3D/telemetry
```
These methods are backed by the internal Scene3D BVH spatial index with a
deterministic flat-walk fallback for parity testing. Transform-only changes
refit the existing BVH; hierarchy, visibility, mesh, LOD, and impostor changes
rebuild it lazily. They skip hidden subtrees and return visible nodes with their
own mesh bounds. Cull substitution is internal. Transformed spatial bounds are
stored and tested in double precision, so far-origin query/raycast candidate
selection does not collapse nodes that are closer together than
single-precision world-space granularity. A generated 10k Scene3D fixture now
guards BVH shape, candidate reduction for isolated AABB queries, and indexed
draw culling. Physics shares the finite-AABB query semantics but keeps a
separate body-centric broadphase because solver pair generation must include
non-render bodies, layer/mask filtering, static-static rejection, trigger state,
and contact-event identity.

## Async assets — `Viper.Game3D.Assets3D`

```zia
class Viper.Game3D.AssetHandle3D {
    expose Boolean ready;
    expose Float progress;                    // 0.0..1.0 best-effort
    expose String error;                      // empty unless terminal error
    expose func getEntity() -> Entity3D?      // null until ready or template-only
    expose func getTemplate() -> ModelTemplate? // null until ready or entity-only
    expose func cancel()
}
expose func Assets3D.LoadModelAsync(path: String) -> AssetHandle3D
expose func Assets3D.LoadModelAssetAsync(assetPath: String) -> AssetHandle3D
expose func Assets3D.LoadModelTemplateAsync(path: String) -> AssetHandle3D
expose func Assets3D.LoadModelTemplateAssetAsync(assetPath: String) -> AssetHandle3D
expose func Assets3D.SetResidencyBudget(bytes: Integer)
expose func Assets3D.GetResidentBytes() -> Integer
expose func Assets3D.SetResidencyHint(template: ModelTemplate, priority: Float, distance: Float)
expose func Assets3D.SetUploadBudget(bytes: Integer)
expose func Assets3D.Evict(handle: AssetHandle3D)
expose func Assets3D.Preload(path: String)
expose func Assets3D.PreloadAsset(assetPath: String)
```
Deferred-handle baseline is implemented for all four `LoadModel*Async` entry
points. Handles start pending with `progress == 0.0`; first observation services
the asset commit queue and starts worker staging/loading for valid uncached requests.
Ready handles preserve the same entity/template result shape as their blocking
counterparts. `cancel()` before worker completion produces a terminal cancelled
handle with no result; completed-handle cancel remains a stable no-op.
glTF/GLB requests now stage root bytes plus external, data URI, and
bufferView-backed buffer/image payloads on a worker and build runtime objects
on the main-thread commit queue; PNG, BMP, JPEG, and GIF image payloads are decoded there
into raw RGBA POD before bounded commit-side `Pixels` preparation, and static/skinned/morph-target
triangle-list, triangle-strip, and triangle-fan mesh primitives with positions,
optional normals, sparse accessor overrides, and JOINTS/WEIGHTS attributes are
decoded into raw `Mesh3D` POD before commit-side mesh object creation. Missing
normals are regenerated during commit, and skin joint remapping still runs after
skeleton import; morph-target delta POD is attached as `MorphTarget3D` during
commit. Non-glTF formats still enqueue a
main-thread load. `Assets3D.Preload(path)` and `Assets3D.PreloadAsset(assetPath)` now start
the filesystem and package-aware template async paths immediately and publish
cache-warm results through the same world-drained commit queue.
`SetResidencyBudget`, `GetResidentBytes`, `SetResidencyHint`, and `Evict` are
implemented over the shared `ModelTemplate` cache. Cache byte estimates include
mesh buffers, model metadata, and decoded material texture pixels, so
texture-heavy templates obey the same budget as geometry-heavy templates.
Eviction considers explicit residency hints before falling back to LRU:
higher-priority templates survive lower-priority templates, nearer templates
survive farther templates when priority ties, and blocking/async load-clear
churn can prove resident template bytes return to zero.
`SetUploadBudget` is implemented over the async asset commit queue; staged glTF
image payloads contribute their decoded RGBA byte count to commit-slice cost,
`0` pauses positive-cost decoded texture slices, positive budgets advance large
decoded images across multiple drains, and negative restores unlimited upload drains.
`Canvas3D.SetTextureUploadBudget` caps Metal/OpenGL/D3D11 Pixels-backed 2D
material texture/cubemap row slices and native compressed `TextureAsset3D` mip
block submissions. Negative means unlimited, `0` pauses new uploads, and
positive budgets advance rows or resident native mips while reporting queued
material/cubemap row bytes and native mip bytes through
`Canvas3D.TextureUploadPendingBytes`. `Canvas3D.TextureUploadBytes` reports the
actual texture payload bytes uploaded into backend storage by the latest ended
frame, including material/cubemap row slices and native compressed mip blocks;
cache hits and software fallback report `0`.
`ClearCache` advances the template-cache generation so preload jobs that started
before the clear cannot repopulate the cache after they finish.
Missing filesystem
paths and missing asset-manager paths complete as terminal non-cancel error
handles (`"cannot read file"` / `"asset not found"`) instead of trapping on
observation. Unsupported model extensions, malformed glTF JSON, and corrupt
required PNG/BMP/JPEG/GIF texture payloads also complete as terminal handle
errors. The worker preload now also validates required buffer payloads, accessor
byte ranges, and corrupt required texture payloads before the commit-side model
build. Blocking glTF loads reject matching corrupt required data-URI texture
payloads instead of silently dropping material maps.
`g3d_openworld_slice_streaming_hitch_probe` records blocking-vs-async timing and
verifies zero-upload-budget pending/release behavior. Cubemap and native-compressed
uploads now share the Canvas3D backend byte budget/telemetry path, and shared
backend pending-byte helpers prove row/native queues return to zero after final
slices drain. The named native-compressed hitch rerun is now covered by
`g3d_openworld_slice_streaming_hitch_native_compressed_probe`, which records
the platform GPU backend, compressed format, zero-budget pending bytes, release
time, upload bytes, raw RGBA bytes, compressed resident bytes, RAM/VRAM
reduction percentages, and final-frame tolerance.
Existing synchronous `LoadModel` stays; `Preload` / `PreloadAsset` are now
background template warms and will move to the full POD loader as that importer
split lands.

## World streaming — `Viper.Game3D.WorldStream3D` (new)

```zia
class Viper.Game3D.WorldStream3D {
    static func New(world: World3D) -> WorldStream3D
    expose Integer residentCellCount;
    expose Integer residentTerrainTileCount;
    expose Integer pendingRequestCount;
    expose Integer residentBytes;
    expose func setCenter(pos: Vec3)             // usually the player
    expose func setRadii(loadRadius: Float, unloadRadius: Float)
    expose func setResidencyBudget(bytes: Integer)
    expose func mountTiledTerrain(manifestPath: String)   // VSCN streaming manifest
    expose func mountCells(manifestPath: String)           // scene cells
    expose func getResidentTerrainTile(index: Integer) -> Terrain3D?
    expose func getCellCount() -> Integer
    expose func getCellName(index: Integer) -> String
    expose func getCellCenter(index: Integer) -> Vec3?
    expose func getCellResident(index: Integer) -> Boolean
    expose func getCellBytes(index: Integer) -> Integer
    expose func getTerrainTileCount() -> Integer
    expose func getTerrainTileName(index: Integer) -> String
    expose func getTerrainTileCenter(index: Integer) -> Vec3?
    expose func getTerrainTileResident(index: Integer) -> Boolean
    expose func getTerrainTileBytes(index: Integer) -> Integer
    expose func update(dt: Float)                // streams in/out around center
}
```
`Terrain3D` gains tile-aware construction. The streamed container is a VSCN
streaming manifest/extension with optional binary sidecars, not a new general
scene format. `World3D.stream` is the lazily-created world-owned
`WorldStream3D` for normal game code.

Implemented slice: `WorldStream3D` exists as a Game3D lower/camel runtime class
with `New`, center/radius/budget setters, manifest mount points, `update`, and
resident telemetry. `mountCells` parses JSON/VSCN manifests shaped as
`{"cells":[{"name":"cell_00","path":"cell_00.vscn","center":[x,y,z],"radius":r,"bytes":n}]}`
and loads/unloads resident `.vscn` scene subtrees around `setCenter`.
Resident cells now report measured scene-resource residency after load,
including authored base meshes, resident LOD meshes, impostor meshes, materials,
and resident material textures. Loaded cells remeasure after mesh/LOD residency
changes so detail can be demoted without unloading the whole cell; unloaded
cells report the manifest `bytes` estimate.
`mountTiledTerrain` parses `tiles[]` manifests with the same metadata shape plus
optional `width`, `depth`, `scale`, and `heightmap`, instantiates `Terrain3D`
tile payloads for resident tiles, applies `viper-heightmap-v1` normalized
height sidecars through the existing terrain heightmap path, spawns matching
static heightfield collider entities for resident tiles, renders resident tiles
through `World3D.drawScene`, stitches full adjacent resident tile edges in
world-height space before terrain LOD meshes are drawn, contributes hidden
terrain nav-bake source nodes to the owning world scene, and exposes terrain
payloads through
`getResidentTerrainTile(index)`. `WorldStream3D.update(dt)` now applies a
deterministic per-update load budget and reports deferred desired payloads
through `pendingRequestCount`. Phase 12 editor/debug hooks expose parsed cell
and terrain-tile counts, names, centers, resident flags, and byte estimates
through lower/camel `getCell*` and `getTerrainTile*` methods, including
`getTerrainTileHeightmap(index)` for the resolved sidecar path. The same
manifest entries now parse authored `material`, `layer` / `collisionLayer`,
`collisionMask` or nested `collision`, `navArea`, `traversalCost`, and optional
`sidecar` / `binarySidecar` metadata. Cell layer/mask metadata is applied to
the spawned root entity, terrain collision metadata is applied to generated
heightfield collider bodies, and typed inspection getters expose the parsed
values. Worker-backed payload decode/upload and streamed nav bake/export tooling
remain Phase 5 runtime work; runtime retained tile rebuilds are covered by
`NavMesh3D.BakeTiled` / `RebuildTile`.
`World3D.stream` now returns a stable world-owned stream handle;
`WorldStream3D.New(world)` remains available for separate controllers.

## Visibility — `Viper.Graphics3D.Canvas3D` / `Scene3D`

```zia
expose func Canvas3D.SetOcclusionCulling(on: Boolean)   // frustum + conservative CPU occlusion
expose func Canvas3D.SetFrustumCulling(on: Boolean)     // frustum-only
expose func Canvas3D.BackendSupports(name: String) -> Boolean // + "occlusion", "hlod"
expose func Scene3D.AddVisibilityZone(name: String, min: Vec3, max: Vec3) -> Integer
expose func Scene3D.AddVisibilityPortal(from: Integer, to: Integer, bidirectional: Boolean) -> Integer
expose Integer Scene3D.PvsCulledCount
expose Integer Scene3D.VisibilityZoneCount
expose Integer Scene3D.VisibilityPortalCount
// auto-LOD on SceneNode3D (extends existing manual LOD)
expose Boolean Mesh3D.Resident
expose Integer Mesh3D.ResidentBytes
expose func SceneNode3D.SetAutoLOD(on: Boolean, screenErrorPx: Float)
expose func SceneNode3D.SetLodResident(index: Integer, on: Boolean)
expose func SceneNode3D.GetLodResident(index: Integer) -> Boolean
expose func SceneNode3D.GetLodResidentBytes(index: Integer) -> Integer
expose func SceneNode3D.SetImpostor(distance: Float, pixels: Object)   // HLOD/impostor
expose Integer Canvas3D.DrawCount;                      // telemetry
expose Integer Canvas3D.OccludedDrawCount;              // telemetry
expose Integer Canvas3D.OcclusionCandidateCount;        // CPU occlusion grid workload telemetry
expose Integer Canvas3D.TextureUploadBytes;             // backend upload telemetry
expose Integer Canvas3D.TextureUploadPendingBytes;      // queued material texture row bytes
expose func Canvas3D.SetTextureUploadBudget(bytes: Integer)
```

Implemented slice: `SetOcclusionCulling` enables frustum rejection plus a
conservative low-resolution CPU coverage/depth grid; Scene3D draws feed that
grid from the BVH spatial candidate set before Canvas3D opaque sorting, while
raw Canvas3D draws keep the sorted-queue fallback. `SetFrustumCulling` remains
frustum-only. `SetAutoLOD` selects among authored resident `AddLOD` meshes by
projected screen size, `SetLodResident` marks the underlying LOD mesh payload
resident/nonresident for draw selection and byte accounting, `SetImpostor`
builds a retained unlit textured quad proxy, `BackendSupports("occlusion")` and
`BackendSupports("hlod")` report those runtime paths, `OccludedDrawCount`
mirrors the latest visibility skip count, and `OcclusionCandidateCount` reports
CPU grid workload. `Scene3D.AddVisibilityZone` / `AddVisibilityPortal` add an
authored interior PVS graph; `Draw` skips drawables inside unreachable zones,
keeps unzoned drawables visible, and reports `PvsCulledCount`. GPU occlusion
queries and Hi-Z are still backend acceleration work. The named
`openworld_slice` dense visibility probe records the authored city/forest
draw/fill-proxy reduction and software final-frame parity proof for this API
surface.

## Lighting — `Viper.Graphics3D.Canvas3D` / `Light3D` / `PostFX`

```zia
expose func Canvas3D.SetClusteredLighting(on: Boolean)  // forward+ many-light path
expose Integer Canvas3D.MaxActiveLights;                // raised when clustered+supported
expose func Canvas3D.SetShadowCascades(count: Integer)  // CSM for directional light
expose Boolean Light3D.CastsShadows;                    // CO-6
// Canvas3D.BackendSupports("clustered-lighting"), ("shadow-csm")
```
The current implemented contract is capability-gated: unsupported clustered
lighting leaves `MaxActiveLights == 16`, `BackendSupports("clustered-lighting")`
false, and `SetClusteredLighting(true)` traps before mutation. The software
backend advertises the capability as the correctness baseline and raises
`MaxActiveLights` to the bounded 64-light payload table when enabled. Real
Metal, D3D11, and OpenGL backend vtables advertise the same bounded many-light
path; fake GPU-named test backends do not.
`SetShadowCascades(1)` maps to the non-cascaded shadow-map behavior; counts
above one require `BackendSupports("shadow-csm")` and trap before mutation on
unsupported backends. Supporting software and real platform GPU backends render
the primary directional caster into up to four camera-depth cascades and publish
split metadata to the backend light payload.

## Physics — `Viper.Graphics3D.Physics3DWorld` / joints / `Collider3D`

```zia
expose func Physics3DWorld.SetSolverIterations(n: Integer)   // iterative contact + joint passes
expose Integer Physics3DWorld.SolverIterations;
expose Integer Physics3DWorld.LastSolverIslandCount;         // max active contact islands in latest Step
expose Integer Physics3DWorld.LastSolverActiveBodyCount;     // max awake dynamic bodies in latest island batch
expose Integer Physics3DWorld.LastSolverContactCount;        // max non-trigger contacts in latest island batch
expose Integer CollisionEvent3D.ContactCount;                // existing API now >1
expose func CollisionEvent3D.GetContact(i: Integer) -> ContactPoint3D
expose Integer Game3D.Collision3DEvent.contactCount;         // wrapper mirrors raw event
expose func Game3D.Collision3DEvent.contactPoint(i: Integer) -> Vec3
expose func Game3D.Collision3DEvent.contactNormal(i: Integer) -> Vec3
expose func Game3D.Collision3DEvent.contactSeparation(i: Integer) -> Float
// new joints (close hinge/rope doc overclaim + add 6DOF)
class Viper.Graphics3D.HingeJoint3D {
    static func New(a: Physics3DBody, b: Physics3DBody,
                    anchor: Vec3, axis: Vec3) -> HingeJoint3D
}
class Viper.Graphics3D.RopeJoint3D {
    static func New(a: Physics3DBody, b: Physics3DBody,
                    maxLength: Float) -> RopeJoint3D
}
class Viper.Graphics3D.SixDofJoint3D {
    static func New(a: Physics3DBody, b: Physics3DBody,
                    frameA: Mat4, frameB: Mat4) -> SixDofJoint3D
    expose func SetLinearLimits(min: Vec3, max: Vec3)
    expose func SetAngularLimits(min: Vec3, max: Vec3)
}
// Collider3D.NewConvexHull uses GJK/EPA for hull-vs-hull and hull-vs-simple pairs
// (sphere/capsule/box), including separated-overlapping-AABB edge cases (no API change).
```

`SetSolverIterations` is now live for contact and joint passes. Awake,
non-trigger contacts are scheduled through independent contact islands, and the
latest `Step` exposes island/body/contact maxima through the solver telemetry
properties above. The named `PHYSICS_ISLAND_BATCH_TARGET` fixture records a
257-body / 32-pile resting target with first-step and settle timings.
AABB-vs-AABB events and rotated box face contacts expose bounded multi-point
manifolds; edge-style box contacts and other shapes still report one
representative contact. Convex-hull pairs now use a simplex/Minkowski narrow
phase rather than AABB fallback.
Mesh-vs-sphere/capsule/box/convex-hull contacts reuse the per-mesh BVH for
candidate triangle pruning and keep the previous full scan as a fallback when
the BVH path is unavailable. The body-centric physics broadphase chooses body
pairs before any per-mesh BVH traversal; this has no public API surface.
`AddJoint(joint, type)` now accepts `RT_JOINT_DISTANCE 0`,
`RT_JOINT_SPRING 1`, `RT_JOINT_HINGE 2`, `RT_JOINT_ROPE 3`, and
`RT_JOINT_SIXDOF 4`. The runtime type constants are `RT_JOINT_*`, not the
stale `RT_JOINT3D_*` spelling older notes used.
SixDof linear limits project frame-anchor separation. Angular limits project
relative joint-frame pose angles, in radians, from the creation relative
orientation and remove angular velocity only for locked axes or motion pushing
an already-limited pose farther out of range.

## Navigation — `Viper.Graphics3D.NavMesh3D` / `NavAgent3D`

```zia
// auto-generation from arbitrary geometry (Recast-style, from scratch)
expose func NavMesh3D.Bake(scene: Scene3D, agentRadius: Float, agentHeight: Float,
                           maxSlope: Float, cellSize: Float) -> NavMesh3D
expose func NavMesh3D.BakeTiled(scene: Scene3D, tileSize: Float, agentRadius: Float,
                                agentHeight: Float, maxSlope: Float,
                                cellSize: Float) -> NavMesh3D
expose func NavMesh3D.RebuildTile(tileX: Integer, tileZ: Integer)   // dynamic carve
expose func NavMesh3D.AddObstacle(min: Vec3, max: Vec3)
expose func NavMesh3D.RemoveObstacle(index: Integer)
expose func NavMesh3D.UpdateObstacle(index: Integer, min: Vec3, max: Vec3)
expose func NavMesh3D.AddOffMeshLink(from: Vec3, to: Vec3, bidirectional: Boolean)
expose func NavMesh3D.SetOffMeshLinkMetadata(index: Integer, kind: String,
                                             cost: Float, state: Integer)
expose func NavMesh3D.SetArea(min: Vec3, max: Vec3, area: String, cost: Float)
expose func NavMesh3D.GetArea(pos: Vec3) -> String
expose func NavMesh3D.GetTraversalCost(pos: Vec3) -> Float
expose Float NavMesh3D.LastPathCost;
// agent local avoidance (RVO-style); agent_radius now applied to corridors
expose Boolean NavAgent3D.AvoidanceEnabled;
expose Float   NavAgent3D.AvoidanceRadius;
expose func World3D.bakeNavMesh(agentRadius: Float, agentHeight: Float,
                                maxSlope: Float, cellSize: Float) -> NavMesh3D
expose func World3D.bakeTiledNavMesh(tileSize: Float, agentRadius: Float,
                                     agentHeight: Float, maxSlope: Float,
                                     cellSize: Float) -> NavMesh3D
```
Existing `NavMesh3D.Build(mesh, ...)` (bake-from-one-mesh) is retained.

Implemented slices: `NavAgent3D.AvoidanceEnabled` and `AvoidanceRadius` are now public `Viper.Graphics3D` PascalCase properties. They provide opt-in same-NavMesh reciprocal velocity-obstacle avoidance during `Update`, scanning nearby spatial-grid peers and selecting a deterministic speed-preserving candidate velocity. `NavMesh3D.Bake(scene, ...)` is public and flattens `Mesh3D` nodes through `Scene3D` world transforms before using the voxel baker. `World3D.bakeNavMesh(...)` and `bakeTiledNavMesh(...)` expose the same bake path as Game3D lower/camel editor hooks over the world's owned scene. `NavMesh3D.BakeTiled(scene, ...)` keeps retained voxel-cell source data per tile, and `RebuildTile(tileX, tileZ)` refreshes only that tile's geometry, heights, and blocked state. `NavMesh3D.AddOffMeshLink(from, to, bidirectional)` and read-only `OffMeshLinkCount` are public; authored endpoints must resolve to current walkable polygons, and pathfinding treats the link as a directed or bidirectional graph edge. `SetOffMeshLinkMetadata(index, kind, cost, state)`, `GetOffMeshLinkKind`, `GetOffMeshLinkTraversalCost`, and `GetOffMeshLinkState` store traversal link kind/cost/state metadata; link costs contribute to A*. `NavMesh3D.AddObstacle(min, max)`, `RemoveObstacle(index)`, `UpdateObstacle(index, min, max)`, and read-only `ObstacleCount` are public; tiled bakes re-carve only overlapped tiles while non-tiled meshes refilter preserved source triangles, and carving now uses exact triangle-footprint overlap against the obstacle volume instead of triangle AABB overlap. `SetArea(min, max, area, cost)`, `GetArea(pos)`, `GetTraversalCost(pos)`, and `LastPathCost` expose polygon area/cost metadata, and A* weights polygon traversal cost. Clipped sub-polygon carving remains a future refinement outside the completed Phase 9 checklist.

## Animation — `Viper.Graphics3D` IK / controller

```zia
class Viper.Graphics3D.IKSolver3D {            // new
    static func TwoBone(skeleton: Skeleton3D, root: Integer,
                        mid: Integer, end: Integer) -> IKSolver3D
    static func LookAt(skeleton: Skeleton3D, bone: Integer) -> IKSolver3D
    static func FABRIK(skeleton: Skeleton3D, chain: Seq[Integer]) -> IKSolver3D
    expose func SetTarget(pos: Vec3)
    expose func SetWeight(w: Float)
    expose func Solve()                        // applied before skinning
}
expose func AnimController3D.SetIKSolver(solver: IKSolver3D)
expose func AnimController3D.PlayLayerAdditive(layer: Integer, state: String)  // TRUE additive
expose func AnimController3D.CrossfadeLayerAdditive(layer: Integer, state: String,
                                                    seconds: Float)
class Viper.Graphics3D.BlendTree3D {           // parametric blend over AnimBlend3D
    static func New1D(skeleton: Skeleton3D) -> BlendTree3D
    static func New2D(skeleton: Skeleton3D) -> BlendTree3D
    expose func AddSample(animation: Animation3D, x: Float, y: Float)
    expose func SetParam(x: Float, y: Float)
    expose func Update(dt: Float)
    expose prop SampleCount: Integer
}
expose func Animation3D.Retarget(animation: Animation3D, srcSkeleton: Skeleton3D,
                                 dstSkeleton: Skeleton3D) -> Animation3D
expose func AnimController3D.SetAnimationLOD(distance: Float, rateHz: Float)
```
`Game3D.Animator3D` gains thin `setIKSolver`, `setBlendTree`,
`playLayerAdditive`, and `crossfadeLayerAdditive` wrappers.

Implemented slice: `AnimController3D.PlayLayerAdditive(layer, state)` and
`CrossfadeLayerAdditive(layer, state, seconds)` are now public and compose the
state as a true bind-pose delta over the current base pose. Existing `PlayLayer`
and `CrossfadeLayer` remain masked replace overlays for compatibility.
`Game3D.Animator3D.playLayerAdditive(layer, state)` and
`crossfadeLayerAdditive(layer, state, seconds)` forward to the same behavior
through the code-first Game3D wrapper and preserve layer entry events.
`AnimController3D.SetAnimationLOD(distance, rateHz)` is implemented as deterministic
update-rate throttling: elapsed time accumulates between samples and is applied in
batches at `rateHz`; non-positive inputs disable the throttle.
`BlendTree3D.New1D/New2D`, `AddSample`, `SetParam`, `Update`, and `SampleCount`
are implemented as a parametric layer over `AnimBlend3D`; `Canvas3D.DrawMeshBlended`
now accepts either the raw blender or a blend tree. `AnimController3D.SetBlendTree`
and `Game3D.Animator3D.setBlendTree` are implemented as the controller-bound path:
the tree supplies the base pose before overlay layers are composed.
`IKSolver3D.TwoBone/LookAt/FABRIK`, `SetTarget`, `SetWeight`, and `Solve` are
implemented with `AnimController3D.SetIKSolver` and `Game3D.Animator3D.setIKSolver`
as the controller-bound path. The baseline covers positional two-bone/FABRIK
chains, local +Z look-at aiming, target-weight blending, and same-skeleton
validation. Imported skinned glTF play/crossfade plus LookAt IK binding and a
terrain-sampled TwoBone foot target are covered by `examples/3d/openworld_slice/`.
Pole vectors, foot-orientation constraints, and a visible foot-planted skinned
character sample remain future refinement.
`Animation3D.Retarget(animation, srcSkeleton, dstSkeleton)` is implemented as a
deterministic name-first, index-fallback channel copy that preserves clip
metadata; full humanoid/proportional mapping remains future work.

## Asset pipeline — textures / glTF

```zia
class Viper.Graphics3D.TextureAsset3D {
    static func LoadKTX2(path: String) -> TextureAsset3D
    static func LoadKTX2Asset(assetPath: String) -> TextureAsset3D
    expose Integer Width;
    expose Integer Height;
    expose Integer MipCount;
    expose String Format;                      // "bc7", "bc3", "astc", "etc2", "rgba8"
    expose Boolean Compressed;
    expose Integer ResidentMipStart;
    expose Integer ResidentMipCount;
    expose Integer ResidentBytes;
    func SetResidentMipRange(firstMip: Integer, mipCount: Integer);
}
// Extend the existing material texture methods to accept either Pixels or
// TextureAsset3D instead of adding duplicate Set*Asset methods.
expose func Material3D.SetTexture(texture: TextureAsset3D)
expose func Material3D.SetAlbedoMap(texture: TextureAsset3D)
expose func Material3D.SetNormalMap(texture: TextureAsset3D)
expose func Material3D.SetSpecularMap(texture: TextureAsset3D)
expose func Material3D.SetEmissiveMap(texture: TextureAsset3D)
expose func Canvas3D.BackendSupports(name: String) -> Boolean // + "bc7", "astc", "etc2"
expose func Model3D.GetCameraCount(sceneIndex: Integer) -> Integer     // glTF camera import
expose func Model3D.GetCamera(sceneIndex: Integer, i: Integer) -> Camera3D?
expose Integer Model3D.SceneCount;
expose func Model3D.GetSceneName(index: Integer) -> String
expose func Model3D.InstantiateSceneAt(index: Integer) -> Scene3D? // no mutable SelectScene
// Draco/meshopt handled transparently inside the glTF loader when optional Phase 11b lands.
```

Implemented slice: `TextureAsset3D.LoadKTX2`, `LoadKTX2Asset`, `Width`,
`Height`, `MipCount`, `Format`, `Compressed`, `ResidentMipStart`,
`ResidentMipCount`, `ResidentBytes`, and `SetResidentMipRange(firstMip,
mipCount)` are registered as `Viper.Graphics3D.TextureAsset3D` with appended
class id `RT_G3D_TEXTUREASSET3D_CLASS_ID`. The loader parses KTX2 metadata,
records declared mip payload byte ranges for residency telemetry, retains
precompressed native mip block payloads for backend upload, and decodes RGBA8,
BC3, BC7 modes 0-7, representative ETC2 RGBA8/EAC, and ASTC LDR void-extent
mips into CPU `Pixels` fallbacks. `SetResidentMipRange` switches the active
fallback to the first resident mip while updating telemetry. Existing material
texture methods accept `TextureAsset3D` when a fallback or native compressed mip
blocks exist, retain the asset source, and resolve the currently resident
fallback/native source at draw time. `Canvas3D.BackendSupports("bc7"/"astc"/"etc2")`
now reports the active backend/device native upload capabilities.

Implemented slice: `Model3D.SceneCount`, `GetCameraCount(sceneIndex)`,
`GetCamera(sceneIndex, i)`, `GetSceneName(index)`, and
`InstantiateSceneAt(index)` are registered as `Viper.Graphics3D.Model3D`
PascalCase APIs with ABI and ctest coverage. glTF imports order immutable scenes
with the active/default scene at index `0`, preserve authored secondary scene
names, instantiate secondary scene roots independently, and import scene-local
perspective/orthographic cameras as `Camera3D` handles.

## Hello (open-world) — target consumer shape

```zia
bind Viper.Game3D as Game3D;

func start() {
    var world = Game3D.World3D.New("Open World", 1280, 720);
    Game3D.World3D.set_floatingOrigin(world, true);
    var stream = Game3D.WorldStream3D.New(world);
    Game3D.WorldStream3D.mountTiledTerrain(stream, "assets/world/terrain.vscn");
    Game3D.WorldStream3D.mountCells(stream, "assets/world/cells.vscn");
    Game3D.WorldStream3D.setRadii(stream, 256.0, 320.0);

    var player = Game3D.Assets3D.LoadModel("assets/char/hero.glb");
    Game3D.World3D.spawn(world, player);
    var fps = Game3D.FirstPersonController.New(world);
    Game3D.World3D.setCameraController(world, fps);

    // manual loop (callback sugar pending CO-2):
    while (Game3D.World3D.tick(world)) {
        Game3D.WorldStream3D.setCenter(stream, Game3D.Entity3D.worldPosition(player));
        Game3D.WorldStream3D.update(stream, Game3D.World3D.get_dt(world));
        Game3D.World3D.stepSimulation(world, Game3D.World3D.get_dt(world));
        Game3D.World3D.beginFrame(world);
        Game3D.World3D.drawScene(world);
        Game3D.World3D.endScene(world);
        Game3D.World3D.present(world);
    }
}
```
This is the Phase-12 target. Until CO-2 lands, interpreted Zia uses the manual
loop / `runFramesOnly`, exactly as `3Dnextlevel` does today.
