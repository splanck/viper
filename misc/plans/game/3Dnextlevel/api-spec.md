# Game3D - runtime API specification

> Draft. All types below are proposed runtime-backed `Viper.Game3D` classes,
> enums, and functions registered through `runtime.def`, not a source-level Zia
> library. The Zia code blocks show the intended consumer-facing shape. The API
> assumes the runtime contracts in `runtime-changes.md`, especially final-frame
> overlay, final screenshot, synthetic input/clock, backend capabilities, and
> package-aware asset loading.

## Conventions

- **Units:** world units, meters by convention.
- **Angles:** degrees at the Game3D API boundary; wrappers convert where needed.
- **Time:** seconds (`Float dt`). Live loops source dt from `Canvas3D.DeltaTimeSec`;
  deterministic tests use the synthetic clock contract.
- **Colors:** `Float` components in `[0, 1]`.
- **Enums over magic integers.** Game3D uses runtime-backed enums/constants for
  fixed option sets and passes them through to lower-level runtime integer APIs
  where values intentionally match.
- **Events before callback sugar.** The authoritative runtime API exposes
  pollable event objects/buffers. The spec may show optional callback helpers
  for ergonomics, but those ship only if Phase 0A proves a clean bridge using
  function types or one-method interfaces.
- **No-return callbacks use `Unit`.** Zia function types for callbacks that do
  not return a value are spelled `(T) -> Unit`, not `(T) -> Void`.
- **Escape hatches:** wrappers expose underlying primitives (`world.canvas`,
  `world.scene`, `entity.node`, `entity.mesh`, `entity.material`, `entity.body`,
  `entity.anim?.controller`).
- **No common-case `Mat4`:** placement APIs accept position/rotation/scale and
  update `SceneNode3D` transforms internally. Raw `Mat4` remains available as an
  escape hatch for advanced rendering.
- **Errors:** Game3D validates boundary inputs and traps with
  `Game3D.<Type>.<method>: <reason>`. It does not duplicate deep runtime
  validation.
- **Namespace:** recommended public namespace is `Viper.Game3D.*`, confirmed in
  Phase 0A before broad `runtime.def` additions.

## Enums and masks - `Viper.Game3D`

```zia
enum Layers { World = 1, Dynamic = 2, Player = 4, Trigger = 8, Debris = 16 }
enum BodyShape { Box, Sphere, Capsule }
enum SyncMode { NodeFromBody = 0, BodyFromNode = 1, NodeFromAnimRootMotion = 2, TwoWayKinematic = 3 }
enum AlphaMode { Opaque = 0, Mask = 1, Blend = 2 }
enum ShadingModel { Phong = 0, Toon, PBR, Fresnel, Emissive, Unlit }
enum QualityLevel { Performance, Balanced, Cinematic }
enum CollisionPhase { Enter = 0, Stay = 1, Exit = 2, Any = 3 }
```

`Layers` values are single-bit. Do not require Zia bitwise operators in user
code; expose a tiny mask helper instead:

```zia
class LayerMask {
    expose Integer bits;

    static func None() -> LayerMask
    static func All() -> LayerMask
    static func Of(layer: Layers) -> LayerMask
    expose func include(layer: Layers) -> LayerMask
    expose func includes(layer: Layers) -> Boolean
}
```

## World3D - `Viper.Game3D.World3D`

The all-in-one container. Owns canvas, camera, scene graph, physics world, input
wrapper, audio, effect registry, lighting/post-FX defaults, and managed loops.
All ownership, registries, event buffers, and frame sequencing live in the C
runtime object; Zia code consumes the object through normal runtime bindings.

