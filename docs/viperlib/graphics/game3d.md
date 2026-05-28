---
status: active
audience: public
last-verified: 2026-05-27
---

# Game3D
> Runtime-first 3D game helpers for code-first Viper and Zia projects.

**Part of [Viper Runtime Library](../README.md) / [Graphics](README.md)**

`Viper.Game3D` is implemented in the normal C runtime, beside the other
`Viper.*` runtime namespaces. It is not a source-level Zia helper library. Thin
Zia samples and convenience imports may exist, but the authoritative API,
ownership rules, diagnostics, and tests live in the runtime.

Game3D composes the lower-level `Viper.Graphics3D`, `Viper.Input`, physics, and
audio surfaces so simple games do not need to hand-wire canvases, cameras,
scenes, physics worlds, final-frame capture, layer masks, and common input.

---

## Quick Start

```zia
module HelloGame3D;

bind Viper.Game3D as Game3D;
bind Viper.Graphics as Graphics;
bind Viper.Math as Math;
bind Viper.Terminal as Terminal;

func start() {
    var world = Game3D.World3D.New("Hello Game3D", 640, 480);
    var env = Game3D.Environment.Outdoor(world);
    Game3D.EnvHandle.withWater(env, -0.05);

    var mat = Game3D.Materials.Metal(0.2, 0.7, 0.9);
    var cube = Game3D.Prefab.Box(1.0, mat);
    Game3D.Entity3D.setName(cube, "Cube");
    Game3D.Entity3D.setPosition(cube, 0.0, 0.75, 0.0);
    Game3D.World3D.spawn(world, cube);

    Game3D.PostFX.Crisp(world);
    Game3D.Quality.Apply(world, Game3D.QualityLevel.get_Balanced());
    Game3D.World3D.lookAt(world, new Math.Vec3(0.0, 0.0, 0.0));
    Game3D.World3D.beginFrame(world);
    Game3D.World3D.drawScene(world);
    Game3D.World3D.drawEffects(world);
    Game3D.World3D.endScene(world);

    var pixels = Game3D.World3D.captureFinalFrame(world);
    if (pixels == null || Graphics.Pixels.get_Width(pixels) != 640) {
        Terminal.Say("capture failed");
    }

    Game3D.World3D.present(world);
    Game3D.World3D.destroy(world);
}
```

This is intentionally free of common-case `Mat4` calls. `Entity3D` owns the raw
`SceneNode3D` transform, and `World3D` owns the normal frame order.

---

## Core Types

| Type | Purpose |
|------|---------|
| `World3D` | Owns the canvas, camera, scene, physics world, input wrapper, audio listener, post-FX registry, timing state, and entity registry |
| `Entity3D` | Spawnable game object with a scene node, optional mesh/material/body/anim handles, layer, collision mask, name, and child entities |
| `LayerMask` | Bitmask helper for collision and gameplay filtering |
| `BodyDef` | Runtime body recipe used by `Entity3D.attachBody` for common static, dynamic, trigger, and CCD bodies |
| `Collision3DEvent` | Entity-aware wrapper around raw `Graphics3D.CollisionEvent3D` enter/stay/exit records |
| `Assets3D` / `ModelTemplate` | Filesystem and package-aware model loading with cached reusable model templates |
| `Animator3D` | Game3D wrapper over `Graphics3D.AnimController3D` for play/crossfade/state-time/events/root-motion attachment |
| `Input3D` | Named keyboard, mouse, movement-axis, and look-axis helper over the runtime input state |
| `Sound3D` | World-owned audio helper with camera-follow listener, loading, 2D, positional, and attached-source playback |
| `EffectRegistry3D` / `Effects3D` | World-owned post-FX plus runtime particle/decal registry and one-call VFX presets |
| `FreeFlyController` | Spectator camera controller with WASD/arrow movement, vertical movement, mouse look, and mouse capture |
| `FirstPersonController` | FPS camera controller that can either move the camera directly or drive a `CharacterController3D` |
| `OrbitController` | Target-orbit camera with drag orbit, wheel zoom, distance clamp, and pitch clamp |
| `FollowController` | Late-update camera follower that tracks an entity's post-physics pose with an offset and damping |
| `CharacterController3D` | Game3D wrapper around `Viper.Graphics3D.Character3D` with camera-relative movement, gravity, jump speed, grounding, and entity sync |
| `Lighting` | One-call readable lighting rigs: studio, outdoor, night, interior, and clear |
| `Materials` | PBR-oriented material presets for plastic, metal, rubber, glass, emissive, unlit, and albedo-map workflows |
| `PostFX` | Backend-safe post-processing presets: cinematic, crisp, and none |
| `Quality` | Applies backend-safe quality policy for post-FX, shadows, and culling |
| `Prefab` | Mesh+material entity factories for boxes, spheres, cylinders, planes, and ground |
| `Environment` / `EnvHandle` | Outdoor/sunset/overcast/night scene setup with terrain, water, and fog chaining |
| `Debug3D` | Final-overlay diagnostics plus axes and physics wire debug rendering |

