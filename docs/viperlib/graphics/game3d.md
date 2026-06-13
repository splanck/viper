---
status: active
audience: public
last-verified: 2026-06-01
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

Writable `Viper.Game3D` properties expose both property accessors and
method-call setters. A writable lower-camel property such as `speed` or
`lookSensitivity` has a `set_speed`/`set_lookSensitivity` runtime accessor and a
matching `setSpeed(...)`/`setLookSensitivity(...)` class method.

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
| `stream` | Lazily created `Viper.Game3D.WorldStream3D` owned by the world |
| `droppedFixedSteps` | Integer counter for fixed-step updates discarded by the spiral-of-death guard |
| `entityCount` | Spawned `Entity3D` objects currently owned by the world |
| `bodyCount` | Physics bodies currently registered through spawned entities |
| `drawCount` | Main 3D draw submissions queued by the latest ended frame |
| `visibleNodeCount` | Drawable scene nodes submitted by the latest scene draw |
| `occludedDrawCount` | Draw submissions skipped by latest visibility culling |
| `streamResidentBytes` | Resident byte estimate from the world-owned stream controller |
| `workerCount` | Internal deterministic job worker count, clamped to `1..64` |
| `jobsEnabled` | True when `workerCount > 1`; false when the world runs single-threaded |
| `floatingOrigin` | Enables camera-relative rebasing for large-coordinate scenes |
| `worldOrigin` | Accumulated world-space offset applied by floating-origin rebases |

The constructor installs explicit default lighting, balanced backend-safe
quality, frustum culling, a readable camera, a neutral clear/ambient setup, and
the default audio listener. These are normal runtime choices, not hidden Zia
setup code.

`World3D.runFixed` caps the number of simulation steps processed by one rendered
frame. If a long frame would require more work than the cap, the dropped step
count is exposed through `World3D.droppedFixedSteps` for telemetry and tuning.
The editor/perf counters (`entityCount`, `bodyCount`, `drawCount`,
`visibleNodeCount`, `occludedDrawCount`, and `streamResidentBytes`) are
read-only wrappers over the owned scene, canvas, physics world, and stream, so a
tool can inspect one world handle without reaching through each subsystem.

`Game3D.Diagnostics` exposes process-wide degradation counters for rare runtime
fallbacks that keep execution correct but indicate pressure or lost fidelity:
`BroadphaseFallbackCount`, `CcdClampedFrames`, `CcdClampedBodies`,
`AnimEventsDropped`, `AudioVoicesEvicted`, `NavGridFallbacks`, and
`StaleEntityCalls`. `Reset()` clears the process aggregates. `Summary()` returns
stable `name=value` lines in that order, omitting zero counters and returning
`""` when clean. Smoke probes can print `Game3D.Diagnostics.Summary()` and
assert it is empty.

`Viper.Graphics3D.Physics3DWorld.BroadphaseFallbackCount` reports the matching
per-world broadphase fallback total. CCD inspection remains available through
`LastCCDRequestedSubsteps`, `LastCCDSubsteps`, `CCDSubstepClampedCount`, plus
`LastCCDClampedBodyCount` and `CCDSubstepClampedBodyCount` for affected-body
totals.

`World3D.setWorkerCount(count)` controls the worker budget reserved for internal
3D jobs. `1` disables threaded jobs while preserving deterministic behavior;
values above `1` allow systems that opt into ordered internal jobs to use
workers. The runtime lazily creates a world-owned worker pool for eligible
batches; animator updates already use this path and are covered by single-worker
vs. multi-worker parity tests. User-authored job callbacks are not part of the
Game3D surface.

`World3D.setOriginRebaseThreshold(meters)` configures the floating-origin
distance threshold. With `floatingOrigin` enabled, the world recenters around
the active camera when it crosses that threshold, calls
`Scene3D.RebaseOrigin()` for scene root subtrees, shifts physics bodies by the
same delta, shifts the explicit audio listener pose, and rebases world-owned
effect-registry particles and decals. The absolute offset accumulates in
`worldOrigin`. `World3D.rebaseOrigin(dx, dy, dz)` exposes the same cross-system
boundary for explicit world-streaming or sector handoff code and must be called
between frames; calling it during an active `beginFrame`/`endScene` pass traps
before state changes. During `World3D.beginFrame`, the flag also enables Canvas3D
camera-relative upload for
double-precision `DrawMesh` model matrices, identity-matrix raw meshes built
through `Mesh3D.AddVertex`, generated `Particles3D` / `Sprite3D` / decal
billboard vertices, and point/spot light positions, so backend float payloads
stay near the camera. With the flag off, bounded scenes use the unchanged
absolute-upload path. The local Game3D CTests include a software final-frame
parity check between a near-origin scene and the same scene after a 50 km
floating-origin rebase, plus a byte-identical bounded-scene check after the
feature is toggled back off. `g3d_bounded_no_regression_probe` also runs the
existing `walk_min.zia` bounded sample with scale flags explicitly off and
checks exact final-frame pixel and captured-state parity.

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
after `apply_postfx` and before `present` when the backend exposes the split
path. Legacy backends still use `present_postfx` with the overlay target, so
capture and presentation both composite crisp overlays over the post-FX scene.

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
| `tick()` | Poll live input, sync backing-window resize state, and advance world timing; returns false when the window should close |
| `stepSimulation(step)` | Clamp invalid, zero, negative, and overlarge steps into the safe dt range; store `world.dt`, advance `frame`/`elapsed` when called directly, then step controllers, animation, physics, scene/audio bindings, effect expiry, and late camera/controller work |
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
for callback, simulation, and render work, keep synthetic-held keys/buttons down
across the whole deterministic run, then restore the previous canvas input
source, clock source, and synthetic delta after the run completes. If a native
`runFrames` callback traps, the synthetic canvas state is restored before the
original trap is rethrown. The built-in run loops avoid double-counting time
because `tick()` / `runFrames()` own frame and elapsed-time accounting.