```zia
class World3D {
    expose Canvas3D canvas;
    expose Camera3D camera;
    expose Scene3D scene;
    expose Physics3DWorld physics;
    expose Input3D input;
    expose Audio3D audio;          // listener follows camera by default
    expose EffectRegistry3D effects;
    expose Float dt;
    expose Float elapsed;
    expose Integer frame;

    static func New(title: String, width: Integer, height: Integer) -> World3D
    static func NewWithCamera(title: String, width: Integer, height: Integer,
                              fovDeg: Float, nearPlane: Float, farPlane: Float) -> World3D
    expose func destroy()
    expose func isDestroyed() -> Boolean

    // Scene and entities
    expose func spawn(e: Entity3D) -> Entity3D
    expose func despawn(e: Entity3D)
    expose func findNode(name: String) -> SceneNode3D?
    expose func findEntity(name: String) -> Entity3D?

    // Camera
    expose func setCameraController(c: CameraController?)
    expose func lookAt(target: Vec3)
    expose func onResize(width: Integer, height: Integer)

    // Environment
    expose func setAmbient(r: Float, g: Float, b: Float)
    expose func addLight(slot: Integer, light: Light3D?)
    expose func clearLights()
    expose func setSkybox(cube: CubeMap3D?)
    expose func setFog(r: Float, g: Float, b: Float, nearPlane: Float, farPlane: Float)
    expose func setQuality(q: QualityLevel)

    // Physics and events
    expose func collisionEventCount(phase: CollisionPhase) -> Integer
    expose func collisionEvent(phase: CollisionPhase, index: Integer) -> Collision3DEvent
    expose func clearCollisionEvents()
    // Optional callback convenience if Phase 0A approves the bridge:
    expose func onCollision(layerA: Layers, layerB: Layers, phase: CollisionPhase,
                            fn: (Collision3DEvent) -> Unit)
    expose func onCollisionSimple(layerA: Layers, layerB: Layers,
                                  fn: (Entity3D, Entity3D) -> Unit)
    expose func setGravity(x: Float, y: Float, z: Float)

    // Managed loop - variable timestep. Callback overloads require Phase 0A
    // callback-bridge approval; manual loop APIs below are mandatory.
    expose func run(update: (Float) -> Unit)
    expose func runWithOverlay(update: (Float) -> Unit, overlay: () -> Unit)

    // Managed loop - fixed timestep
    expose func runFixed(stepSec: Float, update: (Float) -> Unit)
    expose func runFixedWithOverlay(stepSec: Float, update: (Float) -> Unit, overlay: () -> Unit)

    // Deterministic test runner over synthetic input/clock
    expose func runFrames(frameCount: Integer, stepSec: Float, update: (Float) -> Unit)

    // Manual loop escape hatch
    expose func tick() -> Boolean
    expose func stepSimulation(stepSec: Float)
    expose func beginFrame()
    expose func drawScene()
    expose func drawEffects()
    expose func endScene()
    expose func drawOverlay(overlay: () -> Unit)
    expose func captureFinalFrame() -> Pixels
    expose func present()
}
```

### `New` defaults

On construction:

1. Create `Canvas3D`, `Camera3D`, `Scene3D`, `Physics3DWorld`, `Input3D`,
   `Audio3D`, and `EffectRegistry3D`.
2. Install explicit default lighting through `Lighting.Studio`.
3. Apply `Quality.Balanced`, gated by backend capabilities.
4. Enable frustum culling (`SetFrustumCulling(true)`).
5. Set a neutral clear color / skybox.
6. Bind an `AudioListener3D` to the camera and set it active.

Every default is overridable by a later preset or raw primitive call.

### Managed frame sequence

The loop is intentionally split into input, simulation, late sync, render, final
overlay, and present. This avoids one-frame-late controllers and makes collision
and animation ordering explicit.

```text
while not canvas.ShouldClose:
    tick()                         # Poll, input.update, dt clamp, resize/aspect sync

    # Variable-timestep mode:
    update(dt)
    stepSimulation(dt)

    beginFrame()
    drawScene()
    drawEffects()
    endScene()
    if overlay:
        drawOverlay(overlay)        # post-FX-safe final overlay queue
    present()                       # FinalizeFrame + Flip
```

If Phase 0A approves the callback bridge, `runFixed` and `runFrames` call the
user update callback immediately before each simulation step. The mandatory
runtime contract is the manual-loop path: callers run their own update code
between `tick()` and `stepSimulation(step)`.

`stepSimulation(step)` performs:

```text
optional entity.onUpdate callbacks
cameraController?.update(world, step)       # input/movement phase
entity.anim?.update(step)                   # animation/root-motion prep
physics.Step(step)
scene.SyncBindings(step)
dispatch collision enter/stay/exit events
effects.update(step)
Audio3D.SyncBindings(step)
cameraController?.lateUpdate(world, step)   # camera placement for this render
```

### Fixed-timestep loop

`runFixed(step, update)` uses an accumulator for live games. `update` receives the
constant fixed step. Rendering still occurs once per display frame.