Constant classes are runtime-backed too: `Layers`, `BodyShape`, `SyncMode`,
`AlphaMode`, `ShadingModel`, `QualityLevel`, `CollisionPhase`, `Keys`, and
`MouseButtons`.

`ShadingModel` mirrors `Viper.Graphics3D.Material3D`: `Phong=0`, `Toon=1`,
`PBR=2`, `Unlit=3`, `Fresnel=4`, and `Emissive=5`.

---

## World3D Defaults

`World3D.New(title, width, height)` creates:

| Field | Backing runtime object |
|-------|------------------------|
| `canvas` | `Viper.Graphics3D.Canvas3D` |
| `camera` | `Viper.Graphics3D.Camera3D` |
| `scene` | `Viper.Graphics3D.Scene3D` |
| `physics` | `Viper.Graphics3D.Physics3DWorld` |
| `input` | `Viper.Game3D.Input3D` |
| `audio` | `Viper.Game3D.Sound3D` with a camera-aligned listener |
| `effects` | `Viper.Game3D.EffectRegistry3D` with a `PostFX3D` chain and particle/decal registry |
| `droppedFixedSteps` | Integer counter for fixed-step updates discarded by the spiral-of-death guard |

The constructor installs explicit default lighting, balanced backend-safe
quality, frustum culling, a readable camera, a neutral clear/ambient setup, and
the default audio listener. These are normal runtime choices, not hidden Zia
setup code.

`World3D.runFixed` caps the number of simulation steps processed by one rendered
frame. If a long frame would require more work than the cap, the dropped step
count is exposed through `World3D.droppedFixedSteps` for telemetry and tuning.

`World3D.NewWithCamera(title, width, height, fov, near, far)` uses the same
defaults with custom camera projection values.

---

## Presets And Prefabs

Phase 3 adds runtime-backed convenience for the setup code that made small 3D
games noisy:

| API | Purpose |
|-----|---------|
| `Lighting.Studio(world)` | Balanced key/fill lighting for object inspection and samples |
| `Lighting.Outdoor(world, sunDir)` | Sunlight and ambient sky for outdoor scenes |
| `Lighting.Night(world)` / `Interior(world)` / `Clear(world)` | Night, indoor, or empty lighting rigs |
| `Materials.Plastic/Metal/Rubber(r, g, b)` | Common PBR material presets |
| `Materials.Glass(r, g, b, alpha)` | Transparent, double-sided reflective material |
| `Materials.Emissive(r, g, b, intensity)` | Emissive material for lights, pickups, and UI-like meshes |
| `Materials.Unlit(r, g, b)` | Flat unlit material |
| `Materials.FromAlbedoMap(pixels)` | Textured material from a `Pixels` albedo map |
| `PostFX.Cinematic(world)` / `Crisp(world)` / `None(world)` | Backend-safe effect chains; cinematic keeps vignette subtle so demos stay readable at the screen edges |
| `Quality.Apply(world, QualityLevel.*)` | Applies quality, post-FX, frustum culling, and shadow policy safely |

Prefab factories return normal spawnable `Entity3D` objects with mesh and
material already wired:

```zia
var metal = Game3D.Materials.Metal(0.65, 0.68, 0.72);
var crate = Game3D.Prefab.BoxXYZ(1.0, 0.8, 1.4, metal);
Game3D.Entity3D.setPosition(crate, 0.0, 0.4, -2.0);
Game3D.World3D.spawn(world, crate);
```

Available prefab factories are `Box`, `BoxXYZ`, `Sphere`, `Cylinder`, `Plane`,
and `Ground`. `Ground` also sets the entity layer to `Game3D.Layers.World`.

---

## Environment

`Environment.Outdoor(world)`, `Sunset(world)`, `Overcast(world)`, and
`Night(world)` apply clear color, ambient/light setup, fog, and a basic ground
entity immediately. The returned `EnvHandle` can refine the scene:

```zia
var env = Game3D.Environment.Outdoor(world);
Game3D.EnvHandle.withTerrain(env, 96.0, 0.0);
Game3D.EnvHandle.withWater(env, -0.05);
Game3D.EnvHandle.withFog(env, 20.0, 160.0);
```

