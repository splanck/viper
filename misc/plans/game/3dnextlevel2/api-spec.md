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
// internal: rt_scene3d_rebase_origin(delta) shifts nodes/bodies/audio/particles
```

## Spatial queries — `Viper.Graphics3D.Scene3D`

```zia
// index-backed; results match the flat-walk reference
expose func Scene3D.QueryAABB(min: Vec3, max: Vec3) -> Seq[SceneNode3D]
expose func Scene3D.QuerySphere(center: Vec3, radius: Float) -> Seq[SceneNode3D]
expose func Scene3D.RaycastNodes(origin: Vec3, dir: Vec3, maxDist: Float) -> SceneNode3D?
expose Integer Scene3D.VisibleNodeCount;     // last-frame, for Debug3D/telemetry
```
These methods are backed by the internal Scene3D spatial index with a
deterministic flat-walk fallback for parity testing. They skip hidden subtrees
and return visible nodes with their own mesh bounds. Cull substitution is
internal. A generated 10k Scene3D fixture now guards candidate reduction for
isolated AABB queries and indexed draw culling. Physics may share this query
contract while using a separate tuned broadphase structure behind the same API.

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
expose func Assets3D.Evict(handle: AssetHandle3D)
```
Deferred-handle baseline is implemented for all four `LoadModel*Async` entry
points. Handles start pending with `progress == 0.0`, complete synchronously on
first `ready`/result observation, and preserve the same entity/template results
as their blocking counterparts. `cancel()` before first observation produces a
terminal cancelled handle with no result; completed-handle cancel remains a
stable no-op. `SetResidencyBudget` and `Evict` are implemented over the shared
`ModelTemplate` cache with least-recently-used eviction. Missing filesystem
paths and missing asset-manager paths now complete as terminal non-cancel error
handles (`"cannot read file"` / `"asset not found"`) instead of trapping on
observation. Unsupported model extensions and malformed glTF JSON now also
complete as terminal handle errors. Worker decode and budgeted upload remain the
Phase 4 expansion. Existing synchronous `LoadModel`/`Preload` stay, with
`Preload` becoming a real background warm when the worker-backed loader lands.

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
`mountTiledTerrain` parses `tiles[]` manifests with the same metadata shape plus
optional `width`, `depth`, `scale`, and `heightmap`, instantiates `Terrain3D`
tile payloads for resident tiles, applies `viper-heightmap-v1` normalized
height sidecars through the existing terrain heightmap path, spawns matching
static heightfield collider entities for resident tiles, renders resident tiles
through `World3D.drawScene`, contributes hidden terrain nav-bake source nodes to
the owning world scene, and exposes terrain payloads through
`getResidentTerrainTile(index)`. `WorldStream3D.update(dt)` now applies a
deterministic per-update load budget and reports deferred desired payloads
through `pendingRequestCount`. Phase 12 editor/debug hooks expose parsed cell
and terrain-tile counts, names, centers, resident flags, and byte estimates
through lower/camel `getCell*` and `getTerrainTile*` methods, including
`getTerrainTileHeightmap(index)` for the resolved sidecar path. Nav area
metadata, authored terrain physics metadata, and worker-backed scheduler
integration remain Phase 5 runtime work.
`World3D.stream` now returns a stable world-owned stream handle;
`WorldStream3D.New(world)` remains available for separate controllers.

## Visibility — `Viper.Graphics3D.Canvas3D` / `Scene3D`

```zia
expose func Canvas3D.SetOcclusionCulling(on: Boolean)   // frustum + conservative CPU occlusion
expose func Canvas3D.SetFrustumCulling(on: Boolean)     // frustum-only
expose func Canvas3D.BackendSupports(name: String) -> Boolean // + "occlusion", "hlod"
// auto-LOD on SceneNode3D (extends existing manual LOD)
expose func SceneNode3D.SetAutoLOD(on: Boolean, screenErrorPx: Float)
expose func SceneNode3D.SetImpostor(distance: Float, pixels: Object)   // HLOD/impostor
expose Integer Canvas3D.DrawCount;                      // telemetry
expose Integer Canvas3D.OccludedDrawCount;              // telemetry
```