```text
accumulator += clamp(canvas.DeltaTimeSec, 0, DT_MAX)
while accumulator >= step:
    update(step)
    stepSimulation(step)
    accumulator -= step
render once
```

`runFrames(frameCount, step, update)` is for deterministic tests. It selects the
synthetic clock/input path, advances exactly `frameCount` fixed steps, and never
uses wall-clock dt. Tests assert exact simulation state and compare final-frame
pixels with backend-aware tolerances.

## Entity3D - `Viper.Game3D.Entity3D`

The unit spawned into a world: a single mesh node, or a group root wrapping an
imported model subtree.

```zia
class Entity3D {
    expose Integer id;
    expose SceneNode3D node;
    expose Mesh3D? mesh;
    expose Material3D? material;
    expose Physics3DBody? body;
    expose Animator3D? anim;
    expose Layers layer;
    expose LayerMask collisionMask;
    expose String name;

    static func New() -> Entity3D
    static func Of(mesh: Mesh3D, material: Material3D) -> Entity3D
    static func FromNode(root: SceneNode3D) -> Entity3D

    // Transform - fluent
    expose func setPosition(x: Float, y: Float, z: Float) -> Entity3D
    expose func setPositionV(p: Vec3) -> Entity3D
    expose func setScale(s: Float) -> Entity3D
    expose func setScaleXYZ(x: Float, y: Float, z: Float) -> Entity3D
    expose func setRotationEuler(xDeg: Float, yDeg: Float, zDeg: Float) -> Entity3D

    // Contents
    expose func setMesh(m: Mesh3D) -> Entity3D
    expose func setMaterial(m: Material3D) -> Entity3D
    expose func addChild(child: Entity3D) -> Entity3D
    expose func isGroup() -> Boolean

    // Metadata and physics filtering
    expose func setName(n: String) -> Entity3D
    expose func setLayer(layer: Layers) -> Entity3D
    expose func setCollisionMask(mask: LayerMask) -> Entity3D

    // Physics
    expose func attachBody(def: BodyDef) -> Entity3D
    expose func applyImpulse(x: Float, y: Float, z: Float)
    expose func setVelocity(x: Float, y: Float, z: Float)

    // Optional behavior callbacks if Phase 0A approves the callback bridge
    expose func onUpdate(fn: (Entity3D, Float) -> Unit)
    expose func onCollision(fn: (Collision3DEvent) -> Unit)

    // Queries
    expose func position() -> Vec3
    expose func worldPosition() -> Vec3
    expose func isSpawned() -> Boolean
    expose func isDestroyed() -> Boolean
}
```

Notes:

- `FromNode` is required for `Model3D.Instantiate()` results; imported assets are
  node subtrees, not a single mesh/material pair.
- `spawn` attaches `entity.node` to `world.scene` and registers the entity.
- `despawn` removes the entity from the registry, scene, physics world, effect
  bindings, and child entity ownership. Group despawn recurses over Game3D child
  entities; raw imported child nodes remain part of the root node subtree.
- `findNode` returns a raw `SceneNode3D?`; `findEntity` returns only entities
  known to the Game3D registry.
- `World3D.destroy()` releases world-owned runtime objects and invalidates
  managed entities. Calls on destroyed worlds/entities trap with a clear
  `Game3D.<Type>.<method>` diagnostic instead of silently using freed runtime
  state.
- Raw escape hatches returned from `world.canvas`, `world.scene`, `entity.node`,
  `entity.body`, and `entity.anim?.controller` remain valid only according to
  their underlying runtime ownership rules; docs must call out any handle that
  is invalidated by `despawn` or `destroy`.

## BodyDef and collisions - `Viper.Game3D.Physics`

Small value object used by `Entity3D.attachBody`.

```zia
class BodyDef {
    expose BodyShape shape;
    expose Float mass;
    expose Float friction;
    expose Float restitution;
    expose Boolean isStatic;
    expose Boolean isKinematic;
    expose Boolean isTrigger;
    expose Boolean useCCD;
    expose Layers layer;
    expose LayerMask mask;
    expose SyncMode syncMode;

    static func Box(halfX: Float, halfY: Float, halfZ: Float, mass: Float) -> BodyDef
    static func Sphere(radius: Float, mass: Float) -> BodyDef
    static func Capsule(radius: Float, height: Float, mass: Float) -> BodyDef
    static func StaticBox(halfX: Float, halfY: Float, halfZ: Float) -> BodyDef
    static func StaticPlane(size: Float) -> BodyDef

    expose func withLayer(layer: Layers) -> BodyDef
    expose func withMask(mask: LayerMask) -> BodyDef
    expose func asTrigger() -> BodyDef
    expose func withSync(mode: SyncMode) -> BodyDef
}
```