Terrain is represented as a spawnable ground entity with a static body in this
phase, so character/physics samples get a useful floor without manual collider
setup. Water is a transparent plane prefab; `withWater()` uses the most recent
terrain size when terrain has been configured, otherwise it falls back to the
default water size.

---

## Debug3D

`Debug3D` uses the same final overlay path as user HUDs, so its text is drawn
after post-FX rather than being bloomed, toned, or blurred.
On GPU post-FX backends, recorded final overlays are replayed into the backend
overlay target before `present_postfx`, so capture and presentation both
composite crisp overlays over the post-FX scene.

```zia
Game3D.Debug3D.ShowOverlay(world, true);
Game3D.Debug3D.DrawAxes(world, new Math.Vec3(0.0, 0.0, 0.0), 1.5);
Game3D.Debug3D.DrawPhysics(world, true);
Game3D.Debug3D.DrawCameraInfo(world, true);
Game3D.Debug3D.DrawCapabilities(world, true);
```

The overlay reports backend, FPS, requested/active quality, fallback state,
scene node/cull counts, physics body count, optional camera position, backend
capability bits, and whether physics wire debug is enabled. `drawEffects()`
draws axes and physics wire helpers during the 3D pass.

---

## Frame Order

The managed frame helpers use this order:

1. Poll or advance input/time.
2. Update `world.dt`, `world.elapsed`, and `world.frame`.
3. Run gameplay update if a native callback loop is being used.
4. Run the installed Game3D camera/controller `update`.
5. Advance spawned `Entity3D.anim` controllers and collect animation events.
6. Step physics.
7. Sync physics-owned nodes, animation root motion, and 3D audio bindings.
8. Update the effect registry and expire finished particle/decal effects.
9. Run the installed Game3D camera/controller `lateUpdate`.
10. Begin the frame and draw the scene.
11. Draw the effects registry.
12. End the scene.
13. Draw the final overlay if a native overlay callback is being used.
14. Finalize/present, or leave the finalized frame available for capture.

Controller `update` runs inside `stepSimulation(step)` before the physics step,
which means manual loops can still do:

```zia
if (Game3D.World3D.tick(world)) {
    // game code may adjust controller properties, spawn entities, etc.
    Game3D.World3D.stepSimulation(world, Game3D.World3D.get_dt(world));
    Game3D.World3D.beginFrame(world);
    Game3D.World3D.drawScene(world);
    Game3D.World3D.endScene(world);
    Game3D.World3D.present(world);
}
```

`lateUpdate` runs after physics and binding sync, so follow cameras observe the
final entity pose for the frame rather than the previous frame's transform.

Manual code can use the same pieces directly:

| Method | Purpose |
|--------|---------|
| `tick()` | Poll live input, sync canvas resize state, and advance world timing; returns false when the window should close |
| `stepSimulation(step)` | Store `world.dt`, advance `frame`/`elapsed` when called directly, then step controllers, animation, physics, scene/audio bindings, effect expiry, and late camera/controller work |
| `beginFrame()` | Clear and begin drawing with the world camera |
| `drawScene()` | Draw the world scene |
| `drawEffects()` | Draw effect-registry particles/decals plus debug axes/physics wires |
| `endScene()` | End the draw pass without presenting |
| `captureFinalFrame()` | Finalize post-FX/overlay and return `Pixels` |
| `present()` | Finalize if needed and present the frame |

For deterministic interpreted-Zia tests, use `runFramesOnly(frameCount, stepSec)`
or call the manual methods explicitly. `runFramesOnly` uses the synthetic clock
path and leaves the final frame capturable. `runFrames` / `runFramesOnly`
temporarily switch the backing canvas to synthetic input and fixed-clock timing
for callback, simulation, and render work, then restore the previous canvas
input source, clock source, and synthetic delta after each normally completed
frame and at the end of the run. The built-in run loops avoid double-counting
time because `tick()` / `runFrames()` own frame and elapsed-time accounting.

The raw `Canvas3D` finalization calls map to the Game3D frame helpers this way:

| Raw call | Meaning |
|----------|---------|
| `Canvas3D.End()` | Close the 3D scene pass. This does not replay final overlays, read pixels, or present. |
| `Canvas3D.FinalizeFrame()` | Idempotently apply post-FX and replay recorded final overlays. |
| `Canvas3D.ScreenshotFinal()` | Finalize the frame, then read back the post-FX-plus-overlay pixels. |
| `Canvas3D.Flip()` | Finalize the frame if needed, then present it. |

`World3D.endScene()`, `captureFinalFrame()`, and `present()` are the Game3D
wrappers for that contract.

