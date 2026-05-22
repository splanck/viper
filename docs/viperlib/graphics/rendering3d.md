---
status: active
audience: public
last-verified: 2026-05-21
---

# 3D Rendering, Animation, and Environment
> Camera, lighting, animation, audio, particles, post-processing, terrain, navigation, and environment helpers for the Viper.Graphics3D namespace.

**Part of [Viper Runtime Library](../README.md) › [Graphics](README.md)**

---

This page documents the `Viper.Graphics3D` runtime surface for classes not covered by [3D Physics](physics3d.md). For mesh loading, material authoring, scene graphs, and the full 3D asset pipeline, see the [Graphics 3D Guide](../../graphics3d-guide.md).

---

## Camera And Rendering

### Viper.Graphics3D.Camera3D

3D perspective or orthographic camera with smooth follow, orbit, and screen-to-ray projection.

**Type:** Instance (obj)
**Constructor:** `Camera3D.New(fov, near, far)`

#### Properties

| Property   | Type   | Access     | Description |
|------------|--------|------------|-------------|
| `Fov`      | Double | Read/Write | Vertical field of view in degrees |
| `Position` | Object | Read/Write | Camera position as `Vec3` |
| `Forward`  | Object | Read       | Unit forward vector as `Vec3` |
| `Right`    | Object | Read       | Unit right vector as `Vec3` |
| `IsOrtho`  | Boolean | Read      | True when camera is in orthographic projection mode |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `LookAt(pos, target, up)` | `Void(Object, Object, Object)` | Orient camera from a position toward a target using an up vector |
| `Orbit(pivot, yaw, pitch, distance)` | `Void(Object, Double, Double, Double)` | Position and orient camera on a sphere around `pivot` |
| `ScreenToRay(sx, sy, screenW, screenH)` | `Object(Integer, Integer, Integer, Integer)` | Return a normalized `Vec3` direction from a screen pixel |
| `Shake(amplitude, frequency, duration)` | `Void(Double, Double, Double)` | Start a procedural camera shake |
| `SmoothFollow(target, speed, minDist, maxDist, height)` | `Void(Object, Double, Double, Double, Double)` | Lerp toward a target with distance clamping |
| `SmoothLookAt(target, speed, roll)` | `Void(Object, Double, Double)` | Slerp the camera orientation toward a target |

Camera positions and FPS-style movement inputs are clamped to the runtime's safe world range before
view/projection matrices are generated. Non-finite position components fall back to `0.0`.

```rust
bind Viper.Graphics3D.Camera3D as Camera3D;
bind Viper.Math.Vec3 as Vec3;

var cam = Camera3D.New(60.0, 0.1, 1000.0)
cam.LookAt(Vec3.New(0.0, 2.0, -5.0), Vec3.New(0.0, 0.0, 0.0), Vec3.New(0.0, 1.0, 0.0))
cam.Shake(0.3, 15.0, 0.5)
```

```basic
DIM cam AS Viper.Graphics3D.Camera3D
cam = NEW Viper.Graphics3D.Camera3D(60.0, 0.1, 1000.0)
DIM eye AS Viper.Math.Vec3 = NEW Viper.Math.Vec3(0.0, 2.0, -5.0)
DIM tgt AS Viper.Math.Vec3 = NEW Viper.Math.Vec3(0.0, 0.0, 0.0)
DIM up  AS Viper.Math.Vec3 = NEW Viper.Math.Vec3(0.0, 1.0, 0.0)
cam.LookAt(eye, tgt, up)
```

---

### Viper.Graphics3D.RenderTarget3D

Offscreen color buffer for 3D scenes, optionally in HDR format.

**Type:** Instance (obj)
**Constructor:** `RenderTarget3D.New(width, height)`

#### Properties

| Property | Type    | Access | Description |
|----------|---------|--------|-------------|
| `IsHdr`  | Boolean | Read   | True for floating-point HDR buffers |
| `Width`  | Integer | Read   | Buffer width in pixels |
| `Height` | Integer | Read   | Buffer height in pixels |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `NewHdr(width, height)` | `Object(Integer, Integer)` | Create an HDR floating-point render target |
| `AsPixels()` | `Object()` | Download the color buffer into a `Pixels` object |

GPU-backed render targets synchronize their CPU `Pixels` mirror lazily when `AsPixels()` or a
screenshot requests it. Resetting a render target or destroying its owning `Canvas3D` detaches the
backend sync callback, so later CPU readback cannot call into a stale GPU context.

---

### Viper.Graphics3D.CubeMap3D