`StaticPlane(size)` is a convenience helper implemented as a shallow static box
or mesh collider, because the runtime does not currently expose a plane body
constructor. The docs must state the exact thickness and origin.

`attachBody` creates the `Physics3DBody`, applies layer/mask/material flags,
binds it to the entity's node, and registers body ownership with `World3D`.

```zia
class Collision3DEvent {
    expose CollisionPhase phase;
    expose Entity3D a;
    expose Entity3D b;
    expose CollisionEvent3D raw;    // escape hatch to runtime event
    expose Boolean isTrigger;
    expose Float relativeSpeed;
    expose Float normalImpulse;

    expose func point() -> Vec3
    expose func normal() -> Vec3
    expose func other(e: Entity3D) -> Entity3D?
}
```

Dispatch rules:

- Runtime enter/stay/exit buffers are the source of truth.
- Layer pair keys are canonicalized for unordered handlers.
- `CollisionPhase.Any` receives enter, stay, and exit.
- `collisionEventCount` / `collisionEvent` are the required public API for tests,
  docs, and non-Zia consumers.
- Optional `onCollisionSimple` fires only on `Enter` by default, so audio/VFX
  samples do not spam every fixed step.
- Optional entity-local collision handlers run after world-level handlers.

## Input3D - `Viper.Game3D.Input3D`

Named input intent over `Viper.Input.Keyboard` / `Viper.Input.Mouse`.
`Canvas3D.Poll()` must run once per frame before `Input3D.update()`. Synthetic
input is a runtime source, so Game3D code does not branch for tests.

```zia
class Input3D {
    expose Float lookSensitivity;

    expose func update()
    expose func isDown(key: Integer) -> Boolean
    expose func pressed(key: Integer) -> Boolean
    expose func released(key: Integer) -> Boolean
    expose func mouseDelta() -> Vec2
    expose func mouseButton(button: Integer) -> Boolean
    expose func mousePressed(button: Integer) -> Boolean
    expose func wheelY() -> Float
    expose func moveAxis() -> Vec3
    expose func lookAxis() -> Vec2
    expose func captureMouse()
    expose func releaseMouse()
}
```

`Keys` and `MouseButtons` modules re-export runtime constants with stable names.

## Cameras - `Viper.Game3D.Cameras`

Controllers are two-phase: `update` may write movement/body input before
physics, and `lateUpdate` places the camera after physics/binding sync.

```zia
interface CameraController {
    func update(world: World3D, dt: Float);
    func lateUpdate(world: World3D, dt: Float);
}
```

If Zia default interface methods are unsuitable, provide a small
`BaseCameraController` with no-op implementations.

| Controller | Constructor | Behavior |
|---|---|---|
| `FirstPersonController` | `New(world)` | WASD + mouse look, optional `CharacterController3D` grounding |
| `FreeFlyController` | `New(world)` | 6-DoF spectator movement |
| `OrbitController` | `New(world, target: Vec3)` | drag orbit, wheel zoom, pitch clamp |
| `FollowController` | `New(world, target: Entity3D, offset: Vec3)` | late-update spring follow with damping |

## CharacterController3D - `Viper.Game3D.CharacterController3D`

```zia
class CharacterController3D {
    expose Character3D character;
    expose Entity3D entity;
    expose Float speed;
    expose Float jumpSpeed;
    expose Float gravity;

    static func New(world: World3D, entity: Entity3D,
                    radius: Float, height: Float, mass: Float) -> CharacterController3D
    expose func update(input: Input3D, camera: Camera3D, dt: Float)
    expose func teleport(x: Float, y: Float, z: Float)
    expose func grounded() -> Boolean
}
```

## Presets - `Viper.Game3D` preset helpers

### Lighting

```zia
Lighting.Studio(world)
Lighting.Outdoor(world, sunDir: Vec3)
Lighting.Night(world)
Lighting.Interior(world)
Lighting.Clear(world)
```

Presets clear managed light slots, set ambient, install named lights, and tune
shadow bias/resolution through the selected quality profile.