For worker-parity probes, set `World3D.setWorkerCount(world, 1)` and a
multi-worker value before separate `runFramesOnly` replays and compare the final
state. Internal jobs must merge results deterministically, so worker count should
not affect simulation results.

For future simulation-touching changes, rerun the full determinism gate: the
Game3D worker-count replay tests, the ordered-merge surface probe, the native
Zia promise tests, `test_codegen_env_is_native`, and `test_crosslayer_arith`.
Changes that alter IL, VM, or native codegen semantics used by simulation also
need the GATE-009 ADR/proof note.

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
aspect even when the app does not call `onResize()` directly. Game3D tracks the
backing window size, not a temporary `RenderTarget3D` bound on the canvas.

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

The missing piece is a VM-owned re-entrant callback ABI, not a Game3D event
buffer. `BytecodeVM::exec` is a fresh top-level invocation that resets bytecode
execution state, so Game3D cannot safely call it while a runtime call is already
on the VM stack. A future trampoline needs to expose a VM-managed function or
closure invocation entry point that preserves the active frame and handles
captured closure environments. Until then, pollable collision/animation/event
buffers and manual frame loops are the supported interpreted-Zia surface.

This boundary is covered by `g3d_test_game3d_runframes_callback_reject`.

---

## Entities, Layers, And Masks

`Entity3D.Of(mesh, material)` creates a spawnable entity with a raw
`SceneNode3D`. `Entity3D.FromNode(rootNode)` wraps an existing `SceneNode3D`
hierarchy, which is the preferred bridge for imported or procedurally assembled
subtrees that should move through Game3D spawning without cloning. If the root
node has a non-empty name, the wrapper entity inherits it so
`World3D.findEntity(name)` and `World3D.findNode(name)` agree on the imported
root. Raw child nodes stay raw scene nodes: they render and participate in scene
lookup as part of the subtree, but they are not separate Game3D entities.
Transform helpers sanitize non-finite numbers before touching the node and update
an attached body only when the node sync mode is `SyncMode.BodyFromNode`:

| Method | Purpose |
|--------|---------|
| `FromNode(rootNode)` | Wrap an existing `SceneNode3D` hierarchy as a spawnable group entity |
| `setPosition(x, y, z)` / `setPositionV(vec3)` | Set node position |
| `setScale(s)` / `setScaleXYZ(x, y, z)` | Set node scale |
| `setRotationEuler(xDeg, yDeg, zDeg)` | Set node orientation in degrees |
| `setMesh(mesh)` / `setMaterial(material)` | Replace render resources |
| `setMeshRecursive(mesh)` | Replace mesh handles on every drawable raw node in the entity subtree; if none are drawable, assign the root |
| `setMaterialRecursive(material)` | Replace material handles on the root and every raw node in the entity subtree |
| `addChild(child)` | Parent another Game3D entity; reparents from an old Game3D parent and rejects self/cycle parenting |
| `setName(name)` | Name the entity and backing node for lookup |
| `setLayer(layer)` | Set gameplay/physics layer |
| `setCollisionMask(mask)` | Set the layer mask used by attached bodies |
| `attachBody(bodyDef)` | Create and attach a `Physics3DBody` from a `BodyDef` |
| `attachAnimator(animator)` | Attach an `Animator3D` or raw `AnimController3D` to the entity node |
| `position()` / `worldPosition()` | Read local/world position |
| `isSpawned()` / `isDestroyed()` | Inspect lifecycle state |
| `isGroup()` | True when the entity wraps an imported/`FromNode` multi-node group rather than a single primitive |