Cubemap texture resource for environment mapping and skyboxes. Use `Canvas3D.LoadCubeMap` to load six-face cubemaps from disk.

**Type:** Instance (obj)
**Constructor:** `CubeMap3D.New(size)`

---

### Viper.Graphics3D.Light3D

Scene light with configurable color and intensity. Directional, point, and spot variants are created via the `Canvas3D` scene light API.

**Type:** Instance (obj)
**Constructor:** `Canvas3D.AddDirectionalLight(...)` (returned as `Light3D`)

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetIntensity(value)` | `Void(Double)` | Set light intensity multiplier |
| `SetColor(r, g, b)` | `Void(Double, Double, Double)` | Set light color using normalized RGB components |

---

### Viper.Graphics3D.PostFX3D

Post-processing effect chain applied to a rendered scene.

**Type:** Instance (obj)
**Constructor:** `PostFX3D.New()`

#### Properties

| Property      | Type    | Access     | Description |
|---------------|---------|------------|-------------|
| `Enabled`     | Boolean | Read/Write | Enable or disable the entire chain |
| `EffectCount` | Integer | Read       | Number of effects currently in the chain |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddBloom(threshold, intensity, passes)` | `Void(Double, Double, Integer)` | Add a bloom bloom effect |
| `AddTonemap(mode, exposure)` | `Void(Integer, Double)` | Add tone mapping (`0 = Reinhard`, `1 = ACES`) |
| `AddFXAA()` | `Void()` | Add FXAA anti-aliasing |
| `AddColorGrade(brightness, contrast, saturation)` | `Void(Double, Double, Double)` | Add color grading |
| `AddVignette(strength, radius)` | `Void(Double, Double)` | Add a vignette darkening effect |
| `AddSSAO(radius, intensity, samples)` | `Void(Double, Double, Integer)` | Add screen-space ambient occlusion |
| `AddDOF(focusDist, focalRange, blurRadius)` | `Void(Double, Double, Double)` | Add depth of field |
| `AddMotionBlur(strength, samples)` | `Void(Double, Integer)` | Add motion blur |
| `Clear()` | `Void()` | Remove all effects from the chain |

```rust
bind Viper.Graphics3D.PostFX3D as PostFX3D;

var fx = PostFX3D.New()
fx.AddBloom(0.8, 1.2, 4)
fx.AddTonemap(1, 1.0)
fx.AddFXAA()
fx.Enabled = true
```

---

### Viper.Graphics3D.RayHit3D

Mesh raycast result returned by `Canvas3D.Raycast`. Read-only value type.

**Type:** Static (none)

#### Properties

| Property        | Type    | Access | Description |
|-----------------|---------|--------|-------------|
| `Distance`      | Double  | Read   | Distance along the ray to the hit point |
| `Point`         | Object  | Read   | World-space hit point as `Vec3` |
| `Normal`        | Object  | Read   | Surface normal at the hit point as `Vec3` |
| `TriangleIndex` | Integer | Read   | Index of the hit triangle in the mesh |

---

## Skeletal Animation

### Viper.Graphics3D.Skeleton3D

Bone hierarchy for skeletal mesh deformation. Typically loaded alongside a model via `Model3D`.

**Type:** Instance (obj)
**Constructor:** `Skeleton3D.New()`

#### Properties

| Property    | Type    | Access | Description |
|-------------|---------|--------|-------------|
| `BoneCount` | Integer | Read   | Number of bones in the skeleton |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddBone(name, parentIndex, localTransform)` | `Integer(String, Integer, Object)` | Add a named bone with a local transform and parent index |
| `ComputeInverseBind()` | `Void()` | Pre-compute inverse bind pose matrices after all bones are added |
| `FindBone(name)` | `Integer(String)` | Return the bone index, or `-1` if not found |
| `GetBoneName(index)` | `String(Integer)` | Return the name of bone at `index` |

---

### Viper.Graphics3D.Animation3D

Single keyframe animation track referencing a `Skeleton3D`.

**Type:** Instance (obj)
**Constructor:** `Animation3D.New(name, skeleton)`

#### Properties

| Property  | Type    | Access     | Description |
|-----------|---------|------------|-------------|
| `Name`    | String  | Read       | Animation name |
| `Duration`| Double  | Read       | Total duration in seconds |
| `Looping` | Boolean | Read/Write | Whether the animation loops |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddKeyframe(boneIndex, time, translation, rotation, scale)` | `Void(Integer, Double, Object, Object, Object)` | Add a keyframe for the given bone at `time` seconds |