`World3D.onResize(width, height)` updates the camera aspect and resizes the
owned `Canvas3D`, including backend render targets, so window callbacks and
manual resize paths keep the Game3D camera and Graphics3D output in sync.
`World3D.tick()` also observes native canvas size changes after polling so
platform window resizes update `world.Width`, `world.Height`, and the camera
aspect even when the app does not call `onResize()` directly.

---

## Callback Boundary

`World3D.run`, `runWithOverlay`, `runFixed`, `runFixedWithOverlay`,
`runFrames`, and `drawOverlay` are native callback-loop helpers. The callback
argument must be a native function pointer callable from C.

Interpreted Zia function references and closures are managed callback objects,
not C-callable function pointers. Passing one to these native callback-loop
methods traps with a `Game3D.World3D.*: callback must be a native function
pointer` diagnostic. Until a VM callback trampoline exists for this API, use
manual frame methods or `runFramesOnly` from interpreted Zia.

This boundary is covered by `g3d_test_game3d_runframes_callback_reject`.

---

## Entities, Layers, And Masks

`Entity3D.Of(mesh, material)` creates a spawnable entity with a raw
`SceneNode3D`. Transform helpers sanitize non-finite numbers before touching the
node and update an attached body only when the node sync mode is
`SyncMode.BodyFromNode`:

| Method | Purpose |
|--------|---------|
| `setPosition(x, y, z)` / `setPositionV(vec3)` | Set node position |
| `setScale(s)` / `setScaleXYZ(x, y, z)` | Set node scale |
| `setRotationEuler(xDeg, yDeg, zDeg)` | Set node orientation in degrees |
| `setMesh(mesh)` / `setMaterial(material)` | Replace render resources |
| `addChild(child)` | Parent another Game3D entity; reparents from an old Game3D parent and rejects self/cycle parenting |
| `setName(name)` | Name the entity and backing node for lookup |
| `setLayer(layer)` | Set gameplay/physics layer |
| `setCollisionMask(mask)` | Set the layer mask used by attached bodies |
| `attachBody(bodyDef)` | Create and attach a `Physics3DBody` from a `BodyDef` |
| `attachAnimator(animator)` | Attach an `Animator3D` or raw `AnimController3D` to the entity node |
| `position()` / `worldPosition()` | Read local/world position |
| `isSpawned()` / `isDestroyed()` | Inspect lifecycle state |

`World3D.spawn(entity)` attaches the entity node to the world scene and registers
the entity by name. `World3D.despawn(entity)` removes it from the registry,
scene, and physics world. Child Game3D entities are owned by their parent for
despawn purposes; raw imported child nodes remain part of the imported node
subtree. Adding a child to an already-spawned entity spawns that child into the
same world; adding a spawned child under an unspawned entity is rejected because
it would detach the child from its world scene.

Use `LayerMask.None()`, `LayerMask.All()`, `LayerMask.Of(layer)`,
`include(layer)`, and `includes(layer)` for readable filters. Layer values are
validated as single-bit masks. Physics query masks follow the same bit semantics:
`LayerMask.None()` matches no layers, while `LayerMask.All()` matches any layer.

`attachBody` also accepts a raw `Viper.Graphics3D.Physics3DBody` as an escape
hatch. The common path should use `BodyDef`, because it applies filters, node
binding, sync mode, and world registration consistently. The default
`BodyDef` sync mode is `NodeFromBody`: spawn seeds the body from the node once,
then simulation owns the body and writes the resulting pose back to the node.
Use `BodyFromNode` for scripted/authoritative transforms that should push into
physics on every transform setter.

---

## Assets3D

`Assets3D` loads `Model3D` assets into spawnable Game3D entities without manual
scene-node cloning:

```zia
var crate = Game3D.Assets3D.LoadModel("assets/crate.glb");
Game3D.Entity3D.setPosition(crate, 0.0, 0.5, -3.0);
Game3D.World3D.spawn(world, crate);

var enemyTemplate = Game3D.Assets3D.LoadModelTemplateAsset("models/enemy.glb");
var enemy = Game3D.ModelTemplate.instantiate(enemyTemplate);
Game3D.World3D.spawn(world, enemy);
```

| Method | Purpose |
|--------|---------|
| `LoadModel(path)` | Load a filesystem/development model and return a group `Entity3D` |
| `LoadModelAsset(assetPath)` | Load through the asset resolver first, with filesystem fallback for development |
| `LoadModelTemplate(path)` | Load or reuse a cached filesystem `ModelTemplate` |
| `LoadModelTemplateAsset(assetPath)` | Load or reuse a cached package-aware `ModelTemplate` |
| `Preload(path)` | Warm the filesystem template cache |
| `ClearCache()` | Release cached template entries and backing storage after in-flight loads finish |
| `ModelTemplate.instantiate()` | Clone the template root subtree into a group `Entity3D` |