`World3D.spawn(entity)` attaches the entity node to the world scene and registers
the entity by name. `World3D.despawn(entity)` removes it from the registry,
scene, and physics world, and the retained entity handle becomes stale.
Destroying a world also marks any retained entities from that world stale. Stale
entity getters return neutral values (`0`, `null`, or origin vectors), mutators
including `attachAnimator` no-op, and each stale API call increments
`Game3D.Diagnostics.StaleEntityCalls`; invalid non-entity handles still trap.
`isSpawned()` and `isDestroyed()` remain available for lifecycle inspection.
Child Game3D entities are owned by their parent for despawn purposes; raw
imported child nodes remain part of the imported node subtree. Adding a child to
an already-spawned entity spawns that child into the same world; adding a
spawned child under an unspawned entity is rejected because it would detach the
child from its world scene.

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
physics on every transform setter. A raw body can only belong to one spawned
entity in a world; `spawn` and `attachBody` reject attempts to share it so
collision events and physics ownership stay unambiguous. Reattaching the same
spawned raw body to the same entity is a no-op for world registration; it keeps
the existing physics-world body and body index stable.

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
| `LoadModelAsync(path)` | Return an `AssetHandle3D` for a filesystem/development model entity |
| `LoadModelAssetAsync(assetPath)` | Return an `AssetHandle3D` for a package-aware model entity |
| `LoadModelTemplateAsync(path)` | Return an `AssetHandle3D` for a cached filesystem `ModelTemplate` |
| `LoadModelTemplateAssetAsync(assetPath)` | Return an `AssetHandle3D` for a cached package-aware `ModelTemplate` |
| `SetResidencyBudget(bytes)` | Bound the shared template cache by estimated resident bytes; negative means unlimited |
| `GetResidentBytes()` | Report estimated resident bytes held by the shared template cache |
| `SetResidencyHint(template, priority, distance)` | Bias cached-template eviction so higher-priority and nearer templates survive pressure first |
| `SetUploadBudget(bytes)` | Bound each async asset commit drain by decoded texture bytes; negative means unlimited |
| `Evict(handle)` | Drop the cached template entry backing a ready template `AssetHandle3D` |
| `Preload(path)` | Start a background filesystem template-cache warm |
| `PreloadAsset(assetPath)` | Start a background package-aware template-cache warm |
| `ClearCache()` | Release cached template entries and prevent older preload jobs from repopulating the cache |
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

`AssetHandle3D` exposes `ready`, `progress`, `error`, `cancel()`,
`getEntity()`, and `getTemplate()`. Handles start with `ready == false` and
`progress == 0.0`; first observation through `ready`, `progress`, `error`,
`getEntity()`, or `getTemplate()` services the process-wide asset commit queue
and starts worker staging/loading for valid uncached model requests. While the
worker is running, `ready` remains false and repeated observations keep draining
committed results. On success, progress becomes `1.0` and `error == ""`. Entity loaders
return an entity from `getEntity()` and `null` from `getTemplate()`; template
loaders do the inverse. Cached template handles can complete immediately on
first observation.

Missing filesystem paths and missing asset-manager paths become terminal
load-error handles on observation (`"cannot read file"` or `"asset not found"`)
rather than trapping. Existing files with unsupported model extensions complete
with `"unsupported file extension"`. Malformed glTF JSON or other importer
failures complete through the worker/commit path with `"failed to load model"` or
`"failed to load model asset"`. Calling `cancel()` before worker completion makes
the handle terminal with `error == "cancelled"` and no result. Calling `cancel()`
on a completed handle is a no-op, which keeps retrieved results stable.

Async handles accept the same model extensions as blocking `Model3D.Load`:
`.gltf`, `.glb`, `.fbx`, `.obj`, `.stl`, and `.vscn`. glTF/GLB assets use the
worker preload path described below; the other formats are validated by the
handle and completed through the main-thread commit path so callers can use one
async API for mixed model libraries.

Current worker-backed async model loading stages glTF/GLB root bytes plus
external, data URI, and bufferView-backed buffer/image payloads on a runtime
worker. PNG, BMP, JPEG, and GIF image payloads in that preload bundle are decoded on the
worker into raw RGBA POD; the main-thread commit queue prepares those bytes into
`Pixels` objects in bounded slices, touching content generation once the image is
complete. Static, skinned, and morph-target glTF triangle topology
primitives (triangle lists, strips, and fans) with positions, optional normals,
sparse accessor overrides, and `JOINTS_0`/`WEIGHTS_0`/`JOINTS_1`/`WEIGHTS_1`
attributes are decoded into raw `Mesh3D` POD on the worker, then committed into
runtime mesh objects on the main thread; missing normals are regenerated during
commit, skin joint remapping still runs after skeleton import, and morph-target
delta payloads become attached `MorphTarget3D` objects during commit. The worker preload also
rejects missing required buffer
payloads, accessor ranges that overrun their declared bufferViews, and corrupt
required PNG/BMP/JPEG/GIF texture image payloads before the main-thread commit
queue builds the `Model3D`/`ModelTemplate`/`Entity3D` result. Blocking glTF
loads reject the same corrupt required data-URI texture images instead of
silently dropping the material map.
External `.bin` buffers can be loaded from the staged bundle even if the source
file disappears before commit. `Assets3D.Preload(path)` and
`Assets3D.PreloadAsset(assetPath)` schedule the same template-mode worker path as
background cache warms for filesystem and package-aware keys respectively.
`World3D.tick` and simulation steps drain the asset commit queue with a fixed
per-frame item budget plus a decoded-texture byte budget so preload commits do
not require polling a returned handle. `Assets3D.SetUploadBudget(bytes)` changes
that byte budget: negative means unlimited, `0` pauses positive-cost decoded
texture slices, and positive budgets advance large decoded images across
multiple queue drains before the final model/template publish. Pair this with
`Canvas3D.SetTextureUploadBudget`, `TextureUploadBytes`, and
`TextureUploadPendingBytes` when profiling frames where decoded commits turn
into backend texture-cache uploads. That Canvas3D budget row-slices
Pixels-backed 2D material texture and cubemap uploads on Metal/OpenGL/D3D11 and
submits native compressed `TextureAsset3D` resident mip blocks on capable GPU
backends. The backend pending-byte counters return to zero when final row slices
and native mip submissions drain, and the open-world hitch probe verifies
resident-byte cache churn returns to zero after blocking and async clears. Its
GPU opt-in CTest also records native compressed upload pressure; the local
macOS/Metal lane uses ASTC and reports `native_zero_pending_bytes=16` followed
by `native_upload_bytes=16` once a positive texture-upload budget is restored.
The same run records `native_raw_rgba_bytes=64`,
`native_compressed_bytes=16`, `native_ram_reduction_pct=75`,
`native_vram_reduction_pct=75`, and a checked final-frame tolerance.
Non-glTF formats still enqueue a main-thread load request.