### Materials

```zia
Materials.Plastic(r: Float, g: Float, b: Float) -> Material3D
Materials.Metal(r: Float, g: Float, b: Float) -> Material3D
Materials.Rubber(r: Float, g: Float, b: Float) -> Material3D
Materials.Glass(r: Float, g: Float, b: Float, alpha: Float) -> Material3D
Materials.Emissive(r: Float, g: Float, b: Float, intensity: Float) -> Material3D
Materials.Unlit(r: Float, g: Float, b: Float) -> Material3D
Materials.FromAlbedoMap(pixels: Pixels) -> Material3D
```

Each preset sets the appropriate `ShadingModel` and `AlphaMode` so callers never
touch raw integer codes.

### PostFX and quality

```zia
PostFX.Cinematic(world)   PostFX.Crisp(world)   PostFX.None(world)
Quality.Apply(world, QualityLevel.Performance)
Quality.Apply(world, QualityLevel.Balanced)
Quality.Apply(world, QualityLevel.Cinematic)
```

Quality profiles coordinate shadows, post-FX, culling, and debug-overlay cost.
They must query `Canvas3D.BackendSupports(...)` and degrade without trapping.
Software-safe profiles use Bloom, Tonemap, FXAA, ColorGrade, and Vignette only.

## Environment / scenery - `Viper.Game3D.Environment`

One call sets sky, ground, fog, and a matching light rig for good-looking default
outdoor scenes. Builds on existing skybox/terrain/water runtime APIs.

```zia
Environment.Outdoor(world) -> EnvHandle
Environment.Sunset(world) -> EnvHandle
Environment.Overcast(world) -> EnvHandle
Environment.Night(world) -> EnvHandle

class EnvHandle {
    expose func withTerrain(size: Float, height: Float) -> EnvHandle
    expose func withWater(level: Float) -> EnvHandle
    expose func withFog(nearPlane: Float, farPlane: Float) -> EnvHandle
}
```

## Prefabs and assets - `Viper.Game3D.Prefab`, `Viper.Game3D.Assets3D`

```zia
Prefab.Box(size: Float, mat: Material3D) -> Entity3D
Prefab.BoxXYZ(w: Float, h: Float, d: Float, mat: Material3D) -> Entity3D
Prefab.Sphere(radius: Float, segments: Integer, mat: Material3D) -> Entity3D
Prefab.Cylinder(radius: Float, height: Float, segments: Integer, mat: Material3D) -> Entity3D
Prefab.Plane(w: Float, h: Float, mat: Material3D) -> Entity3D
Prefab.Ground(size: Float, mat: Material3D) -> Entity3D

Assets3D.LoadModel(path: String) -> Entity3D
Assets3D.LoadModelAsset(assetPath: String) -> Entity3D
Assets3D.LoadModelTemplate(path: String) -> ModelTemplate
Assets3D.LoadModelTemplateAsset(assetPath: String) -> ModelTemplate
Assets3D.Preload(path: String)
Assets3D.ClearCache()
```

Asset path rules:

- `LoadModel` is the development/filesystem path.
- `LoadModelAsset` resolves through the mounted asset system first and supports
  packaged GLB/glTF dependencies.
- Relative external glTF buffers/textures resolve relative to the model asset.
- `ModelTemplate` caches loaded `Model3D` objects and instantiates group
  entities cheaply.

## Animation - `Viper.Game3D.Animator3D`

Ergonomic wrapper over `AnimController3D` / `Skeleton3D`, exposed as
`entity.anim`.

```zia
class Animator3D {
    expose AnimController3D controller;

    expose func play(name: String) -> Boolean
    expose func crossfade(name: String, sec: Float) -> Boolean
    expose func setSpeed(name: String, speed: Float)
    expose func isPlaying(name: String) -> Boolean
    expose func stateTime() -> Float
    expose func eventCount() -> Integer
    expose func eventName(index: Integer) -> String
    // Optional callback convenience if Phase 0A approves the bridge:
    expose func onAnimEvent(fn: (String) -> Unit)
    expose func update(dt: Float)
}
```

`World3D` advances animation before physics/sync so
`SyncMode.NodeFromAnimRootMotion` has a deterministic place in the frame.

## 3D audio - `Viper.Game3D.Audio3D`

Positional audio over runtime `Audio3D` / `AudioListener3D` /
`AudioSource3D`. The listener follows the active camera by default.