Loaded entities are groups whose backing node is the instantiated model root.
The raw imported child nodes remain under that root and are not separate
Game3D child entities. Despawning the group removes the whole subtree from the
scene. If the imported root has a skeletal animation controller, `entity.anim`
is populated with a `Game3D.Animator3D` wrapper.

`LoadModelAsset` and `LoadModelTemplateAsset` use the runtime asset resolver, so
mounted packages and `asset://` paths work the same way as lower-level
`Model3D.LoadAsset`. Relative glTF buffers and textures resolve relative to the
model asset.

Filesystem template cache keys are canonicalized to absolute paths when
possible, and concurrent requests for the same key share the in-flight load
instead of importing the same model more than once. Waiting threads sleep on the
cache condition variable rather than polling while another thread finishes an
import.

---

## Animator3D

`Animator3D` wraps a lower-level `Viper.Graphics3D.AnimController3D` while
keeping the common game loop readable:

```zia
var controller = AnimController3D.New(skeleton);
AnimController3D.AddState(controller, "idle", idleClip);
AnimController3D.AddState(controller, "run", runClip);
AnimController3D.AddEvent(controller, "run", 0.25, "footstep");
AnimController3D.SetRootMotionBone(controller, rootBone);

var anim = Game3D.Animator3D.New(controller);
Game3D.Entity3D.attachAnimator(player, anim);
SceneNode3D.set_SyncMode(Game3D.Entity3D.get_node(player),
                         Game3D.SyncMode.get_NodeFromAnimRootMotion());
Game3D.Animator3D.play(anim, "run");
```

| Method / property | Purpose |
|-------------------|---------|
| `controller` | Raw `AnimController3D` escape hatch |
| `play(name)` | Play a named controller state |
| `crossfade(name, seconds)` | Blend to another named state |
| `setSpeed(name, speed)` | Change a state's playback speed |
| `isPlaying(name)` | Check the active base-layer state |
| `stateTime()` | Current base-layer playback time |
| `eventCount()` / `eventName(index)` | Events captured during the latest play/update frame |
| `update(dt)` | Manually advance an animator that is not spawned in a world |

Spawned entity animators are advanced automatically by `World3D.stepSimulation`
after camera/controller `update` and before physics. If the entity node uses
`SyncMode.NodeFromAnimRootMotion`, the normal scene binding sync consumes the
controller root-motion delta and moves the entity node deterministically in the
same frame.

`Entity3D.attachAnimator` accepts either a `Game3D.Animator3D` wrapper or a raw
`AnimController3D`. Raw controllers are wrapped automatically so `entity.anim`
always exposes the Game3D helper surface. Imported model templates with a root
animation controller use the same wrapper path.

`Animator3D.eventCount()` and `eventName(index)` are the supported
interpreted-Zia event path. Optional callback sugar such as `onAnimEvent` is
deferred until the VM has a callback trampoline for managed function objects.

---

## Sound3D And Effects3D

`World3D.audio` owns a runtime `SoundListener3D` that follows the world camera
by default. The listener can be detached for cutscenes, replays, or split-view
tests:

```zia
var audio = Game3D.World3D.get_audio(world);
Game3D.Sound3D.listenerFollowCamera(audio, false);
Game3D.Sound3D.setListenerPose(
    audio,
    new Math.Vec3(0.0, 2.0, 6.0),
    new Math.Vec3(0.0, 0.0, -1.0),
    new Math.Vec3(0.0, 1.0, 0.0));
```

| Audio API | Purpose |
|-----------|---------|
| `listener` | Raw `SoundListener3D` escape hatch |
| `listenerFollowCamera(enabled)` | Bind/unbind the listener from the world camera |
| `setListenerPose(pos, forward, up)` | Set a manual listener pose; `up` is reserved for future orientation support |
| `setAttenuation(refDist, maxDist)` | Store and apply Game3D playback attenuation defaults; sources stay full-volume through `refDist`, then fall linearly to silence at `maxDist` |
| `volume` | Default source/playback volume, clamped to 0..100 |
| `load(path)` / `loadAsset(assetPath)` | Load a `Viper.Sound.Sound` clip from filesystem or asset resolver |
| `playAt(clip, pos)` | Create and play a positional `SoundSource3D` at a `Vec3` |
| `playAttached(clip, entity)` | Create an `SoundSource3D` bound to the entity node, so it follows after scene/body sync |
| `play2D(clip)` | Play a non-positional clip and return the voice id |
| `clearSources()` | Stop and release sources created through this `Sound3D` helper |