`ClearCache()` advances the template-cache generation. Any `Preload` /
`PreloadAsset` job that was already in flight may still finish, but it publishes
only to its internal handle and does not reinsert a stale template into the
fresh cache generation. If another thread is actively publishing cache entries,
`ClearCache()` waits briefly for loading entries to settle; on timeout it leaves
the current cache intact rather than clearing entries while publishers still own
them.

`SetResidencyBudget(bytes)` applies to the shared `ModelTemplate` cache. The
budget uses a conservative CPU/GPU-resource estimate that includes mesh buffers,
model metadata, and decoded material texture pixels, treats a negative value as
unlimited, and considers only non-loading entries for eviction. A budget of `0`
keeps returned templates alive for callers that hold them, but prevents them
from remaining in the shared cache. `SetResidencyHint(template, priority,
distance)` annotates cached templates for streaming policy: higher priority
survives pressure before lower priority, and nearer templates survive farther
templates when priority ties; unhinted templates keep the previous LRU behavior.
`GetResidentBytes()` returns the same cache counter, so tests and tooling can
assert that blocking and async load/clear churn returns resident template bytes
to zero. `Evict(handle)` is explicit cache eviction for ready template handles;
entity handles are accepted as a stable no-op.

---

## WorldStream3D

`WorldStream3D` is the Game3D handle for world-partition and terrain-streaming
state. `World3D.stream` lazily creates a stable world-owned stream handle for
the common case; `WorldStream3D.New(world)` still creates a separate stream
controller when tooling or tests need an isolated handle:

```zia
var stream = world.stream;
Game3D.WorldStream3D.mountCells(stream, "assets/world/cells.vscn");
Game3D.WorldStream3D.mountTiledTerrain(stream, "assets/world/terrain.vscn");
Game3D.WorldStream3D.setRadii(stream, 256.0, 320.0);
Game3D.WorldStream3D.setCenter(stream, player.worldPosition);
Game3D.WorldStream3D.update(stream, world.dt);
```

`setCenter`, `setRadii`, `setResidencyBudget`, `mountCells`,
`mountTiledTerrain`, and `update` are registered and tested. `mountCells`
parses a VSCN streaming manifest with a `cells` array:

```json
{"cells":[{"name":"town_00","path":"cells/town_00.vscn","center":[0,0,0],"radius":64,"bytes":65536}]}
```

Cell paths are resolved relative to the manifest, each resident cell loads its
`.vscn` subtree into the world scene, and `update` loads/unloads cells around
the current center using load/unload radii. `mountTiledTerrain` parses a
terrain manifest with a `tiles` array using the same `name`, `path`, `center`,
`radius`, and `bytes` fields, plus optional `width`, `depth`, `scale`, and
`heightmap` for the `Terrain3D` payload:

```json
{"tiles":[{"name":"terrain_00","path":"terrain/terrain_00.tile","heightmap":"terrain/terrain_00.height","center":[0,0,0],"radius":128,"bytes":262144,"width":129,"depth":129,"scale":[4,32,4],"material":"grass","collision":{"enabled":true,"layer":1,"mask":-1},"navArea":"meadow","traversalCost":1.0,"sidecar":"terrain/terrain_00.bin"}]}
```

