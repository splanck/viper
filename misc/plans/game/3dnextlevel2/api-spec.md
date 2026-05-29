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
- **Determinism first.** Simulation stays deterministic; APIs that offload work
  do so transparently and never change `runFrames` results.
- **Escape hatches preserved.** All existing raw `Viper.Graphics3D.*` access
  remains; new wrappers never hide the primitive.
- **Errors** trap as `Game3D.<Type>.<method>: <reason>` /
  `Graphics3D.<Type>.<method>: <reason>`; no duplicated deep validation.
- **Units/angles/colors/time** unchanged from `3Dnextlevel/api-spec.md`.

## Carryover surface (Phase C)

```zia
// CO-4 render-target finalization (or capability-gate)
expose func Canvas3D.BackendSupports(name: String) -> Boolean   // + "rt-finalize"
// CO-6 per-light shadow control
expose Boolean Light3D.CastsShadows;        // get/set; skip non-casters in shadow pass
// CO-3 diagnostics surface is behaviour, not new methods: destroyed/double-despawn/
//      missing-asset/capability-fallback traps in Game3D.<Type>.<method> form.
```

## Concurrency control — `Viper.Game3D.World3D` (mostly internal)

```zia
expose Integer World3D.workerCount;          // read; configured at New or via env
expose func World3D.setWorkerCount(n: Integer)
expose Boolean World3D.jobsEnabled;          // toggle pool (determinism unaffected)
```
The job/parallel-for/commit-queue primitives are internal C (`rt_job_*`); game
code does not author jobs directly in this milestone.

## Large coordinates — `Viper.Game3D.World3D` / `Viper.Graphics3D.Scene3D`

```zia
expose Boolean World3D.floatingOrigin;       // enable origin rebasing
expose func World3D.setOriginRebaseThreshold(meters: Float)
expose Vec3 World3D.worldOrigin;             // current rebase offset (read)
// internal: rt_scene3d_rebase_origin(delta) shifts nodes/bodies/audio/particles
```

## Spatial queries — `Viper.Graphics3D.Scene3D`

```zia
// index-backed; results match the flat-walk reference
expose func Scene3D.queryAABB(min: Vec3, max: Vec3) -> Seq[SceneNode3D]
expose func Scene3D.querySphere(center: Vec3, radius: Float) -> Seq[SceneNode3D]
expose func Scene3D.raycastNodes(origin: Vec3, dir: Vec3, maxDist: Float) -> SceneNode3D?
expose Integer Scene3D.visibleNodeCount;     // last-frame, for Debug3D/telemetry
```
Cull substitution and physics-broadphase unification are internal.

## Async assets — `Viper.Game3D.Assets3D`

```zia
expose func Assets3D.LoadModelAsync(path: String) -> ModelHandle
expose func Assets3D.LoadModelAssetAsync(assetPath: String) -> ModelHandle
expose Boolean ModelHandle.ready;
expose func ModelHandle.get() -> Entity3D?   // null until ready
expose func Assets3D.SetResidencyBudget(bytes: Integer)
expose func Assets3D.Evict(handle: ModelHandle)
```
Existing synchronous `LoadModel`/`Preload` stay (now warmed in background).

## World streaming — `Viper.Game3D.WorldStream` (new)

```zia
class WorldStream {
    static func New(world: World3D) -> WorldStream
    expose func setCenter(pos: Vec3)             // usually the player
    expose func setRadii(loadRadius: Float, unloadRadius: Float)
    expose func mountTiledTerrain(manifestPath: String)   // tile grid > 4 km²
    expose func mountCells(manifestPath: String)          // scene cells
    expose func update(dt: Float)                // streams in/out around center
    expose Integer residentCellCount;
    expose Integer residentTerrainTileCount;
}
```
`Terrain3D` gains tile-aware construction; the streamed container extends VSCN or
a tiled side-format (Phase 0 decision). `World3D` can own a `WorldStream`.

## Visibility — `Viper.Graphics3D.Canvas3D` / `Scene3D`

```zia
expose func Canvas3D.SetOcclusionCulling(on: Boolean)   // now selects REAL occlusion (CO-8)
expose func Canvas3D.SetFrustumCulling(on: Boolean)     // frustum-only (unchanged)
expose Boolean Canvas3D.BackendSupports;                // + "occlusion", "hlod"
// auto-LOD on SceneNode3D (extends existing manual LOD)
expose func SceneNode3D.SetAutoLOD(on: Boolean, screenErrorPx: Float)
expose func SceneNode3D.SetImpostor(distance: Float, pixels: Object)   // HLOD/impostor
expose Integer Canvas3D.OccludedDrawCount;              // telemetry
```

## Lighting — `Viper.Graphics3D.Canvas3D` / `Light3D` / `PostFX`

```zia
expose func Canvas3D.SetClusteredLighting(on: Boolean)  // forward+ many-light path
expose Integer Canvas3D.MaxActiveLights;                // raised when clustered+supported
expose func Canvas3D.SetShadowCascades(count: Integer)  // CSM for directional light
expose Boolean Light3D.CastsShadows;                    // CO-6
```
Falls back to 16-light forward when unsupported.