`World3D.stepSimulation` syncs audio bindings after physics and scene sync, so
attached audio observes the same final entity transforms as follow cameras.

`World3D.effects` owns the existing `PostFX3D` chain plus a particle/decal
registry. The registry can be driven manually, but world stepping and
`drawEffects()` handle the normal update/draw path:

```zia
var effects = Game3D.World3D.get_effects(world);
Game3D.Effects3D.Explosion(world, hitPoint);
Game3D.Effects3D.ImpactDecal(world, hitPoint, hitNormal);
```

| Effects API | Purpose |
|-------------|---------|
| `postfx` | Raw `PostFX3D` escape hatch |
| `count` / `particlesCount` / `decalCount` | Registry diagnostics |
| `addParticles(particles, lifetime)` | Retain a `Particles3D` emitter and auto-remove it after `lifetime` seconds |
| `addDecal(decal)` | Retain a `Decal3D` until its own lifetime expires |
| `update(dt)` / `draw(canvas, camera)` / `clear()` | Manual registry control |
| `Effects3D.Explosion(world, pos)` | Additive burst with warm fire colors |
| `Effects3D.Sparks(world, pos, dir)` | Directional additive sparks |
| `Effects3D.Dust(world, pos)` | Short ground-impact dust puff |
| `Effects3D.Smoke(world, pos)` | Slower rising smoke puff |
| `Effects3D.ImpactDecal(world, pos, normal)` | Fading projected impact decal |

Collision events expose `point()` and `normal()` as `Vec3`, which makes
impact audio/VFX a direct event-buffer workflow:

```zia
var evt = Game3D.World3D.collisionEvent(world, Game3D.CollisionPhase.get_Enter(), 0);
Game3D.Sound3D.playAt(audio, bounceClip, Game3D.Collision3DEvent.point(evt));
Game3D.Effects3D.Dust(world, Game3D.Collision3DEvent.point(evt));
```

---

## BodyDef And Collision Events

`BodyDef` describes the body that `Entity3D.attachBody` should create:

```zia
var ground = Game3D.Prefab.Ground(40.0, Game3D.Materials.Rubber(0.25, 0.35, 0.25));
Game3D.Entity3D.attachBody(ground, Game3D.BodyDef.StaticPlane(40.0));
Game3D.World3D.spawn(world, ground);

var ball = Game3D.Prefab.Sphere(0.5, 24, Game3D.Materials.Plastic(0.9, 0.2, 0.2));
var body = Game3D.BodyDef.Sphere(0.5, 1.0);
Game3D.BodyDef.set_restitution(body, 0.35);
Game3D.BodyDef.set_useCCD(body, true);
Game3D.BodyDef.withMask(body, Game3D.LayerMask.Of(Game3D.Layers.get_World()));
Game3D.Entity3D.attachBody(ball, body);
Game3D.World3D.spawn(world, ball);
```

| API | Purpose |
|-----|---------|
| `BodyDef.Box(halfX, halfY, halfZ, mass)` | Dynamic box body |
| `BodyDef.Sphere(radius, mass)` | Dynamic sphere body |
| `BodyDef.Capsule(radius, height, mass)` | Dynamic capsule body |
| `BodyDef.StaticBox(halfX, halfY, halfZ)` | Static world-layer box |
| `BodyDef.StaticPlane(size)` | Static world-layer floor, implemented as a shallow box |
| `withLayer(layer)` / `withMask(mask)` | Apply collision layer/filter bits |
| `asTrigger()` | Mark the body trigger-only |
| `withSync(mode)` | Set the node/body sync policy |

`BodyDef` exposes `shape`, `mass`, `friction`, `restitution`, `isStatic`,
`isKinematic`, `isTrigger`, `useCCD`, `layer`, `mask`, and `syncMode`
properties. Dynamic body definitions inherit the entity's current layer unless
`withLayer` or `set_layer` is used. `StaticBox` and `StaticPlane` default to
`Game3D.Layers.World`. `StaticPlane(size)` is centered on the entity node, spans
`size` on X/Z, and uses a total thickness of `0.1` world units.

`World3D.collisionEventCount(phase)` and `collisionEvent(phase, index)` expose
the runtime enter/stay/exit buffers through `Collision3DEvent`:

```zia
Game3D.World3D.stepSimulation(world, 0.016);
var count = Game3D.World3D.collisionEventCount(world, Game3D.CollisionPhase.get_Enter());
if (count > 0) {
    var evt = Game3D.World3D.collisionEvent(world, Game3D.CollisionPhase.get_Enter(), 0);
    var other = Game3D.Collision3DEvent.other(evt, ball);
    var point = Game3D.Collision3DEvent.point(evt);
}
```