---

### Viper.Graphics3D.AnimPlayer3D

Plays a single `Animation3D` track on a `Skeleton3D` with optional crossfade.

**Type:** Instance (obj)
**Constructor:** `AnimPlayer3D.New(skeleton)`

#### Properties

| Property    | Type    | Access     | Description |
|-------------|---------|------------|-------------|
| `Speed`     | Double  | Read/Write | Playback speed multiplier |
| `IsPlaying` | Boolean | Read       | True when an animation is playing |
| `Time`      | Double  | Read/Write | Current playback time in seconds |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Play(animation)` | `Void(Object)` | Start playing an `Animation3D` |
| `Crossfade(animation, duration)` | `Void(Object, Double)` | Blend into a new animation over `duration` seconds |
| `Stop()` | `Void()` | Stop the current animation |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the animation by the given delta |
| `GetBoneMatrix(boneIndex)` | `Object(Integer)` | Return the current skinning matrix for `boneIndex` |

```rust
bind Viper.Graphics3D.AnimPlayer3D as AnimPlayer3D;
bind Viper.Graphics3D.Animation3D as Animation3D;

var player = AnimPlayer3D.New(skeleton)
player.Play(walkAnim)
player.Speed = 1.5

// per frame
player.Update(deltaSeconds)
```

```basic
DIM player AS Viper.Graphics3D.AnimPlayer3D = NEW Viper.Graphics3D.AnimPlayer3D(skeleton)
player.Play(walkAnim)
player.Update(deltaSeconds)
```

---

### Viper.Graphics3D.AnimBlend3D

Blends multiple named animation states by weight. Useful for blend trees (run/walk, aim offsets).

**Type:** Instance (obj)
**Constructor:** `AnimBlend3D.New(skeleton)`

#### Properties

| Property     | Type    | Access | Description |
|--------------|---------|--------|-------------|
| `StateCount` | Integer | Read   | Number of animation states in the blend |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddState(name, animation)` | `Integer(String, Object)` | Add a named `Animation3D` state; returns its index |
| `SetWeight(index, weight)` | `Void(Integer, Double)` | Set normalized blend weight for state at `index` |
| `SetWeightByName(name, weight)` | `Void(String, Double)` | Set blend weight by state name |
| `GetWeight(index)` | `Double(Integer)` | Get the current weight for state at `index` |
| `SetSpeed(index, speed)` | `Void(Integer, Double)` | Set playback speed for state at `index` |
| `Update(deltaSeconds)` | `Void(Double)` | Advance all states and produce the blended pose |

---

### Viper.Graphics3D.AnimController3D

Stateful animation controller with named states, triggered transitions, animation events, root motion, and multi-layer blending.

**Type:** Instance (obj)
**Constructor:** `AnimController3D.New(skeleton)`

#### Properties

| Property           | Type    | Access | Description |
|--------------------|---------|--------|-------------|
| `CurrentState`     | String  | Read   | Name of the currently active state |
| `PreviousState`    | String  | Read   | Name of the state before the last transition |
| `IsTransitioning`  | Boolean | Read   | True while a crossfade is in progress |
| `StateCount`       | Integer | Read   | Total number of registered states |
| `RootMotionDelta`  | Object  | Read   | Accumulated root motion `Vec3` since last `ConsumeRootMotion` |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddState(name, animation)` | `Integer(String, Object)` | Register a named animation state |
| `AddTransition(from, to, duration)` | `Boolean(String, String, Double)` | Add a crossfade transition between two states |
| `Play(state)` | `Boolean(String)` | Switch immediately to the named state |
| `Crossfade(state, duration)` | `Boolean(String, Double)` | Blend into the named state over `duration` seconds |
| `Stop()` | `Void()` | Stop all animation and enter idle |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the controller and produce the current pose |
| `SetStateSpeed(state, speed)` | `Void(String, Double)` | Override playback speed for a state |
| `SetStateLooping(state, loop)` | `Void(String, Boolean)` | Override loop setting for a state |
| `AddEvent(state, time, name)` | `Void(String, Double, String)` | Register a named event to fire at a playback time |
| `PollEvent()` | `String()` | Dequeue the next fired event name, or empty string |
| `SetRootMotionBone(index)` | `Void(Integer)` | Designate a bone to extract root motion from |
| `ConsumeRootMotion()` | `Object()` | Read and clear the accumulated root motion `Vec3` |
| `SetLayerWeight(layer, weight)` | `Void(Integer, Double)` | Set the blend weight for an additive layer |
| `SetLayerMask(layer, boneMask)` | `Void(Integer, Integer)` | Restrict a layer to a bone mask bitmask |
| `PlayLayer(layer, state)` | `Boolean(Integer, String)` | Play a state on an additive layer |
| `CrossfadeLayer(layer, state, duration)` | `Boolean(Integer, String, Double)` | Blend into a new state on an additive layer over `duration` seconds |
| `StopLayer(layer)` | `Void(Integer)` | Stop the additive layer |
| `GetBoneMatrix(boneIndex)` | `Object(Integer)` | Return the current skinning matrix for `boneIndex` |

```rust
bind Viper.Graphics3D.AnimController3D as AnimController3D;