```zia
class Audio3D {
    expose AudioListener3D listener;

    expose func listenerFollowCamera(enabled: Boolean)
    expose func setListenerPose(pos: Vec3, forward: Vec3, up: Vec3)
    expose func load(path: String) -> SoundClip3D
    expose func loadAsset(assetPath: String) -> SoundClip3D
    expose func playAt(clip: SoundClip3D, pos: Vec3)
    expose func playAttached(clip: SoundClip3D, e: Entity3D)
    expose func play2D(clip: SoundClip3D)
    expose func setAttenuation(refDist: Float, maxDist: Float)
}
```

`World3D.stepSimulation` calls the runtime audio binding sync after scene/body
sync so camera and attached-source positions are current.

## VFX presets - `Viper.Game3D.Effects3D`

One-call particle/decal effects over the existing particles and decals runtime.

```zia
Effects3D.Explosion(world: World3D, pos: Vec3)
Effects3D.Sparks(world: World3D, pos: Vec3, dir: Vec3)
Effects3D.Dust(world: World3D, pos: Vec3)
Effects3D.Smoke(world: World3D, pos: Vec3)
Effects3D.ImpactDecal(world: World3D, pos: Vec3, normal: Vec3)

class EffectRegistry3D {
    expose func addParticles(p: Particles3D, lifetime: Float)
    expose func addDecal(d: Decal3D)
    expose func update(dt: Float)
    expose func draw(canvas: Canvas3D, camera: Camera3D)
    expose func clear()
}
```

`World3D.drawEffects()` draws registered particles/decals after the scene and
before final overlay. Self-expiring effects remove themselves from the registry.

## Debug3D - `Viper.Game3D.Debug3D`

```zia
Debug3D.ShowOverlay(world, enabled: Boolean)
Debug3D.DrawAxes(world, origin: Vec3, size: Float)
Debug3D.DrawPhysics(world, enabled: Boolean)
Debug3D.DrawCameraInfo(world, enabled: Boolean)
Debug3D.DrawCapabilities(world, enabled: Boolean)
```

Debug text uses the final overlay path. Debug 3D primitives draw during the scene
or effects phase, depending on whether they require depth.

## Hello world

Target: a lit, walkable scene in about 15 lines.

```zia
module HelloGame3D;

bind Viper.Game3D as G;

func start() {
    var world = G.World3D.New("Demo", 1280, 720);
    G.Environment.Outdoor(world);

    world.spawn(G.Prefab.Ground(50.0, G.Materials.Plastic(0.6, 0.6, 0.62))
        .attachBody(G.BodyDef.StaticPlane(50.0).withLayer(G.Layers.World)));
    world.spawn(G.Prefab.Box(2.0, G.Materials.Metal(0.8, 0.2, 0.2))
        .setPosition(0.0, 1.0, 0.0));

    world.setCameraController(G.FirstPersonController.New(world));
    world.run((dt: Float) => {});
}
```

## Richer example - physics, collision FX, follow camera, fixed timestep

```zia
var bounce = world.audio.loadAsset("audio/bounce.wav");

var ball = G.Prefab.Sphere(0.5, 24, G.Materials.Rubber(0.9, 0.3, 0.1))
    .setPosition(0.0, 5.0, 0.0)
    .attachBody(G.BodyDef.Sphere(0.5, 1.0)
        .withLayer(G.Layers.Dynamic)
        .withMask(G.LayerMask.Of(G.Layers.World).include(G.Layers.Dynamic)));
world.spawn(ball);

world.spawn(G.Prefab.Ground(40.0, G.Materials.Plastic(0.5, 0.5, 0.5))
    .attachBody(G.BodyDef.StaticPlane(40.0).withLayer(G.Layers.World)));

world.onCollision(G.Layers.Dynamic, G.Layers.World, G.CollisionPhase.Enter,
    (evt: G.Collision3DEvent) => {
        world.audio.playAt(bounce, evt.point());
        G.Effects3D.Dust(world, evt.point());
    });

world.setCameraController(G.FollowController.New(world, ball, G.Vec3.New(0.0, 4.0, -8.0)));

world.runFixed(1.0 / 60.0, (step: Float) => {
    if (world.input.pressed(G.Keys.Space)) { ball.applyImpulse(0.0, 8.0, 0.0); }
});
```