## Physics — `Viper.Graphics3D.Physics3DWorld` / joints / `Collider3D`

```zia
expose func Physics3DWorld.SetSolverIterations(n: Integer)   // iterated/warm-started
expose Integer CollisionEvent3D.ContactCount;               // now >1 (real manifold)
// new joints (close hinge/rope doc overclaim + add 6DOF)
class HingeJoint3D  { static func New(a, b, anchor: Vec3, axis: Vec3) -> HingeJoint3D }
class RopeJoint3D   { static func New(a, b, maxLength: Float) -> RopeJoint3D }
class SixDOFJoint3D { static func New(a, b, frameA: Mat4, frameB: Mat4) -> SixDOFJoint3D
                      expose func setLinearLimits(min: Vec3, max: Vec3)
                      expose func setAngularLimits(min: Vec3, max: Vec3) }
// Collider3D.NewConvexHull becomes real GJK/EPA convex (no API change)
```
`AddJoint(joint, type)` gains hinge/rope/6DOF type codes.

## Navigation — `Viper.Graphics3D.NavMesh3D` / `NavAgent3D`

```zia
// auto-generation from arbitrary geometry (Recast-style, from scratch)
expose func NavMesh3D.Bake(scene: Scene3D, agentRadius: Float, agentHeight: Float,
                           maxSlope: Float, cellSize: Float) -> NavMesh3D
expose func NavMesh3D.BakeTiled(scene: Scene3D, tileSize: Float, ...) -> NavMesh3D
expose func NavMesh3D.RebuildTile(tileX: Integer, tileZ: Integer)   // dynamic carve
expose func NavMesh3D.AddObstacle(min: Vec3, max: Vec3)
expose func NavMesh3D.AddOffMeshLink(from: Vec3, to: Vec3, bidirectional: Boolean)
// agent local avoidance (ORCA/RVO-style); agent_radius now applied to corridors
expose Boolean NavAgent3D.AvoidanceEnabled;
expose Float   NavAgent3D.AvoidanceRadius;
```
Existing `NavMesh3D.Build(mesh, ...)` (bake-from-one-mesh) is retained.

## Animation — `Viper.Graphics3D` IK / controller

```zia
class IK3D {                                   // new
    static func TwoBone(skeleton, root: Integer, mid: Integer, end: Integer) -> IK3D
    static func LookAt(skeleton, bone: Integer) -> IK3D
    static func FABRIK(skeleton, chain: Seq[Integer]) -> IK3D
    expose func setTarget(pos: Vec3)
    expose func setWeight(w: Float)
    expose func solve()                        // applied before skinning
}
expose func AnimController3D.PlayLayerAdditive(layer: Integer, state: String)  // TRUE additive
class BlendTree3D {                            // parametric blend over AnimBlend3D
    static func New1D(skeleton) -> BlendTree3D
    static func New2D(skeleton) -> BlendTree3D
    expose func addSample(animation: Object, x: Float, y: Float)
    expose func setParam(x: Float, y: Float)
    expose func update(dt: Float)
}
expose func Animation3D.Retarget(srcSkeleton, dstSkeleton) -> Animation3D
expose func AnimController3D.SetAnimationLOD(distance: Float, rateHz: Float)
```
`Game3D.Animator3D` gains thin `ik`, `blendTree`, and additive-layer wrappers.

## Asset pipeline — textures / glTF

```zia
expose func Material3D.SetAlbedoCompressed(ktx2OrPixels: Object)   // GPU-compressed upload
expose Boolean Canvas3D.BackendSupports;       // + "bc7", "astc", "etc2"
expose func Model3D.GetCameraCount() -> Integer     // glTF camera import
expose func Model3D.GetCamera(i: Integer) -> Camera3D?
expose func Model3D.SelectScene(index: Integer)     // glTF multi-scene
// Draco/meshopt handled transparently inside the glTF loader
```

## Hello (open-world) — target consumer shape

```zia
bind Viper.Game3D as Game3D;

func start() {
    var world = Game3D.World3D.New("Open World", 1280, 720);
    world.set_floatingOrigin(true);
    var stream = Game3D.WorldStream.New(world);
    stream.mountTiledTerrain("assets/world/terrain.manifest");
    stream.mountCells("assets/world/cells.manifest");
    stream.setRadii(256.0, 320.0);

    var player = Game3D.Assets3D.LoadModel("assets/char/hero.glb");
    Game3D.World3D.spawn(world, player);
    var fps = Game3D.FirstPersonController.New(world);
    Game3D.World3D.setCameraController(world, fps);

    // manual loop (callback sugar pending CO-2):
    while (Game3D.World3D.tick(world)) {
        stream.setCenter(Game3D.Entity3D.worldPosition(player));
        stream.update(Game3D.World3D.get_dt(world));
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