var ctrl = AnimController3D.New(skeleton)
ctrl.AddState("idle",  idleAnim)
ctrl.AddState("run",   runAnim)
ctrl.AddTransition("idle", "run",  0.2)
ctrl.AddTransition("run",  "idle", 0.3)
ctrl.AddEvent("run", 0.3, "footstepLeft")
ctrl.Play("idle")
ctrl.SetRootMotionBone(0)

// per frame
ctrl.Update(deltaSeconds)
var delta = ctrl.ConsumeRootMotion()
var evt = ctrl.PollEvent()
if evt == "footstepLeft" then
    Audio.Play(footstepSound)
end if
```

---

### Viper.Graphics3D.MorphTarget3D

Per-vertex morph target system for facial animation or shape blending.

**Type:** Instance (obj)
**Constructor:** `MorphTarget3D.New(mesh)`

#### Properties

| Property     | Type    | Access | Description |
|--------------|---------|--------|-------------|
| `ShapeCount` | Integer | Read   | Number of registered morph shapes |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddShape(name)` | `Integer(String)` | Add a named morph shape; returns its index |
| `SetDelta(shape, vertex, dx, dy, dz)` | `Void(Integer, Integer, Double, Double, Double)` | Set the position delta for a vertex in a shape |
| `SetNormalDelta(shape, vertex, dx, dy, dz)` | `Void(Integer, Integer, Double, Double, Double)` | Set the normal delta for a vertex in a shape |
| `SetWeight(index, weight)` | `Void(Integer, Double)` | Set blend weight `[0.0–1.0]` for shape at `index` |
| `GetWeight(index)` | `Double(Integer)` | Get the current weight for a shape |
| `SetWeightByName(name, weight)` | `Void(String, Double)` | Set blend weight by shape name |

---

## Particles And Effects

### Viper.Graphics3D.Particles3D

3D particle emitter with configurable spawn, physics, color, and render properties.

**Type:** Instance (obj)
**Constructor:** `Particles3D.New(maxParticles)`

#### Properties

| Property   | Type    | Access | Description |
|------------|---------|--------|-------------|
| `Count`    | Integer | Read   | Currently active particle count |
| `Emitting` | Boolean | Read   | True while the emitter is running |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Set emitter world position |
| `SetDirection(x, y, z, spread)` | `Void(Double, Double, Double, Double)` | Set emission direction and cone spread in degrees |
| `SetSpeed(min, max)` | `Void(Double, Double)` | Set spawn speed range |
| `SetLifetime(min, max)` | `Void(Double, Double)` | Set particle lifetime range in seconds |
| `SetSize(min, max)` | `Void(Double, Double)` | Set particle size range |
| `SetGravity(x, y, z)` | `Void(Double, Double, Double)` | Apply per-particle gravity vector |
| `SetColor(startColor, endColor)` | `Void(Integer, Integer)` | Set per-particle color gradient (`0xRRGGBBAA`) |
| `SetAlpha(start, end)` | `Void(Double, Double)` | Set per-particle alpha fade |
| `SetRate(particlesPerSecond)` | `Void(Double)` | Set continuous emission rate |
| `SetTexture(pixels)` | `Void(Object)` | Set the particle sprite texture |
| `SetEmitterShape(shape)` | `Void(Integer)` | Set emitter shape (`0 = point`, `1 = sphere`, `2 = box`) |
| `SetEmitterSize(x, y, z)` | `Void(Double, Double, Double)` | Set emitter volume extent for sphere/box shapes |
| `Start()` | `Void()` | Begin continuous emission |
| `Stop()` | `Void()` | Stop continuous emission |
| `Burst(count)` | `Void(Integer)` | Emit `count` particles immediately |
| `Clear()` | `Void()` | Remove all active particles |
| `Update(deltaSeconds)` | `Void(Double)` | Advance particle simulation |
| `Draw(canvas3D, camera)` | `Void(Object, Object)` | Render particles to the scene |