Terrain and heightmap paths are resolved relative to the manifest and terrain
tile telemetry loads/unloads deterministically around the current center.
Resident terrain tiles instantiate `Terrain3D` payloads using the manifest
dimensions and scale; an optional `heightmap` sidecar uses a small text format,
`viper-heightmap-v1 <width> <depth>` followed by normalized height values, and
is applied through the existing 16-bit `Terrain3D.SetHeightmap` path. The
world-owned stream renders resident terrain tiles during `World3D.drawScene`
using their manifest-centered world positions. Each resident terrain tile also
owns an invisible `*_heightfield_collider` entity
with a static `Collider3D.NewHeightfield` body, so physics residency follows
terrain residency and unloads with the tile. Resident tiles also spawn hidden
mesh-only `*_navmesh_source` nodes so `World3D.bakeNavMesh` includes streamed
terrain through the existing scene bake path. When two resident terrain tiles
share a full manifest edge, `WorldStream3D` stitches the adjacent border samples
in world-height space, invalidates cached terrain LOD meshes, and rebuilds the
tile collider/nav sources from the stitched height grid. This keeps the
single-`Terrain3D.New` 4096-sample cap intact while allowing larger streamed
worlds to be composed from multiple tiles.

Both `cells[]` and `tiles[]` may carry authored metadata for runtime/editor
handoff: `material` names the authored surface/material, `layer` or
`collisionLayer` selects a Game3D layer bit, `collisionMask` or nested
`collision.mask` selects the collision mask, nested `collision.enabled=false`
suppresses generated terrain heightfield colliders, `navArea` and
`traversalCost` describe navigation semantics, and `sidecar` / `binarySidecar`
points at optional binary metadata resolved relative to the manifest. Cell
layer/mask metadata is applied to the spawned root entity; terrain collision
metadata is applied to generated heightfield bodies. The metadata is also
available through typed `getCell*` and `getTerrainTile*` inspection methods, each
taking the manifest index returned by `getCellCount()` / `getTerrainTileCount()`
and mirroring the manifest fields. Resident-only payload access uses the
explicit `getResidentTerrainTile(index)` method:

| Method | Signature | Description |
|--------|-----------|-------------|
| `getCellMaterial(i)` / `getTerrainTileMaterial(i)` | `String(Integer)` | Authored surface/material name (`""` if unset) |
| `getCellLayer(i)` / `getTerrainTileLayer(i)` | `Integer(Integer)` | Game3D collision layer bit |
| `getCellCollisionMask(i)` / `getTerrainTileCollisionMask(i)` | `Integer(Integer)` | Collision mask |
| `getCellCollisionEnabled(i)` / `getTerrainTileCollisionEnabled(i)` | `Boolean(Integer)` | Whether generated colliders are enabled |
| `getCellNavArea(i)` / `getTerrainTileNavArea(i)` | `String(Integer)` | Navigation area name |
| `getCellTraversalCost(i)` / `getTerrainTileTraversalCost(i)` | `Double(Integer)` | Navigation traversal cost |
| `getCellSidecar(i)` / `getTerrainTileSidecar(i)` | `String(Integer)` | Optional binary sidecar path (`""` if unset) |
| `getCellSidecarBytes(i)` | `Integer(Integer)` | Resident bytes of the cell's loaded binary sidecar payload (0 if none or unloaded) |

When a resident cell declares a `sidecar` path, the world stream loads that file's
bytes as opaque cell metadata, counts them in the cell's resident-byte budget, and
frees them when the cell unloads. A missing, empty, or oversized sidecar is
recoverable (zero bytes, no error).

`getResidentTerrainTile(index)` returns the nth currently resident terrain
payload or `null`; the `getCell*` and `getTerrainTile*` metadata methods remain
manifest-indexed even when earlier entries are unloaded. The telemetry
properties (`residentCellCount`,
`residentTerrainTileCount`, `pendingRequestCount`, and `residentBytes`) reflect
resident cell payloads plus manifest-backed terrain tile residency. Loaded VSCN
cells are measured from their authored scene resources, including base meshes,
resident LOD meshes, impostor meshes, materials, and resident material textures.
`Mesh3D.Resident` / `SceneNode3D.SetLodResident` changes are remeasured on
subsequent `update(dt)` calls so a cell can shed mesh detail without unloading
its whole scene subtree; unloaded cells continue to report the manifest `bytes`
estimate for planning and editor inspection. `update(dt)` advances a
deterministic per-frame load budget; when
the stream center jumps across multiple desired cell/tile payloads, stale
payloads unload immediately, one or more new payloads are admitted by the frame
budget, any payload whose measured post-load size exceeds the active residency
budget is evicted before telemetry is published, and
`pendingRequestCount` reports deferred desired payloads until later updates
drain them. A budget of `0` unloads cells and reports no resident cells or
tiles; a negative budget is unlimited. Worker-backed decode/upload remains a
future streaming layer.

Editor/debug inspection uses method-style lower/camel calls:
`getCellCount()`, `getCellName(i)`, `getCellCenter(i)`,
`getCellResident(i)`, `getCellBytes(i)`, `getTerrainTileCount()`,
`getTerrainTileName(i)`, `getTerrainTileHeightmap(i)`, `getTerrainTileCenter(i)`,
`getTerrainTileResident(i)`, and `getTerrainTileBytes(i)`. Invalid indexes
return the usual safe defaults (`0`, `false`, `""`, or `null`).