Implemented slice: `SetOcclusionCulling` enables frustum rejection plus a
conservative low-resolution CPU coverage/depth grid over sorted opaque draws;
`SetFrustumCulling` remains frustum-only. `SetAutoLOD` selects among authored
`AddLOD` meshes by projected screen size, `SetImpostor` builds a retained unlit
textured quad proxy, `BackendSupports("occlusion")` and
`BackendSupports("hlod")` report those runtime paths, and `OccludedDrawCount`
mirrors the latest visibility skip count. GPU occlusion queries, Hi-Z, and
portal/PVS are still backend/index acceleration work.

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
`MaxActiveLights` to the bounded 64-light payload table when enabled. Real GPU
forward+ upload/shading work remains Phase 7 runtime scope.
`SetShadowCascades(1)` maps to the current single-shadow-map behavior; counts
above one require
`BackendSupports("shadow-csm")` and currently trap before mutation.

## Physics — `Viper.Graphics3D.Physics3DWorld` / joints / `Collider3D`

```zia
expose func Physics3DWorld.SetSolverIterations(n: Integer)   // iterative contact + joint passes
expose Integer Physics3DWorld.SolverIterations;
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
// Collider3D.NewConvexHull uses GJK/EPA for hull-vs-hull and hull-vs-simple pairs (no API change).
```

`SetSolverIterations` is now live for contact and joint passes. AABB-vs-AABB
events expose a bounded multi-point manifold; other shapes still report one
representative contact, and warm-starting is still pending. Convex-hull pairs now
use a simplex/Minkowski narrow phase rather than AABB fallback.
Mesh/convex-vs-sphere/capsule/box contacts reuse the per-mesh BVH for candidate
triangle pruning and keep the previous full scan as a fallback when the BVH path
is unavailable; this has no public API surface.
`AddJoint(joint, type)` now accepts `RT_JOINT_DISTANCE 0`,
`RT_JOINT_SPRING 1`, `RT_JOINT_HINGE 2`, `RT_JOINT_ROPE 3`, and
`RT_JOINT_SIXDOF 4`. The runtime type constants are `RT_JOINT_*`, not the
stale `RT_JOINT3D_*` spelling older notes used.
SixDof linear limits currently project frame-anchor separation; angular limits
project relative angular velocity along world axes.

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
// agent local avoidance (ORCA/RVO-style); agent_radius now applied to corridors
expose Boolean NavAgent3D.AvoidanceEnabled;
expose Float   NavAgent3D.AvoidanceRadius;
expose func World3D.bakeNavMesh(agentRadius: Float, agentHeight: Float,
                                maxSlope: Float, cellSize: Float) -> NavMesh3D
expose func World3D.bakeTiledNavMesh(tileSize: Float, agentRadius: Float,
                                     agentHeight: Float, maxSlope: Float,
                                     cellSize: Float) -> NavMesh3D
```
Existing `NavMesh3D.Build(mesh, ...)` (bake-from-one-mesh) is retained.

Implemented slices: `NavAgent3D.AvoidanceEnabled` and `AvoidanceRadius` are now public `Viper.Graphics3D` PascalCase properties. They provide an opt-in same-NavMesh local separation pass during `Update`. `NavMesh3D.Bake(scene, ...)` is public and flattens `Mesh3D` nodes through `Scene3D` world transforms before using the existing triangle build path. `World3D.bakeNavMesh(...)` and `bakeTiledNavMesh(...)` expose the same bake path as Game3D lower/camel editor hooks over the world's owned scene. `NavMesh3D.BakeTiled(scene, ...)` and `RebuildTile(tileX, tileZ)` are public baseline entries; they currently operate on a full-scene navmesh and refilter preserved source triangles rather than owning real tile-local data. `NavMesh3D.AddOffMeshLink(from, to, bidirectional)` and read-only `OffMeshLinkCount` are public; authored endpoints must resolve to current walkable polygons, and pathfinding treats the link as a directed or bidirectional graph edge. `NavMesh3D.AddObstacle(min, max)`, `RemoveObstacle(index)`, `UpdateObstacle(index, min, max)`, and read-only `ObstacleCount` are also public; the baseline stores finite AABB obstacles, removes overlapping walkable triangles, and rebuilds adjacency immediately after edits. `agentRadius` is applied as a conservative shared-portal width gate. Full ORCA/RVO crowd quality, polygon/corridor erosion, voxel/region baking, real tiled ownership, and pathfinding acceleration remain tracked Phase 9 work.

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
records declared mip payload byte ranges for residency telemetry, and decodes
uncompressed RGBA8 mip payloads into CPU `Pixels` fallbacks. `SetResidentMipRange`
switches the active fallback to the first resident mip while updating telemetry.
Existing material texture methods accept `TextureAsset3D` when that fallback exists, and
`Canvas3D.BackendSupports("bc7"/"astc"/"etc2")` now recognizes the capability
names while reporting unsupported until native compressed upload lands.

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