```rust
bind Viper.Graphics3D.Particles3D as Particles3D;

var emitter = Particles3D.New(256)
emitter.SetPosition(0.0, 1.0, 0.0)
emitter.SetDirection(0.0, 1.0, 0.0, 30.0)
emitter.SetSpeed(2.0, 6.0)
emitter.SetLifetime(0.5, 1.5)
emitter.SetColor(0xFF6622FF, 0xFF220000)
emitter.SetRate(40.0)
emitter.Start()

// per frame
emitter.Update(deltaSeconds)
emitter.Draw(canvas3d, camera)
```

---

### Viper.Graphics3D.Decal3D

Time-limited projected decal placed in a 3D scene (bullet holes, blood splats, scorch marks).

**Type:** Instance (obj)
**Constructor:** `Decal3D.New(pixels, position, normal)`

#### Properties

| Property  | Type    | Access | Description |
|-----------|---------|--------|-------------|
| `Expired` | Boolean | Read   | True when the decal lifetime has elapsed |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetLifetime(seconds)` | `Void(Double)` | Override the decal lifetime |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the lifetime timer |

---

## Spatial Audio

### Viper.Graphics3D.Audio3D

Static utilities for positioning audio in 3D space via listeners and voice IDs.

**Type:** Static (none)

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetListener(position, forward)` | `Void(Object, Object)` | Set the listener position and orientation |
| `PlayAt(sound, position, volume, loop)` | `Integer(Object, Object, Double, Integer)` | Spawn a spatialized voice at a world position; returns voice ID |
| `UpdateVoice(voiceId, position, volume)` | `Void(Integer, Object, Double)` | Update the position and volume of a live spatialized voice |
| `SyncBindings(deltaSeconds)` | `Void(Double)` | Propagate node/camera bindings from `AudioSource3D` and `AudioListener3D` |

---

### Viper.Graphics3D.AudioListener3D

3D audio listener that tracks a scene node or camera position.

**Type:** Instance (obj)
**Constructor:** `AudioListener3D.New()`

#### Properties

| Property   | Type    | Access     | Description |
|------------|---------|------------|-------------|
| `Position` | Object  | Read/Write | Listener world position as `Vec3` |
| `Forward`  | Object  | Read/Write | Listener facing direction as `Vec3` |
| `Velocity` | Object  | Read/Write | Listener velocity for Doppler as `Vec3` |
| `IsActive` | Boolean | Read/Write | Activate this listener for spatialization |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(pos)` | `Void(Object)` | Set position from a `Vec3` |
| `SetForward(dir)` | `Void(Object)` | Set facing direction from a `Vec3` |
| `SetVelocity(vel)` | `Void(Object)` | Set Doppler velocity from a `Vec3` |
| `BindNode(sceneNode)` | `Void(Object)` | Automatically track a `SceneNode3D` position each `Audio3D.SyncBindings` call |
| `ClearNodeBinding()` | `Void()` | Remove the node binding |
| `BindCamera(camera)` | `Void(Object)` | Automatically track a `Camera3D` position and forward |
| `ClearCameraBinding()` | `Void()` | Remove the camera binding |

---

### Viper.Graphics3D.AudioSource3D

3D audio source positioned in world space, with range and Doppler support.

**Type:** Instance (obj)
**Constructor:** `AudioSource3D.New(sound)`

#### Properties

| Property      | Type    | Access     | Description |
|---------------|---------|------------|-------------|
| `Position`    | Object  | Read/Write | Source world position as `Vec3` |
| `Velocity`    | Object  | Read/Write | Source velocity for Doppler as `Vec3` |
| `MaxDistance` | Double  | Read/Write | Attenuation roll-off distance |
| `Volume`      | Integer | Read/Write | Base volume `0–100` |
| `Looping`     | Boolean | Read/Write | True to loop the audio |
| `IsPlaying`   | Boolean | Read       | True when the source is playing |
| `VoiceId`     | Integer | Read       | Runtime voice ID of the active playback |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(pos)` | `Void(Object)` | Set position from a `Vec3` |
| `SetVelocity(vel)` | `Void(Object)` | Set Doppler velocity from a `Vec3` |
| `Play()` | `Integer()` | Start playback; returns voice ID |
| `Stop()` | `Void()` | Stop playback |
| `BindNode(sceneNode)` | `Void(Object)` | Auto-track a `SceneNode3D` each `Audio3D.SyncBindings` call |
| `ClearNodeBinding()` | `Void()` | Remove node binding |