`examples/3d/openworld_slice/` is the current end-to-end streaming smoke
project. Its CTest moves the stream through all four far-apart quadrants over
budgeted stream ticks,
asserts one resident cell/tile at a time, verifies bounded resident bytes,
validates heightmapped terrain-payload swaps, rendered stream terrain, and
inspection hooks, checks
the async `AssetHandle3D` model path, loads RGBA8 and BC7 KTX2
`TextureAsset3D` fixtures, renders a BC7 texture panel in the main scene,
loads a synthetic skinned glTF agent through
`Assets3D.LoadModelAsset`, crossfades its auto-bound animator from `Idle` to
`Wave`, binds `IKSolver3D.LookAt` through `Animator3D.setIKSolver`, loads a
committed GLB through `Assets3D.LoadModelAsset`, loads a committed WAV through
`Sound3D.loadAsset`, validates a terrain-sampled `IKSolver3D.TwoBone` foot
target, renders a visible foot marker/leg pair for that proof, reads the
`World3D` runtime counters, runs physics/character/nav-agent simulation,
compares the software final frame to the committed
`assets/baselines/openworld_slice_software.png` baseline, and repeats the same
fixed-step sequence to prove deterministic replay.
`streaming_hitch_probe.zia` records blocking-vs-async template load timing,
proves a zero upload budget keeps positive-cost async commit work pending until
the budget is restored, checks `Assets3D.GetResidentBytes()` returns to zero
after cache churn, and in its native-compressed CTest lane proves backend
texture-upload budget telemetry on a capable GPU backend.

---

## World-Scoped NavMesh Baking

`World3D.bakeNavMesh(agentRadius, agentHeight, maxSlope, cellSize)` and
`World3D.bakeTiledNavMesh(tileSize, agentRadius, agentHeight, maxSlope,
cellSize)` are Game3D editor hooks over the lower-level
`Viper.Graphics3D.NavMesh3D.Bake*` APIs. They bake from the world's current
`Scene3D`, including hidden streamed-terrain nav source nodes, preserve
world-space transforms, and return a
`Viper.Graphics3D.NavMesh3D` for `NavAgent3D` use. The tiled entry uses
`NavMesh3D.BakeTiled`, which retains tile voxel source data so
`NavMesh3D.RebuildTile(tileX, tileZ)` can refresh one tile's geometry and
obstacle state without a whole-scene bake.

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
| `playLayerAdditive(layer, name)` | Play a named controller state as a true additive overlay layer |
| `crossfadeLayerAdditive(layer, name, seconds)` | Blend a named controller state as a true additive overlay layer |
| `setBlendTree(tree)` | Use a compatible `BlendTree3D` as the wrapped controller's base pose source |
| `setIKSolver(solver)` | Apply a compatible `IKSolver3D` after overlays and before skinning |
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
Layer entry events from `playLayerAdditive` are captured through the same event
buffer; `crossfadeLayerAdditive` uses the same event capture path. Layer masks
and weights remain controlled on the wrapped `AnimController3D` through the
`controller` escape hatch. `setBlendTree(tree)`
forwards to `AnimController3D.SetBlendTree`; it is useful when a movement
blendspace should supply the base pose while the controller still owns overlay
layers and event polling. `setIKSolver(solver)` forwards to
`AnimController3D.SetIKSolver` for foot/hand targets, look-at bones, and short
FABRIK chains driven from gameplay.

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
`drawEffects()` handle the normal update/draw path. When `World3D.floatingOrigin`
performs a rebase, registered particle emitters, live particles, and decals are
shifted by the same delta; decal mesh caches are rebuilt on the next draw so
their vertices stay near the rebased camera:

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
Setting `isStatic` true clears `isKinematic` and sets mass to zero. Setting it
back to false restores a positive default mass when the definition does not
already have one, so a static definition can be toggled back to a usable dynamic
body without an extra `set_mass` call.

`World3D.collisionEventCount(phase)` and `collisionEvent(phase, index)` expose
the runtime enter/stay/exit buffers through `Collision3DEvent`;
`World3D.clearCollisionEvents()` empties those buffers manually:

```zia
Game3D.World3D.stepSimulation(world, 0.016);
var count = Game3D.World3D.collisionEventCount(world, Game3D.CollisionPhase.get_Enter());
if (count > 0) {
    var evt = Game3D.World3D.collisionEvent(world, Game3D.CollisionPhase.get_Enter(), 0);
    var other = Game3D.Collision3DEvent.other(evt, ball);
    var point = Game3D.Collision3DEvent.point(evt);
    var firstNormal = Game3D.Collision3DEvent.contactNormal(evt, 0);
}
```