The wrapper exposes `phase`, `a`, `b`, `raw`, `isTrigger`, `relativeSpeed`,
`normalImpulse`, `point()`, `normal()`, and `other(entity)`. `CollisionPhase.Any`
iterates enter, stay, and exit records. Optional world/entity collision callback
sugar remains deferred until the VM callback trampoline policy is implemented;
polling the event buffers is the supported interpreted-Zia path.

---

## Input3D

`Input3D` reads the same keyboard and mouse state updated by `Canvas3D.Poll()` or
the synthetic input path. Calling `update()` snapshots key/button transitions,
mouse deltas, and wheel motion for the current Game3D frame, so later polling or
synthetic input changes do not mutate controller decisions already made for that
frame.

| Method / property | Purpose |
|-------------------|---------|
| `lookSensitivity` | Multiplier for `lookAxis()` |
| `update()` | Synchronize input helper state |
| `isDown(key)` / `pressed(key)` / `released(key)` | Keyboard queries |
| `mouseDelta()` | Current mouse movement as `Vec2` |
| `mouseButton(button)` / `mousePressed(button)` | Mouse button queries |
| `wheelY()` | Vertical wheel delta |
| `moveAxis()` | WASD/arrow movement as a normalized `Vec3` |
| `lookAxis()` | Mouse look as `Vec2`, scaled by `lookSensitivity` |
| `captureMouse()` / `releaseMouse()` | Forward to the active mouse capture policy |

Use `Game3D.Keys` and `Game3D.MouseButtons` instead of hard-coded integer input
codes in game code.

---

## Camera And Character Controllers

Install a built-in controller with `World3D.setCameraController(controller)`.
The current runtime accepts the built-in Game3D controller objects listed here;
it does not yet invoke interpreted Zia interface objects as native C callbacks.

```zia
var world = Game3D.World3D.New("Controller Demo", 960, 540);
var freeFly = Game3D.FreeFlyController.New(world);
Game3D.FreeFlyController.set_speed(freeFly, 8.0);
Game3D.World3D.setCameraController(world, freeFly);
Game3D.World3D.runFramesOnly(world, 1, 0.016);
```

`FreeFlyController.New(world)` is the quickest camera for debug views and
editor-like movement. It reads `Input3D.moveAxis()` and the frame's snapshotted
mouse deltas, moves through the camera basis, and can capture or release mouse input with
`captureMouse()` / `releaseMouse()`.

`FirstPersonController.New(world)` uses the same look controls. Without a
character controller it moves the camera directly. With `character` set, it
updates camera orientation first, drives `CharacterController3D.update(...)`
before physics, and late-updates the camera eye to the character position.

```zia
var player = Game3D.Entity3D.New();
Game3D.Entity3D.setPosition(player, 0.0, 1.0, 4.0);
Game3D.World3D.spawn(world, player);

var character = Game3D.CharacterController3D.New(world, player, 0.32, 1.8, 70.0);
var fps = Game3D.FirstPersonController.New(world);
Game3D.FirstPersonController.set_character(fps, character);
Game3D.FirstPersonController.set_speed(fps, 5.0);
Game3D.World3D.setCameraController(world, fps);
```

`CharacterController3D` exposes `speed`, `jumpSpeed`, `gravity`, `teleport(x,y,z)`,
and `grounded()`. It owns a lower-level `Viper.Graphics3D.Character3D`, binds it
to the world's physics world, moves it with swept-slide collision, and mirrors
the character position back to its Game3D entity.

`OrbitController.New(world, target)` takes either a `Vec3` target position or an
`Entity3D` target. Entity targets are resolved to world position during
`lateUpdate`. Holding the left mouse button and dragging changes yaw/pitch,
wheel input changes distance, and `lateUpdate` places the camera on the orbit.

`FollowController.New(world, entity, offset)` tracks an entity after physics.
Set `damping` to `0.0` for a snap follow, or a positive value for exponential
smoothing.

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| `LoadModelAsset` or `Sound3D.loadAsset` returns `null` | Run from the project root or package the asset path in `viper.project`; source-tree samples use `asset assets assets` or repository-relative fixture paths. |
| Final overlay pixels look post-processed | Draw HUD after `World3D.endScene()` with `Canvas3D.BeginOverlay()` / `EndOverlay()`, then call `captureFinalFrame()` or `present()`. |
| Interpreted Zia callback loop traps | Use manual `tick`/`stepSimulation`/frame methods or `runFramesOnly`; native callback loops require C-callable function pointers. |
| Software backend disables a requested quality feature | Inspect `Canvas3D.get_QualityActive()` and `get_QualityFallback()`; `Quality.Apply` avoids unsupported shadow/post-FX paths. |
| A body does not collide with another body | Verify the entity layer, `BodyDef.withLayer`, and `BodyDef.withMask`; masks must include the other body layer. |
| A handle fails after teardown | Destroy the `World3D` last. Spawned entities, effects, and audio source bindings are owned by the world. |