```rust
bind Viper.Graphics3D.AudioSource3D as AudioSource3D;
bind Viper.Graphics3D.AudioListener3D as AudioListener3D;
bind Viper.Graphics3D.Audio3D as Audio3D;
bind Viper.Sound.Sound as Sound;

var listener = AudioListener3D.New()
listener.IsActive = true
listener.BindCamera(cam)

var explosion = Sound.Load("assets/explosion.ogg")
var src = AudioSource3D.New(explosion)
src.MaxDistance = 40.0
src.SetPosition(Vec3.New(10.0, 0.0, 0.0))
src.Play()

// per frame
Audio3D.SyncBindings(deltaSeconds)
```

---

## Navigation

### Viper.Graphics3D.NavMesh3D

Walkable navigation mesh built from scene geometry. Used with `NavAgent3D` for pathfinding.

**Type:** Static (none)
**Constructor:** `NavMesh3D.Build(mesh, agentRadius, agentHeight)`

#### Properties

| Property        | Type    | Access | Description |
|-----------------|---------|--------|-------------|
| `TriangleCount` | Integer | Read   | Number of walkable triangles in the mesh |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `FindPath(start, end)` | `Object(Object, Object)` | Return a `Seq[Vec3]` of waypoints from `start` to `end`, or `Nothing` |
| `SamplePosition(pos)` | `Object(Object)` | Snap `pos` to the nearest walkable position |
| `IsWalkable(pos)` | `Boolean(Object)` | True when `pos` is on the walkable surface |
| `SetMaxSlope(degrees)` | `Void(Double)` | Override the maximum walkable slope angle |
| `DebugDraw(canvas3D)` | `Void(Object)` | Draw the navmesh wireframe for debugging |

---

### Viper.Graphics3D.NavAgent3D

Pathfinding agent that moves along a `NavMesh3D` toward a target.

**Type:** Instance (obj)
**Constructor:** `NavAgent3D.New(navMesh)`

#### Properties

| Property            | Type    | Access     | Description |
|---------------------|---------|------------|-------------|
| `Position`          | Object  | Read       | Current world position as `Vec3` |
| `Velocity`          | Object  | Read       | Current velocity as `Vec3` |
| `DesiredVelocity`   | Object  | Read       | Steering velocity before clamping |
| `HasPath`           | Boolean | Read       | True when a valid path is set |
| `RemainingDistance` | Double  | Read       | Distance remaining to the goal |
| `StoppingDistance`  | Double  | Read/Write | Distance from goal at which to stop |
| `DesiredSpeed`      | Double  | Read/Write | Maximum movement speed |
| `AutoRepath`        | Boolean | Read/Write | Automatically repath when blocked |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetTarget(pos)` | `Void(Object)` | Set a new goal position |
| `ClearTarget()` | `Void()` | Cancel current path |
| `Update(deltaSeconds)` | `Void(Double)` | Advance agent along the path |
| `Warp(pos)` | `Void(Object)` | Teleport the agent to a position |
| `BindCharacter(character3D)` | `Void(Object)` | Drive a `Character3D` from agent velocity |
| `BindNode(sceneNode)` | `Void(Object)` | Drive a `SceneNode3D` position from agent |

```rust
bind Viper.Graphics3D.NavMesh3D as NavMesh3D;
bind Viper.Graphics3D.NavAgent3D as NavAgent3D;

var nav  = NavMesh3D.Build(groundMesh, 0.5, 1.8)
var agent = NavAgent3D.New(nav)
agent.DesiredSpeed = 4.0
agent.StoppingDistance = 0.3
agent.SetTarget(Vec3.New(12.0, 0.0, 8.0))

