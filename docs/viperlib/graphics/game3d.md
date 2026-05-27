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
| `Input3D` | Named keyboard, mouse, movement-axis, and look-axis helper over the runtime input state |
| `Audio3D` | World-owned 3D audio handle; Phase 1 exposes the active listener |
| `EffectRegistry3D` | World-owned effects handle; Phase 1 exposes the world post-FX chain |
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
| `audio` | `Viper.Game3D.Audio3D` with a camera-aligned listener |
| `effects` | `Viper.Game3D.EffectRegistry3D` with a `PostFX3D` chain |

The constructor installs explicit default lighting, balanced backend-safe
quality, frustum culling, a readable camera, a neutral clear/ambient setup, and
the default audio listener. These are normal runtime choices, not hidden Zia
setup code.

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
| `PostFX.Cinematic(world)` / `Crisp(world)` / `None(world)` | Backend-safe effect chains |
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
setup. Water is a transparent plane prefab.

---

## Debug3D

`Debug3D` uses the same final overlay path as user HUDs, so its text is drawn
after post-FX rather than being bloomed, toned, or blurred.

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
5. Step physics.
6. Sync physics-owned nodes and 3D audio bindings.
7. Run the installed Game3D camera/controller `lateUpdate`.
8. Begin the frame and draw the scene.
9. Draw the effects registry.
10. End the scene.
11. Draw the final overlay if a native overlay callback is being used.
12. Finalize/present, or leave the finalized frame available for capture.

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
| `tick()` | Poll live input and advance world timing; returns false when the window should close |
| `stepSimulation(step)` | Step the physics world and sync scene bindings |
| `beginFrame()` | Clear and begin drawing with the world camera |
| `drawScene()` | Draw the world scene |
| `drawEffects()` | Draw the effect registry/post-FX contribution |
| `endScene()` | End the draw pass without presenting |
| `captureFinalFrame()` | Finalize post-FX/overlay and return `Pixels` |
| `present()` | Finalize if needed and present the frame |

For deterministic interpreted-Zia tests, use `runFramesOnly(frameCount, stepSec)`
or call the manual methods explicitly. `runFramesOnly` uses the synthetic clock
path and leaves the final frame capturable.

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
`SceneNode3D`. Transform helpers update that node directly:

| Method | Purpose |
|--------|---------|
| `setPosition(x, y, z)` / `setPositionV(vec3)` | Set node position |
| `setScale(s)` / `setScaleXYZ(x, y, z)` | Set node scale |
| `setRotationEuler(xDeg, yDeg, zDeg)` | Set node orientation in degrees |
| `setMesh(mesh)` / `setMaterial(material)` | Replace render resources |
| `addChild(child)` | Parent another Game3D entity |
| `setName(name)` | Name the entity and backing node for lookup |
| `setLayer(layer)` | Set gameplay/physics layer |
| `setCollisionMask(mask)` | Set the layer mask used by attached bodies |
| `position()` / `worldPosition()` | Read local/world position |
| `isSpawned()` / `isDestroyed()` | Inspect lifecycle state |

`World3D.spawn(entity)` attaches the entity node to the world scene and registers
the entity by name. `World3D.despawn(entity)` removes it from the registry,
scene, and physics world. Child Game3D entities are owned by their parent for
despawn purposes; raw imported child nodes remain part of the imported node
subtree.

Use `LayerMask.None()`, `LayerMask.All()`, `LayerMask.Of(layer)`,
`include(layer)`, and `includes(layer)` for readable filters. Layer values are
validated as single-bit masks.

---

## Input3D

`Input3D` reads the same keyboard and mouse state updated by `Canvas3D.Poll()` or
the synthetic input path.

| Method / property | Purpose |
|-------------------|---------|
| `lookSensitivity` | Multiplier for `lookAxis()` |
| `update()` | Synchronize input helper state |
| `isDown(key)` / `pressed(key)` / `released(key)` | Keyboard queries |
| `mouseDelta()` | Current mouse movement as `Vec2` |
| `mouseButton(button)` / `mousePressed(button)` | Mouse button queries |
| `wheelY()` | Vertical wheel delta |
| `moveAxis()` | WASD/arrow movement as `Vec3` |
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
editor-like movement. It reads `Input3D.moveAxis()` and raw mouse deltas, moves
through the camera basis, and can capture or release mouse input with
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

`OrbitController.New(world, target)` takes a `Vec3` target. Holding the left
mouse button and dragging changes yaw/pitch, wheel input changes distance, and
`lateUpdate` places the camera on the orbit.

`FollowController.New(world, entity, offset)` tracks an entity after physics.
Set `damping` to `0.0` for a snap follow, or a positive value for exponential
smoothing.

---

## Tests

The Game3D runtime is covered by:

| Test | Coverage |
|------|----------|
| `test_rt_game3d` | C runtime contracts for constants, masks, input, world defaults, spawn/despawn, collision-event clearing, native callback loops, overlay hooks, final capture, synthetic controller input, orbit/follow late update, first-person character movement, material presets, prefabs, lighting, quality, environment, post-FX, and debug helpers |
| `g3d_test_game3d_world_probe` | Zia construction, default subsystems, layer masks, entity spawn/find/despawn, resize/aspect, manual frame path, final capture, and destroy |
| `g3d_test_game3d_runframes_probe` | Zia deterministic `runFramesOnly`, dt/elapsed/frame accounting, and final capture |
| `g3d_test_game3d_runframes_callback_reject` | Interpreted Zia callback rejection diagnostic for native callback-loop APIs |
| `g3d_test_game3d_camera_controllers_probe` | Zia free-fly synthetic input, orbit drag/zoom, and follow camera post-physics tracking |
| `g3d_test_game3d_character_controller_probe` | Zia first-person character movement and late-update camera alignment |
| `g3d_test_game3d_presets_probe` | Zia material/prefab presets, lighting, quality fallback, post-FX, environment chaining, physics body setup, and final-overlay debug capture |
| `g3d_walk_min_visual_probe` | Game3D sample final-frame baseline, crisp overlay, directional lighting, and grounded synthetic first-person movement |

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

The core world/entity/input layer and built-in camera/character controllers now
live in the C runtime. `examples/3d/walk_min.zia` is the current small Game3D
walking sample. `BodyDef` construction, rich Game3D collision event wrappers,
prefabs, environment presets, `Assets3D`, animation helpers, 3D audio playback
helpers, VFX presets, starter templates, and showcase samples remain tracked in
the 3D Next Level plan.

Use the lower-level `Viper.Graphics3D` and `Viper.Sound` APIs as escape hatches
while those higher-level Game3D helpers are being implemented.