---

## Tests

The Game3D runtime is covered by:

| Test | Coverage |
|------|----------|
| `test_rt_game3d` | C runtime contracts for constants, masks, input, world defaults, spawn/despawn, collision-event clearing, native callback loops, overlay hooks, final capture, synthetic controller input, orbit/follow late update, first-person character movement, material presets, prefabs, lighting, quality, environment, post-FX, debug helpers, Animator3D root motion/events, Sound3D helpers, and Effects3D presets/expiry |
| `g3d_test_game3d_world_probe` | Zia construction, default subsystems, layer masks, entity spawn/find/despawn, resize/aspect, manual frame path, final capture, and destroy |
| `g3d_test_game3d_runframes_probe` | Zia deterministic `runFramesOnly`, dt/elapsed/frame accounting, and final capture |
| `g3d_test_game3d_runframes_callback_reject` | Interpreted Zia callback rejection diagnostic for native callback-loop APIs |
| `g3d_test_game3d_camera_controllers_probe` | Zia free-fly synthetic input, orbit drag/zoom, and follow camera post-physics tracking |
| `g3d_test_game3d_character_controller_probe` | Zia first-person character movement and late-update camera alignment |
| `g3d_test_game3d_presets_probe` | Zia material/prefab presets, lighting, quality fallback, post-FX, environment chaining, physics body setup, and final-overlay debug capture |
| `g3d_test_game3d_assets_probe` | Zia `Assets3D` filesystem/asset loading, cached templates, and model-template instantiation |
| `g3d_test_game3d_physics_probe` | Zia `BodyDef` static/dynamic body attachment, CCD/filter flags, gravity, and collision production |
| `g3d_test_game3d_collision_probe` | Zia entity-aware collision wrappers for enter/stay/exit and trigger events |
| `g3d_test_game3d_anim_probe` | Zia Animator3D play/crossfade/state-time/events, entity attachment, raw controller wrapping, and root-motion world stepping |
| `g3d_test_game3d_sound_probe` | Zia listener follow/manual pose, attenuation defaults, positional playback, attached-source sync, 2D playback, and source cleanup |
| `g3d_test_game3d_effects_probe` | Zia Effects3D presets, registry diagnostics, draw path, auto-expiry, manual particle/decal registration, and collision-triggered VFX |
| `g3d_test_game3d_docs_snippets` | Copy-paste docs surfaces for setup, presets, assets, physics, audio/VFX, deterministic frame helpers, and manual final-frame capture |
| `g3d_walk_min_visual_probe` | Game3D sample final-frame baseline, crisp overlay, directional lighting, and grounded synthetic first-person movement |
| `g3d_game3d_hello` | <=20-line hello-world scene with lighting, walkable ground, first-person character, and no `Mat4` |
| `g3d_game3d_starter_probe` | Starter project deterministic movement, package-aware model asset, final capture, and grounded character path |
| `g3d_game3d_starter_package_dry_run` | Starter `viper.project` asset packaging layout |
| `g3d_game3d_showcase` | Full-stack sample smoke, software final-frame structural/HUD assertion, asset/audio/VFX/camera/physics/animation integration, and deterministic replay |
| `g3d_game3d_bowling_setup` | Bowling setup migration smoke and deterministic replay |

Run the focused set with:

```sh
ctest --test-dir build -R 'test_rt_game3d|g3d_test_game3d_' --output-on-failure
```

Run the broader 3D regression set with:

```sh
ctest --test-dir build -L graphics3d --output-on-failure
```

---

## Current Boundaries

The core world/entity/input layer, built-in camera/character controllers,
presets, prefabs, environment/debug helpers, `Assets3D`, `BodyDef`,
entity-aware collision event wrappers, `Animator3D`, `Sound3D`, and `Effects3D`
now live in the C runtime.
`examples/3d/walk_min.zia`, `examples/3d/game3d_starter/`, and
`examples/3d/game3d_showcase/` are the current code-first samples. The bowling
setup migration lives at `examples/games/3dbowling/game3d/`.

Use the lower-level `Viper.Graphics3D` and `Viper.Sound` APIs as escape hatches
when a sample needs behavior outside the Game3D convenience layer.