// per frame
agent.Update(deltaSeconds)
```

---

## Environment

### Viper.Graphics3D.Terrain3D

Heightmap terrain with multi-layer splat texturing, LOD, and normal generation.

**Type:** Instance (obj)
**Constructor:** `Terrain3D.New(heightmapPixels, scaleX, scaleY, scaleZ)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetHeightmap(pixels)` | `Void(Object)` | Replace the heightmap with new `Pixels` |
| `SetMaterial(material)` | `Void(Object)` | Assign a base `Material3D` |
| `SetScale(x, y, z)` | `Void(Double, Double, Double)` | Set horizontal and vertical scale |
| `GeneratePerlin(pixels, frequency, octaves, persistence)` | `Void(Object, Double, Integer, Double)` | Generate a Perlin heightmap into `pixels` |
| `SetSplatMap(pixels)` | `Void(Object)` | Set a splat map for multi-layer texture blending |
| `SetLayerTexture(layer, pixels)` | `Void(Integer, Object)` | Set the texture for splat layer `[0–3]` |
| `SetLayerScale(layer, scale)` | `Void(Integer, Double)` | Set UV tiling scale for a splat layer |
| `GetHeightAt(worldX, worldZ)` | `Double(Double, Double)` | Sample interpolated terrain height |
| `GetNormalAt(worldX, worldZ)` | `Object(Double, Double)` | Sample terrain surface normal as `Vec3` |
| `SetLODDistances(near, far)` | `Void(Double, Double)` | Set LOD transition distances |
| `SetSkirtDepth(depth)` | `Void(Double)` | Set the tile skirt depth to hide LOD seams |

```rust
bind Viper.Graphics3D.Terrain3D as Terrain3D;

var heightmap = Pixels.New(256, 256)
var terrain = Terrain3D.New(heightmap, 1.0, 0.25, 1.0)
terrain.GeneratePerlin(heightmap, 0.05, 6, 0.5)
terrain.SetHeightmap(heightmap)
terrain.SetSplatMap(splatPixels)
terrain.SetLayerTexture(0, grassTex)
terrain.SetLayerTexture(1, rockTex)
```

---

### Viper.Graphics3D.Water3D

Animated water plane with wave simulation, reflections, and normal mapping.

**Type:** Instance (obj)
**Constructor:** `Water3D.New(scene)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetHeight(y)` | `Void(Double)` | Set the water plane world Y position |
| `SetWaveParams(amplitude, frequency, speed)` | `Void(Double, Double, Double)` | Configure the base wave simulation |
| `SetColor(r, g, b, a)` | `Void(Double, Double, Double, Double)` | Set base water color |
| `SetTexture(pixels)` | `Void(Object)` | Set the surface texture |
| `SetNormalMap(pixels)` | `Void(Object)` | Set the normal map for surface detail |
| `SetEnvMap(cubeMap)` | `Void(Object)` | Set the reflection `CubeMap3D` |
| `SetReflectivity(amount)` | `Void(Double)` | Set reflection strength `[0.0–1.0]` |
| `SetResolution(pixels)` | `Void(Integer)` | Set reflection render resolution |
| `AddWave(originX, originZ, amplitude, frequency, speed)` | `Void(Double, Double, Double, Double, Double)` | Add a Gerstner wave component |
| `ClearWaves()` | `Void()` | Remove all wave components |

---

### Viper.Graphics3D.Vegetation3D

GPU-instanced foliage (grass, bushes) with density map, wind animation, and LOD.

**Type:** Instance (obj)
**Constructor:** `Vegetation3D.New(mesh)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetDensityMap(pixels)` | `Void(Object)` | Control placement density from a grayscale map |
| `SetWindParams(strength, frequency, turbulence)` | `Void(Double, Double, Double)` | Set foliage wind sway |
| `SetLODDistances(near, far)` | `Void(Double, Double)` | Set LOD fade distances |
| `SetBladeSize(width, height, variance)` | `Void(Double, Double, Double)` | Set blade/frond dimensions |
| `Populate(terrain, count)` | `Void(Object, Integer)` | Scatter `count` instances over a `Terrain3D` using the density map |
| `Update(deltaSeconds, camX, camY, camZ)` | `Void(Double, Double, Double, Double)` | Advance wind simulation relative to camera position |

---

## Misc 3D Helpers

### Viper.Graphics3D.Trigger3D

AABB trigger volume that tracks entry and exit events for `Physics3DBody` objects.

**Type:** Instance (obj)
**Constructor:** `Trigger3D.New(minX, minY, minZ, maxX, maxY, maxZ)`

#### Properties

| Property     | Type    | Access | Description |
|--------------|---------|--------|-------------|
| `EnterCount` | Integer | Read   | Number of bodies that entered this frame |
| `ExitCount`  | Integer | Read   | Number of bodies that exited this frame |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Contains(body)` | `Boolean(Object)` | True when a `Physics3DBody` is inside the volume |
| `Update(world)` | `Void(Object)` | Recompute enter/exit events against a `Physics3DWorld` |
| `SetBounds(minX, minY, minZ, maxX, maxY, maxZ)` | `Void(Double, Double, Double, Double, Double, Double)` | Resize the trigger volume |

---

### Viper.Graphics3D.Transform3D