The wrapper exposes `phase`, `a`, `b`, `raw`, `isTrigger`, `relativeSpeed`,
`normalImpulse`, `contactCount`, `point()`, `normal()`, `contactPoint(i)`,
`contactNormal(i)`, `contactSeparation(i)`, and `other(entity)`.
`point()` and `normal()` are first-contact convenience methods. Raw physics
events can expose multiple contact points for AABB and face-contact OBB box pairs;
other shapes currently carry one representative point. The indexed wrapper methods mirror the raw
`Graphics3D.CollisionEvent3D` surface so broader manifolds can extend behavior
without another Game3D API rename.
If a wrapped raw event or entity reference is invalid, the wrapper fails closed:
`phase` defaults to `Any`, entity and raw handles return `Nothing`, scalar
metrics and contact counts return zero, contact points return the origin, and
contact normals use the documented `+Y` fallback.
`CollisionPhase.Any` iterates enter, stay, and exit records. Optional
world/entity collision callback sugar remains deferred until the VM callback
trampoline policy is implemented; polling the event buffers is the supported
interpreted-Zia path.

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
codes in game code. `Game3D.Keys` covers the WASD/arrow movement keys plus `Space`,
`Shift`, `Ctrl`, `Escape`, and `F11` (handy for a fullscreen toggle).

---

## Camera And Character Controllers

Install a built-in controller with `World3D.setCameraController(controller)`.
The current runtime accepts the built-in Game3D controller objects listed here;
it does not yet invoke interpreted Zia interface objects as native C callbacks.
A controller can be installed on only one world at a time. Installing the same
controller on a second world detaches it from the previous world's controller
slot before binding it to the new world. Built-in controllers validate their
stored world refs during rebinding, so stale or wrong-class private slots do not
detach unrelated worlds.

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
| A handle fails after teardown | Destroy the `World3D` last. Spawned entities, effects, and audio source bindings are owned by the world. Destroyed-handle diagnostics use `Game3D.<Type>.<method>: <reason>` so the failing API call is visible. |

---

## Tests

The Game3D runtime is covered by:

| Test | Coverage |
|------|----------|
| `test_rt_game3d` | C runtime contracts for constants, masks, input, world defaults, spawn/despawn, stale-entity no-op diagnostics, shared-body rejection, collision-event clearing, native callback loops, fixed-loop accumulator/spiral-guard behavior, overlay hooks, final capture, packaged glTF hierarchy loading through `Assets3D.LoadModelAsset`, synthetic controller input, orbit/follow late update, first-person character movement, material presets, prefabs, lighting, quality, environment, post-FX, debug helpers, streamed terrain metadata inspection and LOD seam stitching beyond the single-heightmap cap, Animator3D root motion/events, Sound3D helpers, and Effects3D presets/expiry |
| `g3d_test_game3d_world_probe` | Zia construction, default subsystems, layer masks, entity spawn/find/despawn, direct `Entity3D.FromNode` subtree wrapping, synthetic `tick`, clamped `stepSimulation`, resize/aspect, manual frame path, final capture, and destroy |
| `g3d_test_game3d_runframes_probe` | Zia deterministic `runFramesOnly`, dt/elapsed/frame accounting, and final capture |
| `g3d_test_game3d_runframes_callback_reject` | Interpreted Zia callback rejection diagnostic for native callback-loop APIs |
| `g3d_test_game3d_destroyed_handle_reject` | Interpreted Zia destroyed-`World3D` diagnostic for post-teardown handle use |
| `g3d_test_game3d_destroyed_entity_reject` | Interpreted Zia stale-`Entity3D` neutral getter/no-op diagnostics for post-teardown handle use |
| `g3d_test_game3d_double_despawn_reject` | Interpreted Zia double-despawn stale-handle no-op diagnostic |
| `g3d_test_game3d_camera_controllers_probe` | Zia free-fly synthetic input, orbit drag/zoom, and follow camera post-physics tracking |
| `g3d_test_game3d_character_controller_probe` | Zia first-person character movement and late-update camera alignment |
| `g3d_test_game3d_presets_probe` | Zia material/prefab presets, lighting, quality fallback, post-FX, environment chaining, physics body setup, and final-overlay debug capture |
| `g3d_test_game3d_assets_probe` | Zia `Assets3D` filesystem/asset loading, imported-group spawn/despawn churn without registry retention, cached templates, cache clear/reload, missing package-asset error handles, and model-template instantiation |
| `g3d_test_game3d_physics_probe` | Zia `BodyDef` static/dynamic body attachment, CCD/filter flags, gravity, and collision production |
| `g3d_test_game3d_collision_probe` | Zia entity-aware collision wrappers for enter/stay/exit and trigger events |
| `g3d_test_game3d_anim_probe` | Zia Animator3D play/crossfade/state-time/events, entity attachment, raw controller wrapping, and root-motion world stepping |
| `g3d_test_game3d_sound_probe` | Zia listener follow/manual pose, attenuation defaults, positional playback, attached-source sync, 2D playback, and source cleanup |
| `g3d_test_game3d_effects_probe` | Zia Effects3D presets, registry diagnostics, draw path, auto-expiry, manual particle/decal registration, and collision-triggered VFX |
| `g3d_test_game3d_docs_snippets` | Copy-paste docs surfaces for setup, presets, assets, physics, audio/VFX, deterministic frame helpers, and manual final-frame capture |
| `g3d_test_graphics3d_docs_snippets` | Copy-paste Graphics3D animation docs surface for retargeting, IK pole/ground-normal controls, animation LOD, and bone-count LOD |
| `g3d_walk_min_visual_probe` | Game3D sample final-frame baseline, crisp overlay, directional lighting, and grounded synthetic first-person movement |
| `g3d_bounded_no_regression_probe` | Existing `walk_min.zia` bounded sample run with scale flags off, proving exact final-frame pixel parity and exact player/body/draw/visibility/stream state parity against the default bounded path |
| `g3d_game3d_hello` | <=20-line hello-world scene with lighting, walkable ground, first-person character, and no `Mat4` |
| `g3d_game3d_common_no_mat4` | CMake guard that common Game3D samples/probes avoid direct `Mat4.` calls |
| `g3d_game3d_starter_probe` | Starter project deterministic movement, package-aware model asset, final capture, and grounded character path |
| `g3d_game3d_starter_package_dry_run` | Starter `viper.project` asset packaging layout |
| `g3d_openworld_slice_probe` | Open-world slice stream in/out, rendered heightmapped terrain payload access, visible KTX2/BC7 texture panel, async asset handle completion, skinned glTF play/crossfade plus LookAt IK binding, committed GLB/WAV asset fixture loading, visible terrain-sampled TwoBone foot IK proof, character/physics/nav stepping, software final-frame baseline comparison, and deterministic replay |
| `g3d_openworld_slice_perf_probe` | Open-world slice deterministic software frame-loop perf probe; emits setup, elapsed, average-frame, FPS, optional backend GPU frame time, draw, visibility, entity, body, and stream residency metrics |
| `g3d_openworld_slice_perf_harness` | Reusable CTest perf harness wrapper that runs `perf_probe.zia`, parses the `PERF:` metrics, validates required counters, and emits a stable `HARNESS:` summary |
| `g3d_openworld_slice_streaming_hitch_probe` | Phase 4 async asset probe that records blocking-vs-async load timing, verifies zero-upload-budget pending behavior, releases work under a positive budget, and checks resident bytes return to zero |
| `g3d_openworld_slice_streaming_hitch_native_compressed_probe` | GPU opt-in run of the same hitch probe that binds native compressed texture content, verifies zero texture-upload budget leaves backend bytes pending, then records budgeted native upload bytes, raw-vs-compressed RAM/VRAM reduction, and final-frame texture tolerance once released |
| `g3d_openworld_slice_long_traversal` | Open-world slice repeated all-quadrant stream churn with bounded residency, zero pending requests after settled visits, traversal hitch/memory/seam telemetry, terrain collider checks, render telemetry, and deterministic replay |
| `g3d_openworld_slice_visibility_dense_probe` | Authored dense city/forest visibility fixture that records PVS draw-call/fill-proxy reduction and compares optimized software pixels against the no-PVS baseline |
| `g3d_openworld_slice_gpu_smoke` | Capability-gated platform GPU backend smoke for the open-world slice, including a degenerate-basis normal-map robustness pass, 24-light clustered/forward+ draw, 3-cascade primary directional CSM fixture, and optional backend GPU frame-time telemetry; reports `SKIP` when the requested GPU backend is unavailable |
| `g3d_openworld_slice_package_dry_run` | Open-world slice `viper.project` asset packaging layout |
| `g3d_3dnext2_surface_probe` | Phase-0 3D next-level surface probe covering worker-backed ordered `Viper.Threads.Parallel` map output and `World3D.runFramesOnly` worker-count replay parity |
| `test_rt_g3d_commit_queue` | Internal Graphics3D main-thread commit queue FIFO/budgeted drain, worker-enqueue/main-thread-commit behavior, and worker-drain rejection |
| `scripts/g3d_tsan_concurrency_lane.sh` | Focused ThreadSanitizer lane for the worker pool, ordered map/reduce, runtime concurrency stress, asset-worker decode paths, Game3D worker parity, open-world streaming hitch probe, and the Graphics3D commit queue |
| NL3-031 determinism gate | `g3d_3dnext2_surface_probe`, `test_rt_game3d`, `test_codegen_env_is_native`, native-run Zia promise tests, and `test_crosslayer_arith` prove worker-count replay, ordered merge, and VM/native parity |
| NL3-033 software-baseline gate | `test_rt_canvas3d`, `test_rt_canvas3d_gpu_paths`, `test_rt_canvas3d_production`, `test_rt_scene3d`, `test_vgfx3d_backend_utils`, and open-world slice probes keep software visual correctness as the baseline before capability-gated GPU upload/lighting/shadow/visibility paths |
| NL3-034 policy audit gate | `lint_platform_policy.sh --strict --changed-only`, `run_cross_platform_smoke.sh`, the zero-new-dependency audit, and ADR 0004 cover platform policy, dependency policy, and registry-only IL runtime surface edits |
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
`examples/3d/game3d_showcase/` are the current code-first samples.
`examples/3d/openworld_slice/` is the streaming vertical-slice smoke project.
The bowling setup migration lives at `examples/games/3dbowling/game3d/`.

Use the lower-level `Viper.Graphics3D` and `Viper.Sound` APIs as escape hatches
when a sample needs behavior outside the Game3D convenience layer.