World-space transform (position, rotation quaternion, scale) with matrix output.

**Type:** Instance (obj)
**Constructor:** `Transform3D.New()`

#### Properties

| Property   | Type   | Access     | Description |
|------------|--------|------------|-------------|
| `Position` | Object | Read       | World position as `Vec3` |
| `Rotation` | Object | Read/Write | Rotation as quaternion `Vec4(x,y,z,w)` |
| `Scale`    | Object | Read       | Scale as `Vec3` |
| `Matrix`   | Object | Read       | Combined 4×4 transform matrix as flat `Mat4` |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Set world position |
| `SetEuler(pitchDeg, yawDeg, rollDeg)` | `Void(Double, Double, Double)` | Set rotation from Euler angles in degrees |
| `SetScale(x, y, z)` | `Void(Double, Double, Double)` | Set scale |
| `Translate(delta)` | `Void(Object)` | Move by a `Vec3` offset |

Very large Euler angles and incremental rotation angles are reduced before trigonometry runs, and
the stored quaternion is kept normalized.

---

### Viper.Graphics3D.Path3D

World-space spline path for camera tracks, patrols, or procedural animation.

**Type:** Instance (obj)
**Constructor:** `Path3D.New()`

#### Properties

| Property     | Type    | Access     | Description |
|--------------|---------|------------|-------------|
| `Length`     | Double  | Read       | Total arc length of the path |
| `PointCount` | Integer | Read       | Number of control points |
| `Looping`    | Boolean | Write      | Whether the path wraps at the end |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddPoint(pos)` | `Void(Object)` | Append a `Vec3` control point |
| `GetPositionAt(t)` | `Object(Double)` | Interpolated `Vec3` position at normalized `t` `[0.0–1.0]` |
| `GetDirectionAt(t)` | `Object(Double)` | Tangent direction `Vec3` at normalized `t` |
| `Clear()` | `Void()` | Remove all control points |

Length sampling is capped internally, so very large point counts cannot overflow the subdivision step
calculation.

---

### Viper.Graphics3D.InstanceBatch3D

Efficient GPU-instanced rendering of many copies of the same mesh.

**Type:** Instance (obj)
**Constructor:** `InstanceBatch3D.New(mesh, material)`

#### Properties

| Property | Type    | Access | Description |
|----------|---------|--------|-------------|
| `Count`  | Integer | Read   | Current number of instances |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(transform)` | `Void(Object)` | Append a `Transform3D` instance |
| `Remove(index)` | `Void(Integer)` | Remove instance at `index` |
| `Set(index, transform)` | `Void(Integer, Object)` | Replace the transform at `index` |
| `Clear()` | `Void()` | Remove all instances |

`Add` and `Set` require a valid runtime `Mat4` object with a complete matrix payload; foreign or
undersized objects are ignored.

---

### Viper.Graphics3D.Sprite3D

Billboard sprite placed in 3D world space, suitable for particles, UI labels, or 2D-in-3D overlays.

**Type:** Instance (obj)
**Constructor:** `Sprite3D.New(pixels)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Set world position |
| `SetScale(w, h)` | `Void(Double, Double)` | Set billboard scale in world units |
| `SetAnchor(ax, ay)` | `Void(Double, Double)` | Set anchor offset `[0.0–1.0]` |
| `SetFrame(x, y, width, height)` | `Void(Integer, Integer, Integer, Integer)` | Set the source pixel region |

---

### Viper.Graphics3D.TextureAtlas3D

Texture atlas for 3D rendering with named-region management.

**Type:** Instance (obj)
**Constructor:** `TextureAtlas3D.New(width, height)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(pixels)` | `Integer(Object)` | Pack a `Pixels` image into the atlas; returns region index |
| `GetTexture()` | `Object()` | Return the backing `Pixels` buffer |

---

## Notes

- `Transform3D` is distinct from `SceneNode3D` — use `Transform3D` for standalone matrix math and non-scene-graph transforms; attach nodes to the scene for scene-managed transform hierarchies.
- `AnimController3D.PollEvent` returns events one at a time per call; poll it in a loop until an empty string is returned if multiple events fire in one update.
- `NavMesh3D` is rebuilt by `NavMesh3D.Build`; the mesh is not dynamic. Rebuild when geometry changes.
- `Particles3D.Draw` should be called after `Canvas3D.Flush` to render particles on top of opaque geometry.
- `Audio3D.SyncBindings` must be called once per frame after physics/animation updates so bound sources and listeners track their nodes.
