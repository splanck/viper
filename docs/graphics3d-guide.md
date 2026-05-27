# Viper.Graphics3D — User Guide

## Overview

Viper.Graphics3D is a 3D rendering module for the Viper runtime. It provides a software rasterizer (always available) with GPU-accelerated backends for Metal (macOS), Direct3D 11 (Windows), and OpenGL 3.3 (Linux). The GPU backend is selected automatically with software fallback.

**Namespace:** `Viper.Graphics3D`

For code-first game projects that want a world/entity/input layer over these
primitives, use the runtime-backed `Viper.Game3D` API documented in
[`docs/viperlib/graphics/game3d.md`](viperlib/graphics/game3d.md). Game3D is C
runtime code like the rest of the Viper runtime, not a separate Zia helper
library.

---

### Table of Contents

**Getting Started**
- [Quick Start](#quick-start)
- [Game3D](viperlib/graphics/game3d.md) — World/entity/input helpers for 3D games

**Core Classes**
- [Canvas3D](#canvas3d) — Window, frame loop, drawing, lighting
- [Mesh3D](#mesh3d) — Geometry: box, sphere, plane, OBJ/FBX/glTF loading
- [Camera3D](#camera3d) — Perspective and orthographic cameras
- [Material3D](#material3d) — Color, textures, shading models
- [Light3D](#light3d) — Directional, point, and spot lights

**Rendering Infrastructure**
- [RenderTarget3D](#rendertarget3d) — Off-screen render targets
- [CubeMap3D](#cubemap3d) — Skybox and environment maps
- [PostFX3D](#postfx3d) — Post-processing effects
- [TextureAtlas3D](#textureatlas3d) — Texture atlas packing

**Scene Management**
- [Scene3D](#scene3d) — Scene graph with frustum culling
- [SceneNode3D](#scenenode3d) — Hierarchical scene nodes
- [Transform3D](#transform3d) — 3D transformation (position, rotation, scale)
- [Model3D](#model3d) — Unified imported asset container with instantiation

**Animation**
- [Skeleton3D, Animation3D, AnimPlayer3D](#skeleton3d) — Skeletal animation
- [AnimBlend3D](#animblend3d) — Animation blending
- [AnimController3D](#animcontroller3d) — Stateful animation control, events, and root motion
- [MorphTarget3D](#morphtarget3d) — Morph target (blend shape) animation

**Environment**
- [Terrain3D](#terrain3d) — Heightmap terrain with LOD and splatting
- [Water3D](#water3d) — Gerstner wave water simulation
- [Vegetation3D](#vegetation3d) — Instanced grass and foliage
- [InstanceBatch3D](#instancebatch3d) — GPU instanced rendering
- [Sprite3D](#sprite3d) — Billboard sprites in 3D space
- [Decal3D](#decal3d) — Projected decals

**Physics**
- [Physics3DWorld, PhysicsHit3D, CollisionEvent3D, Collider3D, Physics3DBody](#physics3dworld) — Rigid body physics, queries, and contacts
- [Character3D](#character3d) — Character controller
- [Trigger3D](#trigger3d) — Trigger volumes
- [DistanceJoint3D, SpringJoint3D](#distancejoint3d) — Constraints

**Navigation**
- [NavMesh3D](#navmesh3d) — Navigation mesh pathfinding
- [NavAgent3D](#navagent3d) — Goal-driven agent path following and bindings
- [Path3D](#path3d) — 3D path waypoints

**Collision Queries**
- [Ray3D, RayHit3D, AABB3D, Sphere3D, Segment3D, Capsule3D](#particles3d) — Geometry queries

**Media**
- [VideoPlayer](#videoplayer) — Video playback (MJPEG/AVI, OGV)

**Format Loaders**
- [FBX](#fbx) — Low-level FBX extractor API
- [GLTF](#gltf) — Low-level glTF extractor API

**Operational Reference**
- [Backend Selection](#backend-selection) — GPU vs. software rendering
- [Performance Tips](#performance-tips)
- [Resource Limits](#resource-limits)
- [Error Handling](#error-handling)
- [Threading](#threading)

---

## Quick Start

### Zia

```zia
module HelloCube;

bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Graphics3D.Light3D;
bind Viper.Math.Vec3;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("My 3D App", 640, 480);
    var cam = Camera3D.New(60.0, 640.0 / 480.0, 0.1, 100.0);
    var eye = Vec3.New(0.0, 2.0, 5.0);
    var target = Vec3.New(0.0, 0.0, 0.0);
    var up = Vec3.New(0.0, 1.0, 0.0);
    Camera3D.LookAt(cam, eye, target, up);

    var box = Mesh3D.NewBox(1.0, 1.0, 1.0);
    var mat = Material3D.NewColor(0.8, 0.2, 0.2);

    var light_dir = Vec3.New(-1.0, -1.0, -0.5);
    var light = Light3D.NewDirectional(light_dir, 1.0, 1.0, 1.0);
    Canvas3D.SetLight(canvas, 0, light);
    Canvas3D.SetAmbient(canvas, 0.1, 0.1, 0.1);

    var angle = 0.0;
    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        var xform = Mat4.RotateY(angle);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawMesh(canvas, box, xform, mat);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
        angle = angle + 0.02;
    }
}
```

### BASIC

```basic
USING Viper.Graphics3D
USING Viper.Math

DIM canvas AS Canvas3D = Canvas3D.New("My 3D App", 640, 480)
DIM cam AS Camera3D = Camera3D.New(60.0, 640.0/480.0, 0.1, 100.0)
cam.LookAt(Vec3.New(0, 2, 5), Vec3.Zero(), Vec3.New(0, 1, 0))

DIM box AS Mesh3D = Mesh3D.NewBox(1.0, 1.0, 1.0)
DIM mat AS Material3D = Material3D.NewColor(0.8, 0.2, 0.2)
DIM light AS Light3D = Light3D.NewDirectional(Vec3.New(-1,-1,-0.5), 1, 1, 1)

canvas.SetLight(0, light)
canvas.SetAmbient(0.1, 0.1, 0.1)

DIM angle AS DOUBLE = 0.0
DO WHILE NOT canvas.ShouldClose
    canvas.Poll()
    canvas.Clear(0.1, 0.1, 0.2)
    canvas.Begin(cam)
    canvas.DrawMesh(box, Mat4.RotateY(angle), mat)
    canvas.End()
    canvas.Flip()
    angle = angle + 0.02
LOOP
```

## Canvas3D

The rendering surface. Creates a window and manages the render loop.

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `ShouldClose` | Boolean | read | True when user closes window |
| `Width` | Integer | read | Active output width in pixels (window, or current RenderTarget3D when bound) |
| `Height` | Integer | read | Active output height in pixels (window, or current RenderTarget3D when bound) |
| `Fps` | Integer | read | Frames per second |
| `DeltaTime` | Integer | read | Milliseconds since last Flip, or fixed synthetic dt when synthetic clock is selected (first live frame = 0, capped to 100ms by default) |
| `DeltaTimeMs` | Integer | read | Explicit millisecond alias for `DeltaTime` |
| `DeltaTimeSec` | Number | read | Seconds since last Flip or synthetic frame, using the same clamp as `DeltaTime` |
| `Backend` | String | read | Active renderer: "software", "metal", "d3d11", "opengl" |
| `BackendCapabilities` | Integer | read | Bitmask of `Canvas3D` backend capabilities |
| `QualityRequested` | Integer | read | Last requested quality profile (`0` performance, `1` balanced, `2` cinematic) |
| `QualityActive` | Integer | read | Active quality profile after backend fallback |
| `QualityFallback` | Boolean | read | True when quality setup degraded to stay backend-safe |
| `QualityFallbackReason` | String | read | Human-readable fallback reason, or empty string |
| `FrameFinalized` | Boolean | read | True after `FinalizeFrame()` or `ScreenshotFinal()` has applied post-FX/final overlays for the current frame |
| `Wireframe` | Boolean | write | Toggle wireframe rendering (default: off) |

### Constructors

| Constructor | Description |
|-------------|-------------|
| `New(title, w, h)` | Create canvas window (1-8192 pixels per dimension) |

### Core Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Clear(r, g, b)` | `void(f64, f64, f64)` | Clear framebuffer and depth buffer (0.0-1.0 per channel) |
| `Begin(camera)` | `void(obj)` | Start 3D frame — must be called before DrawMesh |
| `Begin2D()` | `void()` | Start 2D overlay mode for the active output (closed by `End()`) |
| `BeginOverlay()` | `void()` | Start recording a final overlay pass composited after post-FX |
| `EndOverlay()` | `void()` | Finish final overlay recording |
| `ClearOverlay()` | `void()` | Discard recorded final overlay commands for the current frame |
| `End()` | `void()` | End the current 3D or 2D draw pass; this does not run post-FX or present |
| `FinalizeFrame()` | `void()` | Apply post-FX and replay the final overlay once, without presenting |
| `ScreenshotFinal()` | `obj()` | Finalize if needed, then capture the final frame as `Pixels` |
| `Flip()` | `void()` | Finalize if needed, present frame to screen, compute DeltaTime |
| `Poll()` | `i64()` | Process window events and update `Keyboard`/`Mouse`/actions; returns the last raw window event code |
| `BackendSupports(capability)` | `i1(str)` | Test a named backend capability such as `shadows`, `skybox`, `render_target`, `window_readback`, `hardware_instancing`, `postfx`, `gpu_postfx`, `postfx-overlay`, `final-screenshot`, or `gpu-postfx-overlay` |

### Drawing Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `DrawMesh(mesh, transform, material)` | `void(obj, obj, obj)` | Draw a mesh with Mat4 transform and material |
| `DrawMeshSkinned(mesh, transform, material, animPlayer)` | `void(obj, obj, obj, obj)` | Draw with skeletal animation (CPU skinning) |
| `DrawMeshMorphed(mesh, transform, material, morphTarget)` | `void(obj, obj, obj, obj)` | Draw with morph target deformation |
| `DrawMeshBlended(mesh, transform, material, blender)` | `void(obj, obj, obj, obj)` | Draw with animation blend tree |
| `DrawInstanced(batch)` | `void(obj)` | Draw InstanceBatch3D (hardware instancing) |
| `DrawTerrain(terrain)` | `void(obj)` | Draw Terrain3D |
| `DrawDecal(decal)` | `void(obj)` | Draw Decal3D |
| `DrawSprite3D(sprite, camera)` | `void(obj, obj)` | Draw billboard Sprite3D |
| `DrawWater(water, camera)` | `void(obj, obj)` | Draw Water3D surface |
| `DrawVegetation(vegetation)` | `void(obj)` | Draw Vegetation3D |

### Lighting & Environment

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetLight(index, light)` | `void(i64, obj)` | Bind or clear a retained Light3D slot (0-15) |
| `ClearLights()` | `void()` | Clear every retained canvas light slot |
| `SetDefaultLighting()` | `void()` | Install a conservative directional key/fill plus readable ambient |
| `LightCount` | `i64` property | Count active enabled canvas-slot lights |
| `SetAmbient(r, g, b)` | `void(f64, f64, f64)` | Set ambient light color; values are clamped to `0..1` |
| `SetSkybox(cubemap)` | `void(obj)` | Set CubeMap3D skybox |
| `ClearSkybox()` | `void()` | Remove skybox |
| `SetFog(near, far, r, g, b)` | `void(f64, f64, f64, f64, f64)` | Enable linear distance fog; distances and RGB are sanitized |
| `ClearFog()` | `void()` | Disable fog |
| `EnableShadows(mapSize)` | `void(i64)` | Enable shadow mapping (mapSize = shadow map resolution; up to the two strongest directional lights receive shadow maps) |
| `DisableShadows()` | `void()` | Disable shadow mapping |
| `SetShadowBias(bias)` | `void(f64)` | Set shadow acne bias |

### Render Settings

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetBackfaceCull(enabled)` | `void(i1)` | Toggle backface culling (default: on) |
| `SetDTMax(ms)` | `void(i64)` | Cap DeltaTime to prevent spiral-of-death (`ms <= 0` disables the cap; default cap is 100ms) |
| `SetQuality(profile)` | `void(i64)` | Apply a backend-safe post-FX profile: `0` performance, `1` balanced, `2` cinematic |
| `SetInputSource(mode)` | `void(i64)` | Select input source: `0` live, `1` synthetic, `2` live plus synthetic |
| `PushSyntheticKey(key, down)` | `void(i64, i1)` | Queue a synthetic keyboard transition for the next synthetic frame |
| `PushSyntheticMouse(dx, dy, buttons, wheel)` | `void(f64, f64, i64, f64)` | Queue synthetic mouse delta, button bitmask, and vertical wheel delta |
| `ClearSyntheticInput()` | `void()` | Clear queued synthetic input and release synthetic-held keys/buttons |
| `SetClockSource(mode)` | `void(i64)` | Select clock source: `0` live wall clock, `1` fixed synthetic dt |
| `SetSyntheticDeltaTimeSec(dt)` | `void(f64)` | Set fixed synthetic delta time in seconds |
| `AdvanceSyntheticFrame()` | `void()` | Advance one deterministic input/timing frame without pumping platform events |
| `SetRenderTarget(target)` | `void(obj)` | Redirect rendering to offscreen RenderTarget3D |
| `ResetRenderTarget()` | `void()` | Return to window rendering |
| `SetPostFX(fx)` | `void(obj)` | Set PostFX3D chain applied during frame finalization to the window or active render target; SSAO/DOF/motion blur require GPU window postfx |
| `SetFrustumCulling(enabled)` | `void(i1)` | Toggle coarse CPU frustum rejection plus front-to-back opaque ordering |
| `SetOcclusionCulling(enabled)` | `void(i1)` | Compatibility alias for `SetFrustumCulling`; this is not hardware occlusion-query culling |

`Poll()` is the live-loop input boundary. It updates `Viper.Input.Keyboard`,
`Viper.Input.Mouse`, gamepad state, and action mappings; most gameplay code
should read those APIs instead of branching on the raw event code returned by
`Poll()`. The raw return is useful for diagnostics or low-level integrations.

For deterministic tests, select synthetic input and clock before the scripted
frames:

```rust
Canvas3D.SetInputSource(canvas, 1)
Canvas3D.SetClockSource(canvas, 1)
Canvas3D.SetSyntheticDeltaTimeSec(canvas, 1.0 / 60.0)
Canvas3D.PushSyntheticKey(canvas, Keyboard.get_KEY_W(), true)
Canvas3D.PushSyntheticMouse(canvas, 8.0, -2.0, 1, 0.0)
Canvas3D.AdvanceSyntheticFrame(canvas)
```

Synthetic keys and mouse samples flow through the normal `Keyboard` and `Mouse`
state paths, so `WasPressed`, `IsDown`, `Mouse.DeltaX`, button edges, and action
bindings observe the same state shape as live input. `ClearSyntheticInput()`
also releases keys/buttons held by the synthetic source so tests do not leak
state into the next run.

First-frame live timing can be zero because `DeltaTime` is measured after the
first `Flip()`. Code-first loops should seed or clamp their first `dt` before
moving gameplay. The synthetic clock reports the configured fixed dt after
`AdvanceSyntheticFrame()`, `Poll()` in synthetic mode, or `Flip()`.

`SetQuality()` currently configures the canvas post-FX chain. Performance and
Balanced use CPU/software-safe effects only. Cinematic adds SSAO, depth of field,
and motion blur only when the active canvas can present GPU post-FX to the
window; otherwise it uses a CPU-safe cinematic chain and sets
`QualityFallback`/`QualityFallbackReason` for debug overlays. Re-apply quality
after changing output mode if a game switches between a GPU window and a
render target.

Render targets must be bound or reset outside a `Begin`/`End` frame. Changing the active output
mid-frame is rejected so queued draws, overlays, post-processing, and readback state all target one
consistent surface.

### Debug Drawing

| Method | Signature | Description |
|--------|-----------|-------------|
| `DrawLine3D(from, to, color)` | `void(obj, obj, i64)` | Draw 3D line (color = 0xRRGGBB) |
| `DrawPoint3D(pos, color, size)` | `void(obj, i64, i64)` | Draw 3D point |
| `DrawAABBWire(min, max, color)` | `void(obj, obj, i64)` | Draw wireframe AABB |
| `DrawSphereWire(center, radius, color)` | `void(obj, f64, i64)` | Draw wireframe sphere |
| `DrawDebugRay(origin, dir, length, color)` | `void(obj, obj, f64, i64)` | Draw debug ray |
| `DrawAxis(transform, size)` | `void(obj, f64)` | Draw XYZ axes at a Mat4 position |
| `Screenshot()` | `obj()` | Capture the active output as `Pixels` without forcing finalization |
| `ScreenshotFinal()` | `obj()` | Finalize first, then capture post-FX plus final-overlay pixels as `Pixels` |

### HUD Overlay (2D)

| Method | Signature | Description |
|--------|-----------|-------------|
| `DrawRect2D(x, y, w, h, color)` | `void(i64, i64, i64, i64, i64)` | Draw 2D rectangle on screen |
| `DrawText2D(x, y, text, color)` | `void(i64, i64, str, i64)` | Draw 2D text on screen |
| `DrawCrosshair(color, size)` | `void(i64, i64)` | Draw centered crosshair |

`DrawRect2D`, `DrawText2D`, and `DrawCrosshair` remain convenient immediate HUD helpers.
When called between `End()` and `Flip()`, they use the legacy overlay path and are part of
the frame before final post-processing. Use `BeginOverlay()` and `EndOverlay()` for HUD,
debug text, reticles, and capture overlays that must stay crisp after bloom, tonemapping,
or color grading.

### Zia Example

```zia
module Canvas3DDemo;

bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Graphics3D.Light3D;
bind Viper.Math.Vec3;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("Demo", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);
    Camera3D.LookAt(cam, Vec3.New(0.0, 2.0, 5.0), Vec3.Zero(), Vec3.New(0.0, 1.0, 0.0));

    var box = Mesh3D.NewBox(1.0, 1.0, 1.0);
    var mat = Material3D.NewColor(0.8, 0.2, 0.2);

    var light = Light3D.NewDirectional(Vec3.New(-1.0, -1.0, -0.5), 1.0, 1.0, 1.0);
    Canvas3D.SetLight(canvas, 0, light);
    Canvas3D.SetAmbient(canvas, 0.1, 0.1, 0.1);

    // Enable fog and shadows
    Canvas3D.SetFog(canvas, 10.0, 50.0, 0.5, 0.5, 0.6);
    Canvas3D.EnableShadows(canvas, 1024);

    var angle = 0.0;
    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawMesh(canvas, box, Mat4.RotateY(angle), mat);
        Canvas3D.End(canvas);

        // Final HUD overlay, composited after post-FX.
        Canvas3D.BeginOverlay(canvas);
        Canvas3D.DrawText2D(canvas, 10, 10, "Hello 3D!", 0xFFFFFF);
        Canvas3D.DrawCrosshair(canvas, 0xFFFFFF, 12);
        Canvas3D.EndOverlay(canvas);

        Canvas3D.Flip(canvas);
        angle = angle + 0.02;
    }
}
```

**Frame lifecycle:** `Poll → Clear → Begin → DrawMesh (repeated) → End → [BeginOverlay → HUD/debug draws → EndOverlay] → Flip`

`End()` only flushes queued geometry for the current 3D or 2D pass. `FinalizeFrame()`
is the idempotent boundary that applies post-FX and replays the final overlay. `Flip()`
calls `FinalizeFrame()` if needed, then presents. `ScreenshotFinal()` also calls
`FinalizeFrame()` if needed and captures the finalized pixels without presenting, so
the usual capture path is `EndOverlay → ScreenshotFinal → Flip`.

For a compact, executable example of this path, see
`examples/3d/walk_min.zia`. Its companion `walk_min_probe.zia` renders one
software-backend frame, captures with `ScreenshotFinal()`, checks the crisp final
overlay, and compares to the committed baseline in `examples/3d/baselines/`.

**Important:** `Begin`/`End` and `BeginOverlay`/`EndOverlay` must not nest. All 3D draw calls go between `Begin` and `End`; `DrawTerrain` and `DrawVegetation` are rejected during `Begin2D`. Legacy HUD overlay calls (`DrawRect2D`, `DrawText2D`, `DrawCrosshair`) may still be called between `End` and `Flip`, but final overlays should be grouped with `BeginOverlay`/`EndOverlay`. `DrawMesh` and instanced draws require finite transform matrices; invalid matrices are rejected before they reach culling or backend submission. Draw submission clamps material colors and PBR scalars before narrowing to backend floats. Deferred heap `Mesh3D` draws retain the mesh object instead of copying static geometry every draw; do not mutate a submitted heap mesh until after `Canvas3D.End()`.

## Mesh3D

3D geometry with vertices and triangle indices.

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create empty mesh |
| `NewBox(sx, sy, sz)` | `obj(f64, f64, f64)` | Axis-aligned box (24 vertices, 12 triangles) |
| `NewSphere(radius, segments)` | `obj(f64, i64)` | UV sphere (min 4 segments) |
| `NewPlane(sx, sz)` | `obj(f64, f64)` | XZ plane facing +Y (4 vertices, 2 triangles) |
| `NewCylinder(radius, height, segments)` | `obj(f64, f64, i64)` | Cylinder with caps (min 3 segments) |
| `FromOBJ(path)` | `obj(str)` | Load Wavefront OBJ file |
| `FromSTL(path)` | `obj(str)` | Load STL file (binary or ASCII) |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `VertexCount` | Integer | read | Number of vertices |
| `TriangleCount` | Integer | read | Number of triangles |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddVertex(x, y, z, nx, ny, nz, u, v)` | `void(f64 x8)` | Add vertex with position, normal, and UV |
| `AddTriangle(i0, i1, i2)` | `void(i64, i64, i64)` | Add triangle from vertex indices (CCW winding) |
| `Clear()` | `void()` | Reset vertex/index counts to zero (reuse backing arrays) |
| `RecalcNormals()` | `void()` | Auto-compute vertex normals from face geometry |
| `CalcTangents()` | `void()` | Compute tangent vectors (required for normal mapping) |
| `Clone()` | `obj()` | Deep copy of mesh data |
| `Transform(mat4)` | `void(obj)` | Transform all vertices in-place by Mat4 |

### Skeletal and Morph Extensions

These are available both as class methods and through their fully qualified static names.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Mesh3D.SetSkeleton(mesh, skeleton)` | `void(obj, obj)` | Bind a Skeleton3D to the mesh |
| `Mesh3D.SetBoneWeights(mesh, vtx, b0, w0, b1, w1, b2, w2, b3, w3)` | `void(obj, i64, i64, f64, i64, f64, i64, f64, i64, f64)` | Set bone indices + weights for a vertex (4 bones max) |
| `Mesh3D.SetMorphTargets(mesh, morphTarget)` | `void(obj, obj)` | Bind a MorphTarget3D to the mesh |

`SetBoneWeights` drops invalid bone indices and non-positive or non-finite weights, normalizes the remaining positive weights, updates the mesh's skinned bone count, and invalidates cached geometry so renderer-side buffers refresh.

### Zia Example

```zia
module MeshDemo;

bind Viper.Graphics3D.Mesh3D;

func start() {
    // Procedural triangle
    var mesh = Mesh3D.New();
    Mesh3D.AddVertex(mesh, -0.5, -0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    Mesh3D.AddVertex(mesh,  0.5, -0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    Mesh3D.AddVertex(mesh,  0.0,  0.5, 0.0, 0.0, 0.0, 1.0, 0.5, 1.0);
    Mesh3D.AddTriangle(mesh, 0, 1, 2);

    // Primitives
    var box = Mesh3D.NewBox(1.0, 1.0, 1.0);
    var sphere = Mesh3D.NewSphere(0.5, 16);
    var plane = Mesh3D.NewPlane(10.0, 10.0);
    var cyl = Mesh3D.NewCylinder(0.5, 2.0, 12);

    // File loading
    var model = Mesh3D.FromOBJ("assets/model.obj");

    // Compute tangents for normal mapping
    Mesh3D.CalcTangents(model);
}
```

### Winding Order

All mesh generators and the OBJ loader produce **counter-clockwise (CCW)** winding for front faces. When constructing meshes programmatically, vertices must be ordered CCW when viewed from the front.

**Mesh validation:** Procedural generators reject non-finite and non-positive dimensions. `NewPlane` emits +Y-facing triangles, matching its vertex normals and backface-culling expectations. Sphere and cylinder segment counts are clamped to production-safe maxima to avoid accidental unbounded allocation. `AddVertex` traps on non-finite or out-of-float-range vertex data. `AddTriangle` traps on negative, out-of-range, duplicate-index, collinear, or otherwise degenerate triangles and marks the mesh build failed until `Clear()` resets it. `RecalcNormals` and `CalcTangents` skip degenerate or overflowing face contributions instead of narrowing invalid double intermediates into renderer floats.

**Tangents:** `CalcTangents()` uses position/UV derivatives with Gram-Schmidt orthogonalization and `tangent.w` handedness for mirrored UVs. Degenerate UV islands get a normalized fallback tangent orthogonal to the vertex normal so normal maps never receive a tangent parallel to the normal.

**OBJ loader:** Supports v/vn/vt tuples, negative indices, inline face comments, and arbitrary n-gons through fan triangulation. The loader deduplicates identical `(position, uv, normal)` tuples so indexed assets do not balloon into one vertex per face corner. Invalid face indices trap and abort the load instead of emitting corrupt geometry. `.mtl`, `usemtl`, `g`, and `o` directives are parsed and flattened but do not create per-material submeshes.

**STL loader:** Auto-detects binary vs ASCII format, streams exact binary STL payloads without buffering the full file, and computes normals for valid triangles.

## Camera3D

Perspective or orthographic camera with view and projection matrices.

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(fov, aspect, near, far)` | `obj(f64, f64, f64, f64)` | Create perspective camera (fov in degrees) |
| `NewOrtho(size, aspect, near, far)` | `obj(f64, f64, f64, f64)` | Create orthographic camera (size = half-height in world units) |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Fov` | Float | read/write | Field of view in degrees (perspective only) |
| `Position` | Vec3 | read/write | Camera world position |
| `Forward` | Vec3 | read | Camera forward direction |
| `Right` | Vec3 | read | Camera right direction |
| `IsOrtho` | Boolean | read | True for orthographic cameras |
| `Yaw` | Float | read/write | Horizontal rotation angle (FPS mode, degrees) |
| `Pitch` | Float | read/write | Vertical rotation angle (FPS mode, degrees) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `LookAt(eye, target, up)` | `void(obj, obj, obj)` | Point camera from eye toward target (Vec3 args) |
| `Orbit(target, distance, yaw, pitch)` | `void(obj, f64, f64, f64)` | Orbit around target (angles in degrees) |
| `ScreenToRay(sx, sy, sw, sh)` | `obj(i64, i64, i64, i64)` | Return a normalized world-space pick direction (Vec3). Perspective rays should pair it with `ScreenToRayOrigin()` or `GetPosition()`. Orthographic cameras return their forward direction. During active `Shake`, the ray matches the shaken render pose. |
| `ScreenToRayOrigin(sx, sy, sw, sh)` | `obj(i64, i64, i64, i64)` | Return the matching world-space pick origin (Vec3). Perspective cameras return the shaken render eye; orthographic cameras return the unprojected near-plane point for that screen pixel. |
| `Shake(intensity, duration, decay)` | `void(f64, f64, f64)` | Apply camera shake effect |
| `SmoothFollow(target, speed, height, distance, dt)` | `void(obj, f64, f64, f64, f64)` | Smoothly follow a Vec3 target position |
| `SmoothLookAt(target, speed, dt)` | `void(obj, f64, f64)` | Smoothly rotate toward a Vec3 target |
| `FPSInit()` | `void()` | Extract yaw/pitch from current view matrix |
| `FPSUpdate(mdx, mdy, fwd, right, up, speed, dt)` | `void(f64, f64, f64, f64, f64, f64, f64)` | FPS mouse look + WASD movement |

`Yaw`, `Pitch`, `Orbit`, and `Light3D.NewSpot` all use degrees. Writing `Yaw` or `Pitch` updates the camera view immediately.
`Canvas3D.Begin(canvas, camera)` uses the active output's aspect ratio (window or bound `RenderTarget3D`) when building that frame's projection, so perspective remains correct across resizes and RTT passes without mutating the camera object's stored projection/aspect.
Camera constructors and control methods sanitize invalid numeric inputs at the API boundary: non-finite FOV/aspect/clip planes, degenerate `LookAt` vectors, invalid FPS deltas, and invalid shake/follow parameters fall back to finite defaults so view matrices, projection matrices, `ScreenToRay()`, and `ScreenToRayOrigin()` results remain usable.

### Zia Example

```zia
module CameraDemo;

bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Math.Vec3;

func start() {
    var canvas = Canvas3D.New("Camera Demo", 800, 600);

    // Perspective camera
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);
    Camera3D.LookAt(cam, Vec3.New(0.0, 2.0, 5.0), Vec3.Zero(), Vec3.New(0.0, 1.0, 0.0));

    // Orbit camera around target
    Camera3D.Orbit(cam, Vec3.Zero(), 5.0, 45.0, 30.0);

    // Orthographic camera for UI/isometric
    var ortho = Camera3D.NewOrtho(10.0, 800.0 / 600.0, 0.1, 100.0);

    // Camera shake (e.g., on explosion)
    Camera3D.Shake(cam, 0.5, 0.3, 5.0);

    // Smooth follow for third-person
    var player_pos = Vec3.New(5.0, 0.0, 3.0);
    Camera3D.SmoothFollow(cam, player_pos, 4.0, 3.0, 5.0, 0.016);
}
```

**Orthographic cameras** have no perspective foreshortening. Use for isometric RPGs, strategy games, 2D-in-3D rendering, and UI overlays.

**Coordinate system:** Right-handed. +X right, +Y up, +Z toward viewer. Projection uses OpenGL NDC convention (Z: [-1,1]).

## Material3D

Surface appearance for meshes, models, decals, and other 3D drawables.

`Material3D` is now PBR-first. The default legacy Blinn-Phong path still exists for compatibility and for custom shading-model hooks, but new content should usually start with `NewPBR`.

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Default white material |
| `NewColor(r, g, b)` | `obj(f64, f64, f64)` | Colored material (0.0-1.0 per channel) |
| `NewTextured(pixels)` | `obj(obj)` | Material with Pixels texture |
| `NewPBR(r, g, b)` | `obj(f64, f64, f64)` | Metallic/roughness material with albedo color |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Alpha` | Float | read/write | Opacity [0.0=invisible, 1.0=opaque]. Default 1.0. Setting alpha below 1.0 promotes `AlphaMode` from `Opaque` to `Blend`; returning to 1.0 restores `Opaque` only when that promotion was automatic. |
| `Metallic` | Float | read/write | PBR metallic factor [0.0=dielectric, 1.0=metal]. Default 0.0 |
| `Roughness` | Float | read/write | PBR roughness [0.0=smooth, 1.0=rough]. Default 0.5 |
| `AO` | Float | read/write | Ambient-occlusion multiplier. Default 1.0 |
| `EmissiveIntensity` | Float | read/write | Scalar applied to emissive color/map. Default 1.0 |
| `NormalScale` | Float | read/write | Tangent-space normal XY scale. Default 1.0 |
| `AlphaMode` | Integer | read/write | `0=Opaque`, `1=Mask`, `2=Blend` |
| `DoubleSided` | Bool | read/write | Disable backface culling when true |
| `Reflectivity` | Float | read/write | Environment reflection strength [0.0-1.0] |
| `Color` | Vec3 | read | Current diffuse/base color |
| `Unlit` | Bool | read | Whether lighting is ignored |
| `ShadingModel` | Integer | read | Current shading model index |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Clone()` | `obj()` | Duplicate the material state |
| `MakeInstance()` | `obj()` | Duplicate the material for per-object overrides |
| `SetColor(r, g, b)` | `void(f64, f64, f64)` | Change diffuse color |
| `SetTexture(pixels)` | `void(obj)` | Set/change texture (Pixels) |
| `SetAlbedoMap(pixels)` | `void(obj)` | Set/change the PBR albedo map |
| `SetShininess(s)` | `void(f64)` | Specular exponent (default 32.0, higher = sharper highlights) |
| `SetUnlit(flag)` | `void(i1)` | Skip lighting (render flat color) |
| `SetMetallic(value)` | `void(f64)` | Set the metallic factor |
| `SetRoughness(value)` | `void(f64)` | Set the roughness factor |
| `SetAO(value)` | `void(f64)` | Set the AO multiplier |
| `SetEmissiveIntensity(value)` | `void(f64)` | Scale emissive output |
| `SetNormalMap(pixels)` | `void(obj)` | Set tangent-space normal map (Pixels) |
| `SetMetallicRoughnessMap(pixels)` | `void(obj)` | Set the glTF-style metallic/roughness map (`G=roughness`, `B=metallic`) |
| `SetAOMap(pixels)` | `void(obj)` | Set the ambient-occlusion map (`R=occlusion`) |
| `SetSpecularMap(pixels)` | `void(obj)` | Set specular intensity map (Pixels) |
| `SetEmissiveMap(pixels)` | `void(obj)` | Set emissive color map (Pixels) |
| `SetEmissiveColor(r, g, b)` | `void(f64, f64, f64)` | Set emissive color multiplier (additive glow) |
| `SetNormalScale(value)` | `void(f64)` | Scale tangent-space normal-map strength |
| `SetShadingModel(model)` | `void(i64)` | Set shading model (see table below) |
| `SetCustomParam(index, value)` | `void(i64, f64)` | Set custom shader parameter (index 0-7) |
| `SetEnvMap(cubemap)` | `void(obj)` | Set environment CubeMap3D for reflections |

### Workflow Notes

- `NewPBR` and `SetShadingModel(2)` select the metallic/roughness workflow directly.
- Calling `SetMetallic`, `SetRoughness`, `SetAO`, `SetMetallicRoughnessMap`, or `SetAOMap` on a legacy material promotes it into the PBR workflow.
- `Clone()` and `MakeInstance()` both return independent material objects. They eagerly copy scalar state and share the currently referenced texture/cubemap objects by pointer. After cloning, either material can replace its maps independently.
- Color and scalar setters sanitize input at the runtime boundary: colors and PBR factors are clamped to valid ranges, non-finite custom parameters become `0`, and non-finite shadow/fog/material values fall back to deterministic safe defaults. The draw path repeats finite/clamp validation before backend command submission.
- Texture map setters accept `Pixels` handles only, and `SetEnvMap` accepts `CubeMap3D` handles only. Invalid handle types are ignored instead of being retained into material state.
- `AlphaMode` changes how texture alpha is interpreted for PBR materials:
  - `0`: opaque. Texture/material alpha does not enable blending, and surviving fragments write depth as opaque.
  - `1`: masked. Fragments below the cutoff are discarded; surviving fragments render as opaque coverage. Masked materials also cast alpha-tested shadows on the software, Metal, OpenGL, and D3D11 backends.
  - `2`: blended. Texture/material alpha participates in transparency and transparent sorting.
- Explicit `SetAlphaMode` calls take precedence over alpha auto-promotion. For example, a material
  explicitly set to `Blend` remains blended even if `Alpha` is later set back to `1.0`.
- `SetShadingModel` and `SetCustomParam` remain available as advanced escape hatches. They are not the main PBR API.

**Shading models:** `SetShadingModel` selects how the surface is shaded on the legacy path and can post-process the PBR result:
- **0 (BlinnPhong)**: Default. Diffuse + specular highlight.
- **1 (Toon)**: Quantized diffuse bands. `custom[0]` = number of bands (default 4).
- **2 (PBR)**: Selects the metallic/roughness workflow. Use the dedicated PBR setters for material data.
- **3 (Unlit)**: Same visual result as `SetUnlit(true)`.
- **4 (Fresnel)**: Angle-dependent alpha — edges glow brighter. `custom[0]` = power (default 3), `custom[1]` = bias.
- **5 (Emissive)**: Boosted emissive glow. `custom[0]` = strength multiplier (default 2).

See `examples/apiaudit/graphics3d/shading_demo.zia` for the legacy/custom-model path and `examples/apiaudit/graphics3d/material3d_pbr_demo.zia` / `examples/apiaudit/graphics3d/material3d_pbr_demo.bas` for the PBR workflow.

**Ownership:** `Material3D` retains `Pixels` and `CubeMap3D` references internally. The user does not need to hold a second manual reference just to keep assigned textures/maps/cubemaps alive.

### Zia Example

```zia
module MaterialDemo;

bind Viper.Graphics3D.Material3D;

func start() {
    var base = Material3D.NewPBR(0.8, 0.6, 0.4);
    Material3D.set_Metallic(base, 0.7);
    Material3D.set_Roughness(base, 0.3);
    Material3D.set_AO(base, 0.9);
    Material3D.set_EmissiveIntensity(base, 1.4);
    Material3D.set_AlphaMode(base, 2); // blend
    Material3D.set_DoubleSided(base, 1);

    var inst = Material3D.MakeInstance(base);
    Material3D.set_Roughness(inst, 0.75);

    var legacy = Material3D.NewColor(0.4, 0.6, 1.0);
    Material3D.SetShadingModel(legacy, 1);     // Toon
    Material3D.SetCustomParam(legacy, 0, 3.0); // 3 bands
}
```

## Light3D

Light sources for the scene. Up to 16 lights simultaneously.

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `NewDirectional(direction, r, g, b)` | `obj(obj, f64, f64, f64)` | Sun-like light from a direction (Vec3) |
| `NewPoint(position, r, g, b, attenuation)` | `obj(obj, f64, f64, f64, f64)` | Local point light with distance falloff |
| `NewSpot(pos, dir, r, g, b, atten, inner, outer)` | `obj(obj, obj, f64, f64, f64, f64, f64, f64)` | Spot light with cone falloff (angles in degrees) |
| `NewAmbient(r, g, b)` | `obj(f64, f64, f64)` | Uniform ambient light |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetIntensity(value)` | `void(f64)` | Brightness multiplier (default 1.0) |
| `SetColor(r, g, b)` | `void(f64, f64, f64)` | Change light color |
| `SetEnabled(enabled)` | `void(i1)` | Toggle whether the light contributes without clearing its slot |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Type` | Integer | read | `0=directional`, `1=point`, `2=ambient`, `3=spot` |
| `Color` | Vec3 | read | Current normalized RGB color |
| `Intensity` | Float | read | Current brightness multiplier |
| `Enabled` | Bool | read/write | Disabled lights are skipped by `Canvas3D` light submission |
| `Direction` | Vec3 | read | Normalized direction for directional and spot lights |
| `Position` | Vec3 | read | Position for point and spot lights |

Light colors are clamped to `[0, 1]`, intensities and attenuations are clamped to non-negative values, and non-finite positions/directions fall back to finite defaults. Spot cone angles are clamped to `0..89` degrees and reordered when needed so `inner_cos >= outer_cos`.

### Zia Example

```zia
module LightDemo;

bind Viper.Graphics3D.Light3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Math.Vec3;

func start() {
    var canvas = Canvas3D.New("Lights", 800, 600);

    // Directional sunlight
    var sun = Light3D.NewDirectional(Vec3.New(-1.0, -1.0, -0.5), 1.0, 0.95, 0.8);

    // Point light at position with falloff
    var torch = Light3D.NewPoint(Vec3.New(3.0, 2.0, 0.0), 1.0, 0.6, 0.2, 10.0);
    Light3D.SetIntensity(torch, 2.0);

    // Spot light (flashlight)
    var spot = Light3D.NewSpot(
        Vec3.New(0.0, 5.0, 0.0), Vec3.New(0.0, -1.0, 0.0),
        1.0, 1.0, 1.0, 15.0, 20.0, 35.0);

    // Ambient fill
    var ambient = Light3D.NewAmbient(0.1, 0.1, 0.15);

    // Bind lights to canvas
    Canvas3D.SetLight(canvas, 0, sun);
    Canvas3D.SetLight(canvas, 1, torch);
    Canvas3D.SetLight(canvas, 2, spot);
    Canvas3D.SetLight(canvas, 3, ambient);
    Canvas3D.SetAmbient(canvas, 0.05, 0.05, 0.05);
}
```

`Canvas3D.SetLight()` retains the assigned light until you replace that slot or clear it with `null`.
Spot-light inner and outer cone angles are sanitized to remain finite and strictly separated before
their cosines are sent to the software and GPU backends, avoiding undefined falloff at equal cones.

**Lighting model:** Blinn-Phong with per-vertex (software) or per-pixel (GPU) shading. Includes diffuse and specular components.

## RenderTarget3D

Offscreen rendering targets for render-to-texture effects (TV screens, mirrors, security cameras, post-processing).

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(width, height)` | `obj(i64, i64)` | Create offscreen target (1-8192 pixels per dimension) |
| `NewHdr(width, height)` | `obj(i64, i64)` | Create HDR offscreen target with RGBA16F internal color storage |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `IsHdr` | Bool | read | True when the target stores HDR color internally |
| `Width` | Integer | read | Target width |
| `Height` | Integer | read | Target height |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AsPixels()` | `obj()` | Read back color buffer as a new Pixels object |

### Zia Example

```zia
module RenderTargetDemo;

bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.RenderTarget3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("RTT Demo", 800, 600);
    var cam = Camera3D.New(60.0, 1.0, 0.1, 100.0);
    var target = RenderTarget3D.New(256, 256);
    var screen_mat = Material3D.New();
    var screen_mesh = Mesh3D.NewPlane(2.0, 2.0);

    // Render scene to offscreen target
    Canvas3D.SetRenderTarget(canvas, target);
    Canvas3D.Clear(canvas, 0.2, 0.0, 0.0);
    Canvas3D.Begin(canvas, cam);
    // ... draw objects to target ...
    Canvas3D.End(canvas);

    // Use result as texture on a quad
    Material3D.SetTexture(screen_mat, RenderTarget3D.AsPixels(target));
    Canvas3D.ResetRenderTarget(canvas);

    // Draw main scene with the textured quad
    Canvas3D.Begin(canvas, cam);
    Canvas3D.DrawMesh(canvas, screen_mesh, Mat4.Identity(), screen_mat);
    Canvas3D.End(canvas);
    Canvas3D.Flip(canvas);
}
```

**Note:** `AsPixels()` returns a fresh copy each call. The render target's CPU-side color/depth buffers are allocated lazily on first CPU access (or when the software backend binds the target), so GPU-only RTT passes do not pay the host-memory cost up front.
HDR targets created with `NewHdr()` keep their GPU color attachment in `RGBA16F`, but `AsPixels()` still returns standard `Pixels`. GPU readback keeps both a tonemapped RGBA8 mirror and a linear RGBA32F CPU mirror; render-target postfx consumes the linear HDR mirror for Bloom, Tonemap, FXAA, ColorGrade, and Vignette before final 8-bit conversion so highlights are not clamped before the chain runs.
When a render target is bound, `Canvas3D.Width`, `Canvas3D.Height`, `Begin2D()`, debug overlays, and `Screenshot()` all operate in that target's pixel space instead of the window's.
`Canvas3D.Begin()` also uses the target's aspect ratio for that frame's projection while the render target is bound, so switching between the window and RTT views does not stretch perspective or rewrite the camera's stored projection.
`Canvas3D.SetRenderTarget()` accepts `RenderTarget3D` handles only and prepares the target's color/depth storage before handing it to the active backend, so a successful bind has a valid CPU mirror for software rendering and later readback.
**PostFX:** If a render target is active when you call `Flip()`, the canvas applies the current CPU-supported `PostFX3D` chain to that render target instead of the window backbuffer. SSAO, DOF, and motion blur require GPU window postfx because they need scene depth/history/velocity buffers; on a render target or software CPU path they trap with a clear error instead of silently no-oping.

## CubeMap3D

Six-face cube texture for skyboxes and environment reflections.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(right, left, top, bottom, front, back)` | `obj(obj, obj, obj, obj, obj, obj)` | Create cubemap from 6 Pixels faces |

CubeMap3D has no methods or properties — it is a data object used by `Canvas3D.SetSkybox` and `Material3D.SetEnvMap`.
CubeMap faces use the same top-left pixel origin as `Pixels`; the GPU backends normalize upload orientation so skyboxes and reflections sample consistently across backends.
Skyboxes honor the active camera projection across all backends. Perspective cameras reconstruct a per-pixel view ray; orthographic cameras sample the cubemap along the camera forward direction so the background stays stable instead of being distorted by perspective-only math. When a backend lacks a native skybox hook, the CPU fallback caches the generated RGBA skybox by cubemap generation, output size, and camera projection, then blits the cached image on stable frames instead of resampling the cubemap every frame.
Environment reflections are roughness-aware. Low-roughness materials keep a sharp cubemap reflection, while higher roughness values sample blurrier cubemap mips on GPU backends and a matching blur kernel in the software renderer.
CubeMap uploads and CPU fallback skybox caches are keyed by a stable internal cubemap identity plus the six face `Pixels` generations, so recreating or mutating cubemaps cannot accidentally reuse stale GPU skybox, CPU fallback, or reflection textures after allocator address reuse.
Seam handling is also more consistent now: the software sampler remaps bilinear taps across neighboring faces and OpenGL enables seamless cubemap filtering, which reduces visible face-edge seams when the artwork itself lines up.

### Zia Example

```zia
module CubeMapDemo;

bind Viper.Graphics3D.CubeMap3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Material3D;
bind Viper.IO.Pixels;

func start() {
    var canvas = Canvas3D.New("Skybox Demo", 800, 600);

    // Load 6 face textures
    var right = Pixels.Load("skybox_right.png");
    var left  = Pixels.Load("skybox_left.png");
    var top   = Pixels.Load("skybox_top.png");
    var bot   = Pixels.Load("skybox_bottom.png");
    var front = Pixels.Load("skybox_front.png");
    var back  = Pixels.Load("skybox_back.png");

    var skybox = CubeMap3D.New(right, left, top, bot, front, back);
    Canvas3D.SetSkybox(canvas, skybox);

    // Use same cubemap for material environment reflections
    var chrome = Material3D.NewColor(0.9, 0.9, 0.9);
    Material3D.SetEnvMap(chrome, skybox);
    Material3D.set_Reflectivity(chrome, 0.8);
}
```

---

## Scene3D

Hierarchical scene graph with frustum culling and LOD support.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create scene with root node |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Root` | SceneNode3D | read | Root node of the scene tree |
| `NodeCount` | Integer | read | Total nodes in tree |
| `CulledCount` | Integer | read | Nodes culled in last Draw |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(node)` | `void(obj)` | Add node to root |
| `Remove(node)` | `void(obj)` | Detach node from parent |
| `Find(name)` | `obj(str)` | Recursive depth-first name search |
| `Draw(canvas, cam)` | `void(obj, obj)` | Traverse + render (with frustum culling) |
| `Clear()` | `void()` | Remove all children from root |
| `Save(path)` | `i64(str)` | Write JSON scene snapshot (returns 0 on success) |
| `SyncBindings(dt)` | `void(f64)` | Apply scene-node body / animator bindings before draw |

---

## SceneNode3D

Individual node in a Scene3D tree with transform, mesh, material, and child hierarchy.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create empty scene node |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Position` | Vec3 | read | Local position |
| `Rotation` | Quat | read/write | Local rotation |
| `Scale` | Vec3 | read | Local scale |
| `WorldMatrix` | Mat4 | read | Computed world transform (lazy) |
| `WorldPosition` | Vec3 | read | World-space position without manual matrix decomposition |
| `WorldScale` | Vec3 | read | World-space scale magnitudes without manual matrix decomposition |
| `ChildCount` | Integer | read | Number of child nodes |
| `Parent` | SceneNode3D | read | Parent node (null if root) |
| `Visible` | Boolean | read/write | Visibility (hides node + all descendants) |
| `Name` | String | read/write | Name for Find() lookup |
| `Mesh` | Mesh3D | write | Mesh to render |
| `Material` | Material3D | write | Material for rendering |
| `AABBMin` | Vec3 | read | Subtree axis-aligned bounding box minimum in this node's local space |
| `AABBMax` | Vec3 | read | Subtree axis-aligned bounding box maximum in this node's local space |
| `Body` | Physics3DBody | read | Bound body used by `SyncBindings` |
| `Animator` | AnimController3D | read | Bound controller used for root motion and skinned draw submission |
| `SyncMode` | Integer | read/write | Transform sync policy used by `Scene3D.SyncBindings` |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Set local position |
| `SetScale(x, y, z)` | `void(f64, f64, f64)` | Set local scale |
| `AddChild(child)` | `void(obj)` | Attach child (auto-detaches from previous parent) |
| `RemoveChild(child)` | `void(obj)` | Detach child node |
| `GetChild(index)` | `obj(i64)` | Get child by index |
| `Find(name)` | `obj(str)` | Recursive name search in subtree |
| `BindBody(body)` | `void(obj)` | Attach a `Physics3DBody` for transform sync |
| `ClearBodyBinding()` | `void()` | Remove the current body binding |
| `BindAnimator(controller)` | `void(obj)` | Attach an `AnimController3D` for root motion and animated draw submission |
| `ClearAnimatorBinding()` | `void()` | Remove the current animator binding |
| `AddLOD(distance, mesh)` | `void(f64, obj)` | Add LOD mesh at distance threshold |
| `ClearLOD()` | `void()` | Remove all LOD levels |

### Zia Example

```zia
module SceneDemo;

bind Viper.Graphics3D.Scene3D;
bind Viper.Graphics3D.SceneNode3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Math.Quat;

func start() {
    var canvas = Canvas3D.New("Scene Demo", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var scene = Scene3D.New();

    // Create a tree node
    var trunk = SceneNode3D.New();
    SceneNode3D.set_Name(trunk, "trunk");
    SceneNode3D.SetPosition(trunk, 0.0, 0.0, 0.0);
    SceneNode3D.set_Mesh(trunk, Mesh3D.NewCylinder(0.3, 3.0, 8));
    SceneNode3D.set_Material(trunk, Material3D.NewColor(0.4, 0.25, 0.1));

    // Child node (branches)
    var branch = SceneNode3D.New();
    SceneNode3D.set_Name(branch, "branch");
    SceneNode3D.SetPosition(branch, 0.0, 2.5, 0.0);
    SceneNode3D.set_Mesh(branch, Mesh3D.NewSphere(1.5, 12));
    SceneNode3D.set_Material(branch, Material3D.NewColor(0.1, 0.6, 0.1));

    // LOD: use low-poly sphere at distance
    SceneNode3D.AddLOD(branch, 20.0, Mesh3D.NewSphere(1.5, 4));

    SceneNode3D.AddChild(trunk, branch);
    Scene3D.Add(scene, trunk);

    // Find node by name
    var found = Scene3D.Find(scene, "branch");

    // Render loop
    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Scene3D.Draw(scene, canvas, cam);
        Canvas3D.Flip(canvas);
    }
}
```

Transform order: `world = parent_world * Translate * Rotate * Scale`. Dirty flags propagate to descendants automatically. Finite zero scale is preserved on `Transform3D` and `SceneNode3D`; only non-finite scale components are replaced. `Scene3D.Save` writes a `.vscn` asset with embedded meshes, materials, textures, cubemaps, and node hierarchy using round-trip float precision. `Scene3D.Load` validates JSON, base64 payloads, mesh indices, asset references, and child nodes before returning a scene; invalid partial assets fail the load instead of being skipped.

### Binding Sync

`Scene3D.SyncBindings(dt)` is the explicit bridge between simulation / animation systems and the scene graph. `Scene3D.Draw` does not mutate bound bodies or controllers.

`SceneNode3D.SyncMode` values:

- `0` = `NodeFromBody`: pull the bound `Physics3DBody` world pose into the node.
- `1` = `BodyFromNode`: push the node world pose into the bound body.
- `2` = `NodeFromAnimatorRootMotion`: consume root motion from the bound `AnimController3D` into the node's local transform (translation plus rotation).
- `3` = `TwoWayKinematic`: push node-to-body while the body is kinematic, otherwise pull body-to-node.

Recommended frame order:

1. Step physics and update animation controllers.
2. Call `Scene3D.SyncBindings(dt)`.
3. Call `Scene3D.Draw(canvas, camera)`.

When `NodeFromAnimatorRootMotion` is active, `Scene3D.SyncBindings(dt)` consumes both translation and rotation deltas from the controller's configured root-motion bone once per controller update.

Current scope:

- `SceneNode3D` bindings currently cover `Physics3DBody` and `AnimController3D`.
- `NavAgent3D` now provides its own `BindNode` / `BindCharacter` workflow for navigation-driven motion.
- `SoundListener3D` and `SoundSource3D` now use `Sound3D.SyncBindings(dt)`, and `Scene3D.SyncBindings(dt)` forwards into that audio-binding pass after node/body/anim synchronization.

## Model3D

`Model3D` is the preferred high-level import surface for reusable 3D assets. It normalizes `.vscn`, `.fbx`, `.gltf`, `.glb`, and geometry-only `.obj` files into one container that keeps shared meshes, materials, skeletons, animations, and a template node hierarchy together.

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `MeshCount` | Integer | read | Number of shared `Mesh3D` objects |
| `MaterialCount` | Integer | read | Number of shared `Material3D` objects |
| `SkeletonCount` | Integer | read | Number of imported `Skeleton3D` objects |
| `AnimationCount` | Integer | read | Number of imported `Animation3D` clips |
| `NodeCount` | Integer | read | Number of imported logical scene nodes (excluding the synthetic template root) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Load(path)` | `obj(str)` | Load `.vscn`, `.fbx`, `.gltf`, `.glb`, or `.obj` into a `Model3D` |
| `LoadAsset(path)` | `obj(str)` | Load through `Viper.IO.Assets`; `.gltf` external buffers/images resolve relative to the model asset |
| `GetMesh(index)` | `obj(i64)` | Get a shared `Mesh3D` by index |
| `GetMaterial(index)` | `obj(i64)` | Get a shared `Material3D` by index |
| `GetSkeleton(index)` | `obj(i64)` | Get a shared `Skeleton3D` by index |
| `GetAnimation(index)` | `obj(i64)` | Get a shared `Animation3D` by index |
| `FindNode(name)` | `obj(str)` | Find a template `SceneNode3D` by name inside the imported hierarchy |
| `Instantiate()` | `obj()` | Clone the template hierarchy into a fresh `SceneNode3D` subtree |
| `InstantiateScene()` | `obj()` | Create a fresh `Scene3D` and attach cloned top-level imported nodes below its root |

### Ownership and Instancing

- Imported meshes, materials, skeletons, and animations are shared across instances.
- OBJ-backed models create one default material and synthesized `mesh_N` template nodes.
- `Instantiate()` clones nodes and transforms only. The returned node is a synthetic root group that owns the imported top-level nodes.
- Mutating an instantiated node does not mutate the template returned by `FindNode`.
- `InstantiateScene()` is the easiest way to drop an imported asset into a fresh scene while preserving node names and hierarchy.

### Zia Example

```zia
module Model3DDemo;

bind Viper.Graphics3D;
bind Viper.Terminal;

func start() {
    var model = Model3D.Load("tree.gltf");
    var templateNode = Model3D.FindNode(model, "Trunk");
    var instanceRoot = Model3D.Instantiate(model);
    var scene = Model3D.InstantiateScene(model);

    Say("Nodes = " + toString(Model3D.get_NodeCount(model)));
    Say("Meshes = " + toString(Model3D.get_MeshCount(model)));
    Say("Template trunk found = " + toString(templateNode != null));
    Say("Instance root children = " + toString(SceneNode3D.get_ChildCount(instanceRoot)));
    Say("Scene nodes = " + toString(Scene3D.get_NodeCount(scene)));
}
```

For game-facing asset loading, prefer `Model3D.Load` for loose filesystem files during early development and `Model3D.LoadAsset` for code that should also work from embedded or mounted `.vpa` packages. `LoadAsset` accepts both plain asset paths such as `"assets/tree.glb"` and explicit URIs such as `"asset://tree.glb"`; mounted assets are checked before the development filesystem fallback. Use the lower-level `FBX` and `GLTF` helpers when you explicitly want extractor-style access to importer-native arrays.

Format note:
- `.vscn`, FBX, and glTF imports can populate shared skeletons and animation clips when the source format contains supported skin/animation data.
- FBX-backed `Model3D` assets preserve authored `Model` hierarchy, local TRS, and mesh/material attachments when the source file contains object connections, instead of always collapsing to synthetic `mesh_N` nodes.
- OBJ-backed `Model3D` assets use the existing geometry-only OBJ loader and synthesize template nodes because OBJ has no scene hierarchy.
- glTF imports populate meshes, materials, active-scene node hierarchy, skins, morph targets, punctual lights, skeletal clips, and node/morph animation clips.
- glTF skeletal tracks map to `Skeleton3D` / `Animation3D`; non-joint node translation, rotation, scale, and morph `weights` tracks are bound automatically on `Model3D.Instantiate()` and `InstantiateScene()`. Node animation channels reject non-finite sample data and non-increasing key times before playback; LINEAR rotation tracks use quaternion slerp, and CUBICSPLINE tracks use glTF Hermite tangents. Call `Scene3D.SyncBindings(dt)` each frame to advance those imported node clips.
- glTF mesh extraction supports `POSITION`, `NORMAL`, `TEXCOORD_0`, `TEXCOORD_1`, `COLOR_0`, `TANGENT`, `JOINTS_0`/`WEIGHTS_0`, and `JOINTS_1`/`WEIGHTS_1`. Secondary joint sets are reduced to the four strongest supported influences and renormalized. Skins above the runtime 256-bone palette are rejected instead of silently dropping the rig.
- glTF morph targets import `POSITION`, `NORMAL`, and `TANGENT` deltas. Position/normal morphs can use the GPU path; tangent morphs currently route through the CPU morph path so tangent-space normal mapping stays correct.
- glTF node hierarchies are rejected if they contain invalid child references, duplicate parents, or cycles; valid meshes/materials still remain available to the asset container.
- Triangle-list, triangle-strip, and triangle-fan glTF primitives are triangulated on import.
- Materialless glTF primitives receive a shared default white PBR material so valid assets render through `Scene3D` / `Model3D` without manual material assignment.
- VSCN round-trips the current `vgfx3d_vertex_le_v2` vertex layout, per-slot material texture metadata, node-attached lights, and high-precision node transforms, while still loading older `vgfx3d_vertex_le_v1` scenes. The loader rejects malformed JSON/base64, invalid mesh index buffers, broken node references, and partial child subtrees; finite transform/material/light values are sanitized during load.
- `.glb` files are validated as GLB 2.0 containers before JSON parse. External `.gltf` buffers and images are resolved relative to the asset path and reject absolute paths, URI schemes, and `.` / `..` traversal segments. In `LoadAsset`, those external dependencies are loaded through `Viper.IO.Assets` first and missing-dependency diagnostics name both the parent model and dependency path.
- glTF `extensionsRequired` is enforced. Required `KHR_texture_transform`, `KHR_materials_emissive_strength`, `KHR_materials_unlit`, and `KHR_lights_punctual` are accepted; unsupported required extensions such as Draco, Meshopt, Basis/KTX2, DDS, and exact advanced material extensions fail load rather than rendering incomplete fallback data.

## Skeleton3D

Bone hierarchy for skeletal animation.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create empty skeleton |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `BoneCount` | Integer | read | Number of bones |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddBone(name, parentIdx, bindPose)` | `i64(str, i64, obj)` | Add bone (returns index). parentIdx=-1 for root. bindPose is Mat4 |
| `ComputeInverseBind()` | `void()` | Compute inverse bind matrices (call after all bones added) |
| `FindBone(name)` | `i64(str)` | Find bone index by name (-1 if not found) |
| `GetBoneName(index)` | `str(i64)` | Get bone name by index |

Bones must be added in topological order (parent before child). Max 256 bones per skeleton.
Add all bones before binding the skeleton to a mesh or creating an `AnimPlayer3D`, `AnimBlend3D`,
or `AnimController3D`; those runtime objects allocate fixed-size pose buffers and freeze the
skeleton topology.

---

## Animation3D

Keyframe animation clip with per-bone position, rotation, and scale tracks.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(name, duration)` | `obj(str, f64)` | Create animation clip with name and duration in seconds |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Looping` | Boolean | read/write | Loop playback |
| `Duration` | Float | read | Total duration in seconds |
| `Name` | String | read | Animation name |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddKeyframe(boneIdx, time, pos, rot, scale)` | `void(i64, f64, obj, obj, obj)` | Add keyframe: bone index, time, position Vec3, rotation Quat, scale Vec3 |

Keyframes are kept sorted by time within each bone channel. Rotation keyframes use normalized
quaternions and SLERP; position/scale use linear interpolation. `pos`, `rot`, or `scale` may be
`null`; omitted or non-finite/out-of-float-range components fall back to the bone bind pose instead
of erasing that component to zero/identity.

---

## AnimPlayer3D

Playback controller for skeletal animation.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(skeleton)` | `obj(obj)` | Create player bound to a Skeleton3D |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Speed` | Float | read/write | Playback speed multiplier |
| `IsPlaying` | Boolean | read | Currently playing |
| `Time` | Float | read/write | Current playback time in seconds |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Play(animation)` | `void(obj)` | Start playing an Animation3D |
| `Crossfade(animation, duration)` | `void(obj, f64)` | Blend to new animation over duration (SLERP for rotation) |
| `Stop()` | `void()` | Stop playback and output the bind pose |
| `Update(dt)` | `void(f64)` | Advance animation by dt seconds |
| `GetBoneMatrix(boneIdx)` | `obj(i64)` | Get the current global/world Mat4 for a bone |

Negative playback speeds play clips in reverse. Looping clips wrap in both directions; non-looping
clips clamp at the start/end and stop when they hit the boundary. Player and animation handles are
validated before playback, so wrong graphics object types are ignored instead of being sampled as
animation memory.

### Zia Example

```zia
module SkeletonDemo;

bind Viper.Graphics3D.Skeleton3D;
bind Viper.Graphics3D.Animation3D;
bind Viper.Graphics3D.AnimPlayer3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Math.Mat4;
bind Viper.Math.Vec3;
bind Viper.Math.Quat;

func start() {
    var canvas = Canvas3D.New("Skeleton", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    // Build skeleton
    var skel = Skeleton3D.New();
    Skeleton3D.AddBone(skel, "root", -1, Mat4.Identity());
    Skeleton3D.AddBone(skel, "arm", 0, Mat4.Translate(1.0, 0.0, 0.0));
    Skeleton3D.ComputeInverseBind(skel);

    // Create walk animation
    var walk = Animation3D.New("walk", 1.0);
    Animation3D.set_Looping(walk, true);
    var pos0 = Vec3.New(0.0, 0.0, 0.0);
    var pos1 = Vec3.New(0.0, 0.5, 0.0);
    var rot = Quat.Identity();
    var scl = Vec3.One();
    Animation3D.AddKeyframe(walk, 0, 0.0, pos0, rot, scl);
    Animation3D.AddKeyframe(walk, 0, 0.5, pos1, rot, scl);
    Animation3D.AddKeyframe(walk, 0, 1.0, pos0, rot, scl);

    // Play animation
    var player = AnimPlayer3D.New(skel);
    AnimPlayer3D.Play(player, walk);

    // Bind skeleton to mesh
    Mesh3D.SetSkeleton(mesh, skel);

    // Render loop
    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        var dt = Canvas3D.get_DeltaTime(canvas);
        AnimPlayer3D.Update(player, dt / 1000.0);

        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawMeshSkinned(canvas, mesh, Mat4.Identity(), mat, player);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

- `DrawMeshSkinned` applies CPU or GPU skinning via the internal skinning palette. Skinning weights are normalized consistently across CPU and GPU paths; missing palettes copy vertices through unchanged, and unused backend bone-palette slots behave as identity transforms.
- `Crossfade` blends every bone using TRS decomposition: position/scale linearly interpolated, rotation via quaternion SLERP. Channels present in only one clip blend against bind pose, and the fading-out clip keeps its own speed/looping settings during the transition.

## MorphTarget3D

Blend shapes for facial animation, muscle flex, and shape-based deformation.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(vertexCount)` | `obj(i64)` | Create morph target set for a mesh vertex count |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `ShapeCount` | Integer | read | Number of registered shapes |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddShape(name)` | `i64(str)` | Register a blend shape (returns shape index) |
| `SetDelta(shapeIdx, vertexIdx, dx, dy, dz)` | `void(i64, i64, f64, f64, f64)` | Set position delta for a vertex in a shape |
| `SetNormalDelta(shapeIdx, vertexIdx, dx, dy, dz)` | `void(i64, i64, f64, f64, f64)` | Set normal delta for a vertex in a shape |
| `SetWeight(shapeIdx, weight)` | `void(i64, f64)` | Set blend weight for a shape, clamped to `[-1, 1]` |
| `GetWeight(shapeIdx)` | `f64(i64)` | Get current blend weight |
| `SetWeightByName(name, weight)` | `void(str, f64)` | Set blend weight by shape name |

### Zia Example

```zia
module MorphDemo;

bind Viper.Graphics3D.MorphTarget3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("Morph Demo", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var mesh = Mesh3D.NewSphere(1.0, 16);
    var mat = Material3D.NewColor(0.8, 0.6, 0.5);

    // Create morph targets
    var morph = MorphTarget3D.New(Mesh3D.get_VertexCount(mesh));
    var smile = MorphTarget3D.AddShape(morph, "smile");
    MorphTarget3D.SetDelta(morph, smile, 10, 0.1, 0.05, 0.0);
    MorphTarget3D.SetDelta(morph, smile, 11, -0.1, 0.05, 0.0);

    // Bind to mesh
    Mesh3D.SetMorphTargets(mesh, morph);

    // Animate weight
    MorphTarget3D.SetWeight(morph, smile, 0.7);

    // Draw morphed mesh
    Canvas3D.Begin(canvas, cam);
    Canvas3D.DrawMeshMorphed(canvas, mesh, Mat4.Identity(), mat, morph);
    Canvas3D.End(canvas);
}
```

- Shape storage grows on demand
- Normal deltas optional (lazy-allocated on first `SetNormalDelta`)
- Weights can be negative (reverse deformation)
- `New(vertexCount)` bounds allocation size, deltas sanitize non-finite values to `0`, and non-finite weights become `0` before clamping to the supported range.
- `Canvas3D.DrawMeshMorphed` requires a `Mat4` transform, a `Mesh3D`, and a matching `MorphTarget3D`; mismatched vertex counts or invalid handles skip the draw without dereferencing the wrong object type.
- GPU-applied on Metal and on OpenGL/D3D11 while the active shape count fits backend shader limits; otherwise CPU-applied as `finalPos = basePos + sum(weight * delta)` per vertex. GPU backends clamp active shape counts to shader-indexable limits and disable the morph path on upload failure rather than reusing stale buffers.

## FBX Loader

Low-level extractor API for meshes, skeletons, materials, animations, and morph targets from binary FBX files (v7100-7700). For instantiation-ready imported assets, prefer `Model3D.Load("asset.fbx")`.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `Load(path)` | `obj(str)` | Parse binary FBX file |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `MeshCount` | Integer | read | Number of geometry objects |
| `AnimationCount` | Integer | read | Number of animation stacks |
| `MaterialCount` | Integer | read | Number of materials |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `GetMesh(index)` | `obj(i64)` | Get Mesh3D by index |
| `GetSkeleton()` | `obj()` | Get Skeleton3D (or null) |
| `GetAnimation(index)` | `obj(i64)` | Get Animation3D by index |
| `GetAnimationName(index)` | `str(i64)` | Get animation name by index |
| `GetMaterial(index)` | `obj(i64)` | Get Material3D by index |
| `GetMorphTarget(index)` | `obj(i64)` | Get MorphTarget3D by index |

### Zia Example

```zia
module FBXDemo;

bind Viper.Graphics3D.FBX;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.AnimPlayer3D;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("FBX Demo", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var asset = FBX.Load("character.fbx");
    var mesh = FBX.GetMesh(asset, 0);
    var skel = FBX.GetSkeleton(asset);
    var mat = FBX.GetMaterial(asset, 0);

    // Play first animation
    var player = AnimPlayer3D.New(skel);
    var anim = FBX.GetAnimation(asset, 0);
    AnimPlayer3D.Play(player, anim);

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        AnimPlayer3D.Update(player, Canvas3D.get_DeltaTime(canvas) / 1000.0);
        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawMeshSkinned(canvas, mesh, Mat4.Identity(), mat, player);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

Supports zlib-compressed array properties, negative polygon indices, and Z-up to Y-up coordinate conversion. `Model3D.Load("asset.fbx")` adapts these extracted resources into an instantiable scene asset and now preserves authored FBX `Model` hierarchy when the file contains object connections.

---

## GLTF Loader

Low-level extractor API for meshes and materials from glTF 2.0 files. `Model3D.Load` uses the same loader internally and preserves the active-scene node hierarchy for instantiation.

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `GLTF.Load(path)` | `obj(str)` | Parse glTF file |
| `GLTF.LoadAsset(path)` | `obj(str)` | Parse glTF/GLB through `Viper.IO.Assets`, including package-relative external dependencies |
| `GLTF.get_MeshCount(asset)` | `i64(obj)` | Number of meshes |
| `GLTF.GetMesh(asset, index)` | `obj(obj, i64)` | Get Mesh3D by index |
| `GLTF.get_MaterialCount(asset)` | `i64(obj)` | Number of materials |
| `GLTF.GetMaterial(asset, index)` | `obj(obj, i64)` | Get Material3D by index |

### Zia Example

```zia
module GLTFDemo;

bind Viper.Graphics3D.GLTF;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("GLTF Demo", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var asset = GLTF.Load("scene.gltf");
    var mesh = GLTF.GetMesh(asset, 0);
    var mat = GLTF.GetMaterial(asset, 0);

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawMesh(canvas, mesh, Mat4.Identity(), mat);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

**Note:** GLTF is class-backed in the runtime catalog and also works as a low-level extractor helper. It preserves the active-scene hierarchy, matrix-authored node transforms, extended mesh attributes, materials, skeletons, animations, and morph targets listed above. For preserved node hierarchies and scene instantiation, load `.gltf` or `.glb` through `Model3D.Load`.

Supported glTF material fidelity:
- Core metallic-roughness PBR, base-color / normal / metallic-roughness / occlusion / emissive texture slots, alpha modes, `doubleSided`, and `KHR_materials_emissive_strength`. PBR base-color and emissive textures are decoded from sRGB to linear before lighting on software, Metal, D3D11, and OpenGL.
- `KHR_materials_unlit` is supported as a required extension. `KHR_materials_specular`, `KHR_materials_clearcoat`, and `KHR_materials_transmission` are mapped onto the current `Material3D` surface where possible when optional, but assets that mark those advanced material extensions as required are rejected until the renderer has exact shader support.
- `KHR_texture_transform`, `textureInfo.texCoord`, wrap mode, and nearest/linear filter state are preserved independently for base-color, normal, specular, emissive, metallic-roughness, and occlusion texture slots across software, Metal, D3D11, and OpenGL.
- `KHR_lights_punctual` directional, point, and spot lights attach to their authored scene nodes. `Scene3D.Draw` transforms them by node world pose and includes them in the per-draw light snapshot; imported directional lights participate in shadow selection from that snapshot, and glTF `range` maps to the runtime quadratic attenuation coefficient.

## Particles3D

Emitter-based 3D particle effects with physics, lifetime, and billboard rendering.
Particle setters sanitize non-finite values: ranges are kept non-negative and ordered, alpha and
direction spread are clamped, invalid directions fall back to +Y, and invalid update deltas are ignored.
Rendering is batched per emitter. Additive particles skip sorting, while alpha particles sort a
temporary key array back-to-front and submit one billboard mesh without reordering the live particle
array.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(maxParticles)` | `obj(i64)` | Create particle emitter with max capacity |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Count` | Integer | read | Number of active particles |
| `Emitting` | Boolean | read | Whether emitter is active |
| `Additive` | Boolean | write | Additive particle blending mode (fire, sparks). Default: false. Preserves each particle's own alpha/intensity. |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Set emitter world position |
| `SetDirection(dx, dy, dz, spreadDegrees)` | `void(f64, f64, f64, f64)` | Set emission direction + cone spread in degrees |
| `SetSpeed(min, max)` | `void(f64, f64)` | Set speed range |
| `SetLifetime(min, max)` | `void(f64, f64)` | Set lifetime range in seconds |
| `SetSize(start, end)` | `void(f64, f64)` | Set particle size (start/end interpolation) |
| `SetGravity(x, y, z)` | `void(f64, f64, f64)` | Set gravity vector |
| `SetColor(startColor, endColor)` | `void(i64, i64)` | Set color range (0xRRGGBB) |
| `SetAlpha(start, end)` | `void(f64, f64)` | Set alpha range (fade out: 1.0 → 0.0) |
| `SetRate(particlesPerSec)` | `void(f64)` | Set emission rate |
| `SetTexture(pixels)` | `void(obj)` | Set particle sprite (null for solid quads) |
| `SetEmitterShape(shape)` | `void(i64)` | 0=point, 1=sphere, 2=box |
| `SetEmitterSize(sx, sy, sz)` | `void(f64, f64, f64)` | Set emitter volume (for sphere/box shapes) |
| `Start()` | `void()` | Start continuous emission |
| `Stop()` | `void()` | Stop emission |
| `Burst(count)` | `void(i64)` | Instantly spawn N particles |
| `Clear()` | `void()` | Remove all active particles |
| `Update(dt)` | `void(f64)` | Advance simulation by dt seconds |
| `Draw(canvas, camera)` | `void(obj, obj)` | Render particles (between Begin/End) |

### Zia Example

```zia
module ParticleDemo;

bind Viper.Graphics3D.Particles3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;

func start() {
    var canvas = Canvas3D.New("Particles", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var sparks = Particles3D.New(500);
    Particles3D.SetPosition(sparks, 0.0, 0.0, 0.0);
    Particles3D.SetDirection(sparks, 0.0, 1.0, 0.0, 23.0);
    Particles3D.SetSpeed(sparks, 2.0, 5.0);
    Particles3D.SetLifetime(sparks, 0.5, 1.5);
    Particles3D.SetSize(sparks, 0.2, 0.05);
    Particles3D.SetGravity(sparks, 0.0, -9.8, 0.0);
    Particles3D.SetColor(sparks, 0xFFAA22, 0xFF2200);
    Particles3D.SetAlpha(sparks, 1.0, 0.0);
    Particles3D.SetRate(sparks, 50.0);
    Particles3D.set_Additive(sparks, true);
    Particles3D.Start(sparks);

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        var dt = Canvas3D.get_DeltaTime(canvas) / 1000.0;
        Particles3D.Update(sparks, dt);

        Canvas3D.Clear(canvas, 0.0, 0.0, 0.0);
        Canvas3D.Begin(canvas, cam);
        Particles3D.Draw(sparks, canvas, cam);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

- Particles are billboarded (camera-facing)
- Additive mode uses true additive blending and stays fully batched in one draw call
- Alpha blend mode sorts particles back-to-front and submits per-particle keyed draws so blending stays correct against the rest of the scene

## PostFX3D

Full-screen post-processing effect chain applied automatically in `Canvas3D.Flip()` to the active canvas output.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create empty post-processing chain |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Enabled` | Boolean | read/write | Enable/disable entire chain (bypass temporarily) |
| `EffectCount` | Integer | read | Number of effects in chain |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `NewQuality(canvas, profile)` | `obj(obj, i64)` | Create a backend-safe quality chain for a canvas (`0` performance, `1` balanced, `2` cinematic) |
| `AddBloom(threshold, intensity, passes)` | `void(f64, f64, i64)` | Bloom glow effect |
| `AddTonemap(mode, exposure)` | `void(i64, f64)` | Tone mapping (0=off, 1=Reinhard, 2=ACES) |
| `AddFXAA()` | `void()` | Fast approximate anti-aliasing |
| `AddColorGrade(brightness, contrast, saturation)` | `void(f64, f64, f64)` | Color grading |
| `AddVignette(radius, softness)` | `void(f64, f64)` | Screen-edge darkening |
| `AddSSAO(radius, intensity, samples)` | `void(f64, f64, i64)` | Screen-space ambient occlusion |
| `AddDOF(focusDist, aperture, maxBlur)` | `void(f64, f64, f64)` | Depth of field |
| `AddMotionBlur(strength, samples)` | `void(f64, i64)` | Velocity-buffer motion blur |
| `Clear()` | `void()` | Remove all effects from chain |

Effects run strictly in append order. If you add the same effect type more than once, each pass is preserved instead of being collapsed into one combined backend setting. The GPU backends now follow that same ordered-chain behavior as the CPU path, so `Flip()`, GPU screenshots, and GPU readback all match the authored `PostFX3D` chain. Bloom `passes` is part of the backend snapshot so GPU paths can widen the bloom radius consistently with the authored quality setting.

PostFX parameters are bounded before they reach CPU or GPU shaders: bloom passes clamp to `0..32`, SSAO samples to `1..128`, motion-blur samples to `1..64`, vignette softness has a non-zero floor, and non-finite exposure/radius/intensity values fall back to safe defaults.

Bloom, Tonemap, FXAA, ColorGrade, and Vignette run on both GPU outputs and CPU render-target/software fallback outputs. SSAO, DOF, and MotionBlur require `Canvas3D.BackendSupports(canvas, "gpu_postfx")` and a window-backed `Flip()` so the backend can provide depth, scene history, and motion vectors; attaching those effects to a render target or software CPU path raises an explicit runtime trap.

### Zia Example

```zia
module PostFXDemo;

bind Viper.Graphics3D.PostFX3D;
bind Viper.Graphics3D.Canvas3D;

func start() {
    var canvas = Canvas3D.New("PostFX Demo", 800, 600);

    var fx = PostFX3D.New();
    PostFX3D.AddBloom(fx, 0.8, 0.5, 5);
    PostFX3D.AddTonemap(fx, 1, 1.2);
    PostFX3D.AddFXAA(fx);
    PostFX3D.AddColorGrade(fx, 0.05, 1.1, 1.2);
    PostFX3D.AddVignette(fx, 0.6, 0.4);
    PostFX3D.AddSSAO(fx, 0.5, 1.0, 16);
    PostFX3D.AddDOF(fx, 10.0, 5.0, 1.0);
    Canvas3D.SetPostFX(canvas, fx);

    // Temporarily disable
    PostFX3D.set_Enabled(fx, false);

    // Re-enable
    PostFX3D.set_Enabled(fx, true);

    // Reset chain
    PostFX3D.Clear(fx);
}
```

Effects are applied in chain order (first added = first applied). Chain storage grows as needed instead of truncating at a fixed 8-effect limit.

## Ray3D / AABB3D / Sphere3D / Segment3D / Capsule3D / RayHit3D

3D raycasting and collision detection for picking, shooting, and physics. These are all standalone functions (no classes).

### Ray3D — Raycasting

| Function | Signature | Description |
|----------|-----------|-------------|
| `Ray3D.IntersectTriangle(o, d, v0, v1, v2)` | `f64(obj, obj, obj, obj, obj)` | Möller-Trumbore; returns distance or -1 |
| `Ray3D.IntersectMesh(o, d, mesh, transform)` | `obj(obj, obj, obj, obj)` | Test all triangles, returns closest RayHit3D or null |
| `Ray3D.IntersectAABB(o, d, min, max)` | `f64(obj, obj, obj, obj)` | Slab method; returns distance or -1 |
| `Ray3D.IntersectSphere(o, d, center, radius)` | `f64(obj, obj, obj, f64)` | Quadratic formula; returns distance or -1 |

Ray directions are normalized internally for all `Ray3D` intersection helpers. A zero-length or non-finite direction is a miss, and returned distances are Euclidean world distances even when the input direction was not normalized. Sphere hits from inside the sphere return distance `0`.

### RayHit3D — Hit result

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Distance` | Float | read | Distance from ray origin to hit point |
| `Point` | Vec3 | read | World-space hit position |
| `Normal` | Vec3 | read | Surface normal at hit point |
| `TriangleIndex` | Integer | read | Index of hit triangle in mesh |

### AABB3D — Box collision

| Function | Signature | Description |
|----------|-----------|-------------|
| `AABB3D.Overlaps(minA, maxA, minB, maxB)` | `i1(obj, obj, obj, obj)` | Boolean overlap test |
| `AABB3D.Penetration(minA, maxA, minB, maxB)` | `obj(obj, obj, obj, obj)` | Minimum push-out Vec3 |
| `AABB3D.ClosestPoint(min, max, point)` | `obj(obj, obj, obj)` | Closest point on AABB to a point |
| `AABB3D.SphereOverlaps(min, max, center, radius)` | `i1(obj, obj, obj, f64)` | AABB vs sphere overlap test |

### Sphere3D — Sphere collision

| Function | Signature | Description |
|----------|-----------|-------------|
| `Sphere3D.Overlaps(centerA, radiusA, centerB, radiusB)` | `i1(obj, f64, obj, f64)` | Sphere vs sphere overlap |
| `Sphere3D.Penetration(centerA, radiusA, centerB, radiusB)` | `obj(obj, f64, obj, f64)` | Push-out Vec3 |

### Segment3D — Line segment

| Function | Signature | Description |
|----------|-----------|-------------|
| `Segment3D.ClosestPoint(segA, segB, point)` | `obj(obj, obj, obj)` | Closest point on segment to a point |

### Capsule3D — Capsule collision

| Function | Signature | Description |
|----------|-----------|-------------|
| `Capsule3D.SphereOverlaps(capA, capB, capR, center, radius)` | `i1(obj, obj, f64, obj, f64)` | Capsule vs sphere overlap |
| `Capsule3D.AABBOverlaps(capA, capB, capR, min, max)` | `i1(obj, obj, f64, obj, obj)` | Capsule vs AABB overlap |

Ray/AABB/capsule helpers validate `Vec3`, `Mat4`, mesh, and hit handles. Non-finite rays or
dimensions return a miss or safe zero result. AABB helpers canonicalize inverted min/max bounds,
penetration vectors push the first shape out of the second, and capsule-vs-AABB uses exact
segment-to-box distance rather than only testing against the box center.

### Zia Example

```zia
module RaycastDemo;

bind Viper.Graphics3D.Ray3D;
bind Viper.Graphics3D.RayHit3D;
bind Viper.Graphics3D.AABB3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Math.Vec3;

func start() {
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);
    var origin = Camera3D.get_Position(cam);
    var dir = Camera3D.get_Forward(cam);

    // Ray-sphere test
    var center = Vec3.New(5.0, 0.0, 0.0);
    var dist = Ray3D.IntersectSphere(origin, dir, center, 1.0);

    // Ray-mesh test
    var hit = Ray3D.IntersectMesh(origin, dir, mesh, transform);
    if (hit != null) {
        var point = RayHit3D.get_Point(hit);
        var normal = RayHit3D.get_Normal(hit);
        var tri = RayHit3D.get_TriangleIndex(hit);
    }

    // AABB overlap
    var minA = Vec3.New(-1.0, -1.0, -1.0);
    var maxA = Vec3.New(1.0, 1.0, 1.0);
    var minB = Vec3.New(0.0, 0.0, 0.0);
    var maxB = Vec3.New(2.0, 2.0, 2.0);
    var overlaps = AABB3D.Overlaps(minA, maxA, minB, maxB);
    var pushout = AABB3D.Penetration(minA, maxA, minB, maxB);
}
```

## FPS Camera

First-person camera controller with yaw/pitch mouse look and WASD movement. These methods are on the Camera3D class (documented above in the Camera3D section).

```zia
module FPSDemo;

bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Input.Mouse;
bind Viper.Math.Vec3;

func start() {
    var canvas = Canvas3D.New("FPS", 800, 600);
    var cam = Camera3D.New(70.0, 800.0 / 600.0, 0.1, 200.0);
    Camera3D.LookAt(cam, Vec3.New(0.0, 1.7, 0.0), Vec3.New(0.0, 1.7, -1.0), Vec3.New(0.0, 1.0, 0.0));
    Camera3D.FPSInit(cam);
    Mouse.Capture();

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        var dt = Canvas3D.get_DeltaTime(canvas) / 1000.0;
        var mdx = Mouse.DeltaX() * 0.1;
        var mdy = Mouse.DeltaY() * 0.1;
        Camera3D.FPSUpdate(cam, mdx, -mdy, 0.0, 0.0, 0.0, 5.0, dt);

        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Canvas3D.Begin(canvas, cam);
        // ... draw scene ...
        Canvas3D.End(canvas);
        Canvas3D.DrawCrosshair(canvas, 0xFFFFFF, 12);
        Canvas3D.Flip(canvas);
    }
}
```

- `FPSInit` decomposes the current view matrix to extract yaw/pitch
- `FPSUpdate(mdx, mdy, fwd, right, up, speed, dt)` accumulates yaw/pitch, clamps pitch to +/-89 degrees, applies WASD movement
- `Yaw`/`Pitch` properties allow reading/writing the current angles and rebuild the view immediately
- Use `Mouse.Capture()` to hide cursor and enable warp-to-center mouse tracking

## Sound3D

Spatial audio now has two layers:

- `SoundListener3D` and `SoundSource3D` are the preferred gameplay-facing APIs.
- `Sound3D` remains as the low-level compatibility layer for direct listener/voice control.

### SoundListener3D

An `SoundListener3D` owns the active listener transform used for attenuation and stereo pan. The first listener you create becomes active automatically if no other active listener exists; you can switch the active listener by setting `IsActive = true` on another instance.

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Position` | `Vec3` | read/write | Listener world position |
| `Forward` | `Vec3` | read/write | Listener facing direction |
| `Velocity` | `Vec3` | read/write | Listener velocity |
| `IsActive` | `Boolean` | read/write | Whether this listener is driving `Sound3D` |

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `obj()` | Create a listener object |
| `SetPosition(position)` | `void(obj)` | Set world position from a `Vec3` |
| `SetForward(forward)` | `void(obj)` | Set facing direction from a `Vec3` |
| `SetVelocity(velocity)` | `void(obj)` | Set velocity explicitly |
| `BindNode(node)` | `void(obj)` | Follow a `SceneNode3D` world transform |
| `ClearNodeBinding()` | `void()` | Stop following a node |
| `BindCamera(camera)` | `void(obj)` | Follow a `Camera3D` position and forward vector |
| `ClearCameraBinding()` | `void()` | Stop following a camera |

### SoundSource3D

An `SoundSource3D` owns one spatial sound instance. It caches world-space position and can follow a bound `SceneNode3D`.

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Position` | `Vec3` | read/write | Source world position |
| `Velocity` | `Vec3` | read/write | Source velocity |
| `DopplerFactor` | `Float` | read | Latest computed Doppler pitch multiplier |
| `RefDistance` | `Float` | read/write | Full-volume radius before attenuation begins |
| `MaxDistance` | `Float` | read/write | Distance at which the sound attenuates to silence |
| `Volume` | `Integer` | read/write | Base volume before attenuation `[0,100]` |
| `Looping` | `Boolean` | read/write | Whether `Play()` uses looped voice playback |
| `IsPlaying` | `Boolean` | read | Whether the current voice is still alive |
| `VoiceId` | `Integer` | read | Current low-level voice handle, or `0` when idle |

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(sound)` | `obj(obj)` | Create a source around a `Sound` object |
| `SetPosition(position)` | `void(obj)` | Set world position from a `Vec3` |
| `SetVelocity(velocity)` | `void(obj)` | Set velocity explicitly |
| `Play()` | `i64()` | Start playback and return the active voice handle |
| `Stop()` | `void()` | Stop the active voice and clear `VoiceId` |
| `BindNode(node)` | `void(obj)` | Follow a `SceneNode3D` world transform |
| `ClearNodeBinding()` | `void()` | Stop following a node |

### Binding Sync

Bound audio objects are explicit, just like physics/animation bindings:

- `Sound3D.SyncBindings(dt)` updates all bound listeners and sources, recomputes velocities, and refreshes any live source voices.
- `Scene3D.SyncBindings(dt)` calls `Sound3D.SyncBindings(dt)` after scene/body/anim synchronization, so most scene-driven games only need the scene call.
- Property setters such as `source.Position = ...` update the cached spatial state immediately even when no scene binding is involved.
- `BindNode` accepts only `SceneNode3D`, `BindCamera` accepts only `Camera3D`, and non-Vec3 position/velocity/forward values are ignored. Null vectors still collapse to origin for compatibility.
- `SoundSource3D.MaxDistance` is finite and non-negative; invalid values become `0.0`.
- `SoundSource3D.RefDistance` defaults to `1.0`; `MaxDistance` is raised when needed so it is never smaller than `RefDistance`.

Recommended frame order for scene-driven audio:

1. Move cameras and scene nodes.
2. Call `Scene3D.SyncBindings(dt)`.
3. Trigger `SoundSource3D.Play()` or `Sound3D.PlayAt(...)` calls for the frame.

### Sound3D Compatibility Layer

`Sound3D` is still available when you want direct listener/voice control without allocating objects.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sound3D.SetListener(position, forward)` | `void(obj, obj)` | Set the fallback listener position and forward vector |
| `Sound3D.PlayAt(sound, position, maxDist, volume)` | `i64(obj, obj, f64, i64)` | Play a `Sound` at a world position |
| `Sound3D.UpdateVoice(voice, position, maxDist)` | `void(i64, obj, f64)` | Recompute attenuation and pan for a moving voice |
| `Sound3D.SyncBindings(dt)` | `void(f64)` | Update all bound `SoundListener3D` / `SoundSource3D` objects |

### Zia Example

```zia
module Sound3DObjectsDemo;

bind Viper.Graphics3D;
bind Viper.Math;
bind Viper.Sound;

func start() {
    var cam = Camera3D.New(60.0, 1.0, 0.1, 100.0);
    Camera3D.LookAt(
        cam, Vec3.New(0.0, 2.0, 6.0), Vec3.New(0.0, 1.5, 0.0), Vec3.New(0.0, 1.0, 0.0));

    var node = SceneNode3D.New();
    SceneNode3D.SetPosition(node, 3.0, 0.5, -1.0);

    var listener = SoundListener3D.New();
    listener.BindCamera(cam);
    listener.IsActive = true;

    var source = SoundSource3D.New(Synth.Tone(523, 220, 0));
    source.BindNode(node);
    source.RefDistance = 2.0;
    source.MaxDistance = 20.0;
    source.Volume = 75;

    Sound3D.SyncBindings(0.016);

    if (Audio.IsAvailable() && Audio.Init() != 0) {
        var voice = source.Play();
        source.Stop();
    }
}
```

- Linear distance attenuation stays at full volume through `refDist`, then falls to zero at `maxDist`
- Pan is derived from the listener's right vector and the source direction in world space
- `Sound3D.PlayAt` still records per-voice `max_distance`, and `UpdateVoice(..., 0.0)` reuses that stored value
- `SoundSource3D.DopplerFactor` exposes the latest factor computed from listener/source velocity; the current mixer applies volume and pan, with playback-rate application reserved for rate-capable backends

## Mouse Capture

For FPS-style games, capture the mouse to prevent it from leaving the window:

```zia
Mouse.Capture()   // hides cursor + warps to center each frame
Mouse.Release()   // restores cursor
```

When captured, `Mouse.DeltaX()`/`Mouse.DeltaY()` report movement from center. The cursor is hidden and warped to the window center each `Canvas3D.Poll()` call. Only active when the window has focus.

---

## Physics3D

Impulse-based 3D rigid body simulation with box, sphere, and capsule collision shapes.
Bodies now track quaternion orientation and angular velocity in addition to linear motion.
Shape-specific narrow-phase collision: sphere-sphere uses radial distance (not AABB),
box-sphere uses closest-point projection in the box's oriented local space. Collision detection uses a sweep-and-prune broadphase
before narrow-phase tests. Non-trigger contacts apply impulses at the contact point, so off-center
hits update angular velocity as well as linear velocity. Coulomb friction and Baumgarte positional
correction are applied to non-trigger contacts.

Capsule primitive collision honors body orientation for capsule-vs-capsule, capsule-vs-sphere,
capsule-vs-box, mesh, and heightfield tests. Box primitives honor body and compound-child
orientation; the `NewAABB` name is kept as a compatibility factory for box bodies.
`Raycast` tests actual collider geometry for boxes, spheres, capsules, compound leaves, mesh/convex
triangles, and heightfields. Sphere and capsule sweeps use adaptive sampling so small-radius sweeps
and long capsules can hit thin geometry.

### Physics3DWorld

World storage for bodies, contacts, contact events, and joints grows on demand from production-sized initial capacities. Query result lists are still bounded for predictable allocation behavior.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(gx, gy, gz)` | `obj(f64, f64, f64)` | Create world with gravity vector |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `BodyCount` | Integer | read | Number of active bodies |
| `CollisionCount` | Integer | read | Number of contacts from last Step |
| `CollisionEventCount` | Integer | read | Number of current collision events from last Step |
| `EnterEventCount` | Integer | read | Number of collision pairs that began touching this step |
| `StayEventCount` | Integer | read | Number of collision pairs still touching this step |
| `ExitEventCount` | Integer | read | Number of collision pairs that stopped touching this step |
| `JointCount` | Integer | read | Number of active joints |

| Method | Signature | Description |
|--------|-----------|-------------|
| `Step(dt)` | `void(f64)` | Advance simulation by dt seconds |
| `Add(body)` | `void(obj)` | Add body to world |
| `Remove(body)` | `void(obj)` | Remove body from world |
| `SetGravity(x, y, z)` | `void(f64, f64, f64)` | Update gravity |
| `AddJoint(joint, type)` | `void(obj, i64)` | Add joint (type: 0=distance, 1=spring) |
| `RemoveJoint(joint)` | `void(obj)` | Remove joint |
| `Raycast(origin, direction, maxDistance, mask)` | `obj(obj, obj, f64, i64)` | Return the nearest `PhysicsHit3D` or `none` |
| `RaycastAll(origin, direction, maxDistance, mask)` | `obj(obj, obj, f64, i64)` | Return a sorted `PhysicsHitList3D` or `none` |
| `SweepSphere(center, radius, delta, mask)` | `obj(obj, f64, obj, i64)` | Sweep a sphere and return the first `PhysicsHit3D` or `none` |
| `SweepCapsule(a, b, radius, delta, mask)` | `obj(obj, obj, f64, obj, i64)` | Sweep a capsule segment and return the first `PhysicsHit3D` or `none` |
| `OverlapSphere(center, radius, mask)` | `obj(obj, f64, i64)` | Return a `PhysicsHitList3D` of overlaps or `none` |
| `OverlapAABB(min, max, mask)` | `obj(obj, obj, i64)` | Return a `PhysicsHitList3D` of overlaps or `none` |
| `GetCollisionBodyA(index)` | `obj(i64)` | Get first body in contact pair |
| `GetCollisionBodyB(index)` | `obj(i64)` | Get second body in contact pair |
| `GetCollisionNormal(index)` | `obj(i64)` | Get contact normal Vec3 (A->B) |
| `GetCollisionDepth(index)` | `f64(i64)` | Get penetration depth |
| `GetCollisionEvent(index)` | `obj(i64)` | Get current `CollisionEvent3D` |
| `GetEnterEvent(index)` | `obj(i64)` | Get `CollisionEvent3D` from the enter bucket |
| `GetStayEvent(index)` | `obj(i64)` | Get `CollisionEvent3D` from the stay bucket |
| `GetExitEvent(index)` | `obj(i64)` | Get `CollisionEvent3D` from the exit bucket |

Notes:
- Query `mask` uses the same layer bit semantics as body collision layers. `0` means "match any layer".
- Queries include trigger bodies and mark them through `PhysicsHit3D.IsTrigger`.
- `Raycast` and `RaycastAll` are true ray queries for sphere, capsule, and box colliders; complex collider types fall back to their world AABB. Use `SweepSphere` or `SweepCapsule` for volume casts.
- `GetContactSeparation()` returns negative values while penetrating and positive values when separated.

### PhysicsHit3D

`PhysicsHit3D` is the result object returned by `Raycast`, `SweepSphere`, and `SweepCapsule`.

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Body` | Object | read | Hit `Physics3DBody` |
| `Collider` | Object | read | Hit `Collider3D` leaf collider |
| `Point` | Vec3 | read | Contact point approximation |
| `Normal` | Vec3 | read | Surface normal at the hit |
| `Distance` | Float | read | World-space distance travelled before the hit |
| `Fraction` | Float | read | `Distance / maxDistance` for sweeps and raycasts |
| `StartedPenetrating` | Boolean | read | Query began already overlapping the target |
| `IsTrigger` | Boolean | read | Hit body is trigger-only |

### PhysicsHitList3D

`PhysicsHitList3D` is returned by `RaycastAll`, `OverlapSphere`, and `OverlapAABB`.

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Count` | Integer | read | Number of hits in the list |

| Method | Signature | Description |
|--------|-----------|-------------|
| `Get(index)` | `obj(i64)` | Return hit `index` as a `PhysicsHit3D` |

### CollisionEvent3D

`CollisionEvent3D` is the structured per-pair contact event produced by `Physics3DWorld.Step()`.

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `BodyA` | Object | read | First body in the pair |
| `BodyB` | Object | read | Second body in the pair |
| `ColliderA` | Object | read | Leaf collider for body A |
| `ColliderB` | Object | read | Leaf collider for body B |
| `IsTrigger` | Boolean | read | Pair includes at least one trigger body |
| `ContactCount` | Integer | read | Number of contact points in the event (`1` in the current backend) |
| `RelativeSpeed` | Float | read | Relative speed along the contact normal before resolution |
| `NormalImpulse` | Float | read | Solver normal impulse from the last step (`0` for trigger pairs) |

| Method | Signature | Description |
|--------|-----------|-------------|
| `GetContact(index)` | `obj(i64)` | Return `ContactPoint3D` for the manifold point |
| `GetContactPoint(index)` | `obj(i64)` | Return manifold point position as `Vec3` |
| `GetContactNormal(index)` | `obj(i64)` | Return manifold point normal as `Vec3` |
| `GetContactSeparation(index)` | `f64(i64)` | Return signed separation (`< 0` means penetration) |

### ContactPoint3D

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Point` | Vec3 | read | Contact point position |
| `Normal` | Vec3 | read | Contact normal |
| `Separation` | Float | read | Signed separation (`< 0` while penetrating) |

---

### Collider3D

`Collider3D` is the reusable shape object for 3D physics. Prefer authoring colliders first and
then attaching them to `Physics3DBody`; the old body shape constructors remain as convenience
wrappers for simple cases.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `NewBox(hx, hy, hz)` | `obj(f64, f64, f64)` | Box collider with half-extents |
| `NewSphere(radius)` | `obj(f64)` | Sphere collider |
| `NewCapsule(radius, height)` | `obj(f64, f64)` | Capsule collider authored along local Y; `height` is total height including caps, and values below `2*radius` collapse to a sphere-like capsule |
| `NewConvexHull(mesh)` | `obj(obj)` | Convex-hull collider sourced from a `Mesh3D` |
| `NewMesh(mesh)` | `obj(obj)` | Static triangle-mesh collider |
| `NewHeightfield(heightmap, sx, sy, sz)` | `obj(obj, f64, f64, f64)` | Static heightfield collider from `Pixels` |
| `NewCompound()` | `obj()` | Empty compound collider for child composition |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Type` | Integer | read | Collider kind: `0=box`, `1=sphere`, `2=capsule`, `3=convexHull`, `4=mesh`, `5=compound`, `6=heightfield` |

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddChild(child, localTransform)` | `void(obj, obj)` | Add a child collider to a compound collider |
| `GetLocalBoundsMin()` | `obj()` | Local-space AABB minimum as `Vec3` |
| `GetLocalBoundsMax()` | `obj()` | Local-space AABB maximum as `Vec3` |

Notes:
- `NewMesh` and `NewHeightfield` are static-only in v1. Attach them only to static bodies.
- `NewConvexHull` currently expects convex source geometry and uses the mesh surface as the hull.
- Compound colliders are the preferred way to build complex dynamic bodies from simple children.

---

### Physics3DBody

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(mass)` | `obj(f64)` | Create an empty body and assign a collider later |
| `NewAABB(sx, sy, sz, mass)` | `obj(f64, f64, f64, f64)` | Box body (mass=0 for static); name retained for compatibility |
| `NewSphere(radius, mass)` | `obj(f64, f64)` | Sphere body |
| `NewCapsule(radius, height, mass)` | `obj(f64, f64, f64)` | Capsule body; `height` is total height including caps |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Collider` | Object | read/write | Active `Collider3D` shape for the body |
| `Position` | Vec3 | read | World position (set via `SetPosition`) |
| `Orientation` | Quat | read | World orientation (set via `SetOrientation`) |
| `Velocity` | Vec3 | read | Linear velocity (set via `SetVelocity`) |
| `AngularVelocity` | Vec3 | read | Angular velocity in radians/sec (set via `SetAngularVelocity`) |
| `Restitution` | Float | read/write | Bounciness, clamped to `0..1` |
| `Friction` | Float | read/write | Surface friction, clamped to finite non-negative values |
| `LinearDamping` | Float | read/write | Velocity damping per second |
| `AngularDamping` | Float | read/write | Spin damping per second |
| `CollisionLayer` | Integer | read/write | Bitmask layer |
| `CollisionMask` | Integer | read/write | Bitmask for which layers to collide with |
| `Static` | Boolean | read/write | Immovable body (mass-independent) |
| `Kinematic` | Boolean | read/write | Infinite-mass body driven by explicit linear/angular velocity |
| `Trigger` | Boolean | read/write | Overlap detection only, no physics response |
| `CanSleep` | Boolean | read/write | Allow automatic sleep when idle |
| `Sleeping` | Boolean | read | Body is asleep and skipped by dynamic integration |
| `UseCCD` | Boolean | read/write | Enable substep-based CCD for fast motion |
| `Grounded` | Boolean | read | Touching ground surface |
| `GroundNormal` | Vec3 | read | Surface normal of ground contact |
| `Mass` | Float | read | Body mass |

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetCollider(collider)` | `void(obj)` | Attach or replace the active `Collider3D` |
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Teleport body |
| `SetOrientation(quat)` | `void(obj)` | Set body orientation from a Quat |
| `SetVelocity(vx, vy, vz)` | `void(f64, f64, f64)` | Set linear velocity |
| `SetAngularVelocity(wx, wy, wz)` | `void(f64, f64, f64)` | Set angular velocity |
| `ApplyForce(fx, fy, fz)` | `void(f64, f64, f64)` | Accumulate force (applied per step) |
| `ApplyImpulse(ix, iy, iz)` | `void(f64, f64, f64)` | Instant velocity change |
| `ApplyTorque(tx, ty, tz)` | `void(f64, f64, f64)` | Accumulate torque (applied per step) |
| `ApplyAngularImpulse(ix, iy, iz)` | `void(f64, f64, f64)` | Instant angular velocity change |
| `Wake()` | `void()` | Wake a sleeping dynamic body |
| `Sleep()` | `void()` | Force a dynamic body into the sleeping state |

`NewAABB`, `NewSphere`, and `NewCapsule` now allocate a body, create the matching collider, and
attach it internally. Use `New(mass)` plus `SetCollider()` when you want reusable or advanced
shapes.

For a small headless example of the new rotation surface, see
`examples/apiaudit/graphics3d/physics3d_rotation_demo.zia`.
For the collider split and advanced-shape surface, see
`examples/apiaudit/graphics3d/collider3d_advanced_demo.zia`.
For world-space queries, see
`examples/apiaudit/graphics3d/physics3d_queries_demo.zia`.
For structured collision events, see
`examples/apiaudit/graphics3d/collisionevent3d_demo.zia`.

---

### Character3D

Controller-based character movement with slide-and-step collision response.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(radius, height, mass)` | `obj(f64, f64, f64)` | Create character controller |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `StepHeight` | Float | read/write | Max step-up height |
| `Grounded` | Boolean | read | On ground |
| `JustLanded` | Boolean | read | Landed this frame |
| `Position` | Vec3 | read | Current position |

| Method | Signature | Description |
|--------|-----------|-------------|
| `Move(direction, dt)` | `void(obj, f64)` | Move with collision response (Vec3 direction) |
| `SetSlopeLimit(degrees)` | `void(f64)` | Max climbable slope angle |
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Teleport |

---

### Trigger3D

AABB zone for enter/exit detection.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(x0, y0, z0, x1, y1, z1)` | `obj(f64 x6)` | Create AABB trigger zone |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `EnterCount` | Integer | read | Bodies that entered since last check |
| `ExitCount` | Integer | read | Bodies that exited since last check |

| Method | Signature | Description |
|--------|-----------|-------------|
| `Contains(position)` | `i1(obj)` | Point-in-zone test (Vec3) |
| `Update(body)` | `void(obj)` | Check body against zone (call per frame per body) |
| `SetBounds(x0, y0, z0, x1, y1, z1)` | `void(f64 x6)` | Update zone bounds |

---

### DistanceJoint3D

Maintains a fixed distance between two body centers.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(bodyA, bodyB, distance)` | `obj(obj, obj, f64)` | Create distance joint |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Distance` | Float | read/write | Target distance |

Distance joints retain both body handles for the joint lifetime. Negative or non-finite target
distances are sanitized to zero, and the solver skips bodies with non-finite motion state.

---

### SpringJoint3D

Hooke's law spring with configurable stiffness and damping.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(bodyA, bodyB, restLen, stiffness, damping)` | `obj(obj, obj, f64, f64, f64)` | Create spring joint |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Stiffness` | Float | read/write | Spring constant |
| `Damping` | Float | read/write | Velocity damping factor |
| `RestLength` | Float | read | Equilibrium distance |

Spring joints retain both body handles for the joint lifetime. Rest length, stiffness, and damping
are non-negative finite values; invalid inputs become zero and very large values are clamped to keep
solver impulses finite.

### Zia Example

```zia
module PhysicsDemo;

bind Viper.Graphics3D.Physics3DWorld;
bind Viper.Graphics3D.Physics3DBody;
bind Viper.Graphics3D.Character3D;
bind Viper.Graphics3D.Trigger3D;
bind Viper.Graphics3D.DistanceJoint3D;
bind Viper.Math.Vec3;

func start() {
    // Create physics world with gravity
    var world = Physics3DWorld.New(0.0, -9.8, 0.0);

    // Static ground
    var ground = Physics3DBody.NewAABB(100.0, 1.0, 100.0, 0.0);
    Physics3DBody.SetPosition(ground, 0.0, -0.5, 0.0);
    Physics3DBody.set_Static(ground, true);
    Physics3DWorld.Add(world, ground);

    // Dynamic sphere
    var ball = Physics3DBody.NewSphere(0.5, 1.0);
    Physics3DBody.SetPosition(ball, 0.0, 10.0, 0.0);
    Physics3DBody.set_Restitution(ball, 0.8);
    Physics3DWorld.Add(world, ball);

    // Distance joint between two bodies
    var anchor = Physics3DBody.NewSphere(0.2, 0.0);
    Physics3DBody.SetPosition(anchor, 0.0, 15.0, 0.0);
    Physics3DBody.set_Static(anchor, true);
    Physics3DWorld.Add(world, anchor);
    var joint = DistanceJoint3D.New(anchor, ball, 5.0);
    Physics3DWorld.AddJoint(world, joint, 0);

    // Character controller
    var player = Character3D.New(0.4, 1.8, 80.0);
    Character3D.SetPosition(player, 5.0, 1.0, 0.0);

    // Trigger zone
    var zone = Trigger3D.New(-2.0, 0.0, -2.0, 2.0, 3.0, 2.0);

    // Simulation loop
    Physics3DWorld.Step(world, 0.016);

    // Check collisions
    var n = Physics3DWorld.get_CollisionCount(world);
    for i in 0..n {
        var bodyA = Physics3DWorld.GetCollisionBodyA(world, i);
        var normal = Physics3DWorld.GetCollisionNormal(world, i);
    }

    // Check trigger
    Trigger3D.Update(zone, ball);
    var entered = Trigger3D.get_EnterCount(zone);
}
```

---

## Transform3D

Standalone 3D transform (position, rotation, scale) with lazy matrix computation.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create identity transform |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Position` | Vec3 | read | Current position |
| `Rotation` | Quat | read/write | Current rotation |
| `Scale` | Vec3 | read | Current scale |
| `Matrix` | Mat4 | read | Computed world matrix (lazy) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Set position |
| `SetEuler(pitch, yaw, roll)` | `void(f64, f64, f64)` | Set rotation from Euler angles (degrees) |
| `SetScale(sx, sy, sz)` | `void(f64, f64, f64)` | Set scale |
| `Translate(delta)` | `void(obj)` | Move relative (Vec3) |
| `Rotate(axis, angle)` | `void(obj, f64)` | Rotate around axis (Vec3, angle in radians) |
| `LookAt(target, up)` | `void(obj, obj)` | Orient toward target (Vec3s) |

### Zia Example

```zia
module TransformDemo;

bind Viper.Graphics3D.Transform3D;
bind Viper.Math.Vec3;
bind Viper.Math.Quat;

func start() {
    var xform = Transform3D.New();
    Transform3D.SetPosition(xform, 5.0, 0.0, 3.0);
    Transform3D.SetEuler(xform, 0.0, 45.0, 0.0);
    Transform3D.SetScale(xform, 2.0, 2.0, 2.0);

    // Incremental movement
    Transform3D.Translate(xform, Vec3.New(1.0, 0.0, 0.0));
    Transform3D.Rotate(xform, Vec3.New(0.0, 1.0, 0.0), 0.5);

    // Look at a target
    Transform3D.LookAt(xform, Vec3.New(0.0, 0.0, 0.0), Vec3.New(0.0, 1.0, 0.0));

    // Get computed matrix for rendering
    var mat = Transform3D.get_Matrix(xform);
}
```

---

## Sprite3D

Camera-facing billboard sprite in 3D space. Useful for particles, foliage, and NPC labels.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(texture)` | `obj(obj)` | Create billboard from Pixels texture |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Set world position |
| `SetScale(width, height)` | `void(f64, f64)` | Billboard size in world units |
| `SetAnchor(ax, ay)` | `void(f64, f64)` | Pivot point (0-1, default 0.5/0.5 = center) |
| `SetFrame(x, y, w, h)` | `void(i64, i64, i64, i64)` | Sprite sheet sub-rectangle (pixels) |

Draw via `Canvas3D.DrawSprite3D(sprite, camera)`. Mesh and material are cached internally.

### Zia Example

```zia
module Sprite3DDemo;

bind Viper.Graphics3D.Sprite3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.IO.Pixels;

func start() {
    var canvas = Canvas3D.New("Sprite3D", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var tex = Pixels.Load("tree_billboard.png");
    var tree = Sprite3D.New(tex);
    Sprite3D.SetPosition(tree, 5.0, 1.0, -3.0);
    Sprite3D.SetScale(tree, 2.0, 3.0);
    Sprite3D.SetAnchor(tree, 0.5, 0.0); // bottom-center

    // In render loop:
    Canvas3D.Begin(canvas, cam);
    Canvas3D.DrawSprite3D(canvas, tree, cam);
    Canvas3D.End(canvas);
}
```

---

## Decal3D

Surface-projected quad for bullet holes, blood splatters, footprints.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(position, normal, size, material)` | `obj(obj, obj, f64, obj)` | Create decal aligned to surface (Vec3 pos, Vec3 normal, Float size, Material3D) |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Expired` | Boolean | read | True when lifetime reached |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetLifetime(seconds)` | `void(f64)` | Set auto-fade duration |
| `Update(dt)` | `void(f64)` | Advance lifetime |

Draw via `Canvas3D.DrawDecal(decal)`.

### Zia Example

```zia
module DecalDemo;

bind Viper.Graphics3D.Decal3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Math.Vec3;

func start() {
    var canvas = Canvas3D.New("Decals", 800, 600);

    var bullet_mat = Material3D.NewTextured(bulletHolePixels);
    Material3D.set_Alpha(bullet_mat, 0.9);

    var decal = Decal3D.New(
        Vec3.New(2.0, 1.5, 0.01),  // position on wall
        Vec3.New(0.0, 0.0, 1.0),   // wall normal
        0.3,                         // size
        bullet_mat);
    Decal3D.SetLifetime(decal, 10.0);

    // In render loop:
    Decal3D.Update(decal, dt);
    if (Decal3D.get_Expired(decal) == false) {
        Canvas3D.DrawDecal(canvas, decal);
    }
}
```

---

## Water3D

Animated water surface with Gerstner wave simulation, texture support, and environment reflections.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(width, depth)` | `obj(f64, f64)` | Create water plane |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetHeight(y)` | `void(f64)` | Set world Y position |
| `SetWaveParams(speed, amplitude, frequency)` | `void(f64, f64, f64)` | Legacy single-wave parameters |
| `SetColor(r, g, b, a)` | `void(f64, f64, f64, f64)` | Water tint (0-1 float RGBA) |
| `SetTexture(pixels)` | `void(obj)` | Surface texture (Pixels) |
| `SetNormalMap(pixels)` | `void(obj)` | Wave normal map (Pixels) |
| `SetEnvMap(cubemap)` | `void(obj)` | Environment CubeMap3D for reflections |
| `SetReflectivity(r)` | `void(f64)` | Reflection strength [0.0-1.0] |
| `SetResolution(n)` | `void(i64)` | Grid resolution (8-256, default 64) |
| `AddWave(dirX, dirZ, speed, amplitude, wavelength)` | `void(f64, f64, f64, f64, f64)` | Add directional Gerstner wave (max 8) |
| `ClearWaves()` | `void()` | Remove all Gerstner waves |
| `Update(dt)` | `void(f64)` | Advance wave animation |

### Zia Example

```zia
module WaterDemo;

bind Viper.Graphics3D.Water3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.CubeMap3D;

func start() {
    var canvas = Canvas3D.New("Water", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 200.0);

    var water = Water3D.New(100.0, 100.0);
    Water3D.SetHeight(water, 0.0);
    Water3D.SetColor(water, 0.0, 0.3, 0.5, 0.7);
    Water3D.SetResolution(water, 128);

    // Gerstner waves for realistic ocean
    Water3D.AddWave(water, 1.0, 0.0, 2.0, 0.3, 10.0);
    Water3D.AddWave(water, 0.7, 0.7, 1.5, 0.15, 7.0);
    Water3D.AddWave(water, -0.3, 1.0, 3.0, 0.1, 15.0);

    // Environment reflections
    Water3D.SetEnvMap(water, skyboxCubemap);
    Water3D.SetReflectivity(water, 0.6);

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        var dt = Canvas3D.get_DeltaTime(canvas) / 1000.0;
        Water3D.Update(water, dt);

        Canvas3D.Clear(canvas, 0.5, 0.7, 0.9);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawWater(canvas, water, cam);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

**Gerstner waves:** When waves are added via `AddWave`, the water uses a sum of directional Gerstner waves instead of the legacy single sine wave. Each wave has a direction, speed, amplitude, and wavelength. Up to 8 waves can be combined for realistic ocean effects. Normals are computed from wave derivatives for correct lighting.

`Water3D` clamps extreme sizes, heights, wave speeds, amplitudes, frequencies, and wavelengths before mesh generation so renderer-facing vertices and normals stay finite. If a generated mesh fails validation, the water surface clears the partial mesh and remains dirty so the next valid update can rebuild it.

`Update(0.0)` is valid: it rebuilds the mesh when resolution or wave settings are dirty without advancing animation time. `DrawWater` also performs that zero-delta rebuild if a surface has not been built yet or was invalidated by `SetResolution`.

Draw via `Canvas3D.DrawWater(water, camera)`.

See `examples/apiaudit/graphics3d/water_demo.zia` for a complete example.

---

## Terrain3D

Heightmap-based terrain with chunked rendering, LOD, and texture splatting.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(widthSegments, depthSegments)` | `obj(i64, i64)` | Create terrain grid |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetHeightmap(pixels)` | `void(obj)` | Load height from R+G channels of Pixels (16-bit) |
| `GeneratePerlin(noise, scale, octaves, persistence)` | `void(obj, f64, i64, f64)` | Generate heights from PerlinNoise (native fast path) |
| `SetMaterial(material)` | `void(obj)` | Set surface material |
| `SetScale(sx, sy, sz)` | `void(f64, f64, f64)` | Set terrain world size (Y = height scale) |
| `SetSplatMap(pixels)` | `void(obj)` | Set splat map (RGBA: weights for layers 0-3) |
| `SetLayerTexture(layer, pixels)` | `void(i64, obj)` | Set texture for splat layer (0-3) |
| `SetLayerScale(layer, scale)` | `void(i64, f64)` | Set UV tiling scale per layer |
| `GetHeightAt(x, z)` | `f64(f64, f64)` | Query height at world XZ position |
| `GetNormalAt(x, z)` | `obj(f64, f64)` | Query surface normal at world XZ (Vec3) |
| `SetLODDistances(near, far)` | `void(f64, f64)` | Set LOD transition distances (default 100/250) |
| `SetSkirtDepth(depth)` | `void(f64)` | Set skirt depth to hide LOD cracks |

### Zia Example

```zia
module TerrainDemo;

bind Viper.Graphics3D.Terrain3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Math.PerlinNoise;

func start() {
    var canvas = Canvas3D.New("Terrain", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 500.0);

    var terrain = Terrain3D.New(128, 128);
    Terrain3D.SetScale(terrain, 200.0, 30.0, 200.0);

    // Generate heights from Perlin noise
    var noise = PerlinNoise.New(42);
    Terrain3D.GeneratePerlin(terrain, noise, 0.02, 6, 0.5);

    // Material and splatting
    Terrain3D.SetMaterial(terrain, Material3D.NewColor(0.3, 0.5, 0.2));
    Terrain3D.SetLayerTexture(terrain, 0, grassPixels);
    Terrain3D.SetLayerTexture(terrain, 1, rockPixels);
    Terrain3D.SetLayerScale(terrain, 0, 10.0);
    Terrain3D.SetLayerScale(terrain, 1, 5.0);
    Terrain3D.SetSplatMap(terrain, splatPixels);

    // LOD settings
    Terrain3D.SetLODDistances(terrain, 80.0, 200.0);
    Terrain3D.SetSkirtDepth(terrain, 2.0);

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        Canvas3D.Clear(canvas, 0.5, 0.7, 0.9);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawTerrain(canvas, terrain);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

**Procedural generation:** Two approaches are supported:
1. **Zia-only:** Use `PerlinNoise.Octave2D()` to fill a `Pixels` buffer, then call `SetHeightmap()`. The heightmap uses 16-bit precision via R (high byte) + G (low byte) channels in `0xRRGGBBAA` pixel format.
2. **Native fast path:** Call `GeneratePerlin(noise, scale, octaves, persistence)` with a `PerlinNoise` object. This writes directly to the internal float heightmap, bypassing the Pixels intermediate for better performance on large terrains. The `noise` parameter is a `PerlinNoise` object, `scale` controls coordinate frequency, `octaves` sets detail layers (typically 4-8), and `persistence` controls amplitude decay (typically 0.4-0.6). Non-finite scale/persistence values are sanitized, octaves are clamped to `1..16`, and generated heights are clamped to `0..1`.

**Texture splatting:** When a splat map is set, the terrain blends 4 layer textures per-pixel during rasterization, weighted by the splat map RGBA channels. Each layer can have its own UV tiling scale for detail repetition. The software, Metal, OpenGL, and D3D11 backends all perform per-pixel splat sampling. Backend splatting is enabled only when the control map and all four layer textures are present; incomplete splat sets render with the base material/fallback texture instead of sampling missing layers. A `1x1` splat map is valid and acts as uniform coverage for the whole terrain. Any baked fallback texture is stored in the standard `Pixels` format, `0xRRGGBBAA`.

**LOD (Level of Detail):** Terrain chunks use 3 resolution levels based on distance from the camera:
- LOD 0 (full): 16x16 quads per chunk (nearest chunks)
- LOD 1 (half): 8x8 quads per chunk (mid-range)
- LOD 2 (quarter): 4x4 quads per chunk (distant)

Configure with `SetLODDistances(nearDist, farDist)` — chunks closer than `nearDist` use LOD 0, between `nearDist` and `farDist` use LOD 1, beyond `farDist` use LOD 2. Default: 100/250. Invalid distances are sanitized so `farDist` stays greater than `nearDist`. Chunks outside the camera frustum are culled entirely (not drawn). Skirt geometry (`SetSkirtDepth(depth)`) hides cracks at LOD transitions by extending chunk edges downward and is included in chunk bounds, so visible skirts are not clipped by frustum culling. Invalid or negative skirt depths disable skirts. Edge chunks always include their far row/column endpoints at coarser LODs, so partial edge chunks still produce triangles.

Draw via `Canvas3D.DrawTerrain(terrain)` during a normal 3D `Begin`/`End` pass. Terrain is not valid inside `Begin2D()`.

See `examples/apiaudit/graphics3d/procedural_terrain_demo.zia` and `terrain_lod_demo.zia` for complete examples.

---

## InstanceBatch3D

Draw many copies of one mesh with different transforms in a single draw call.
Transforms passed to `Add` and `Set` are copied into finite float matrices; any non-finite element is
replaced with the corresponding identity-matrix value before culling or backend submission.
GPU instanced draws synthesize previous-instance matrices when the caller does not provide them, so
motion-vector consumers get stable no-streak first frames. Raw instanced submissions separate motion
history by batch buffer identity; reuse the same matrix buffer across frames for continuous history.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(mesh, material)` | `obj(obj, obj)` | Create batch for a Mesh3D+Material3D pair |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Count` | Integer | read | Number of instances |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(transform)` | `void(obj)` | Add instance with Mat4 transform |
| `Remove(index)` | `void(i64)` | Remove instance by index |
| `Set(index, transform)` | `void(i64, obj)` | Update instance Mat4 transform |
| `Clear()` | `void()` | Remove all instances |

Draw via `Canvas3D.DrawInstanced(batch)`.

Instanced motion-history keys use stable mesh/material/count/index identity rather than the
transient transform-buffer address, so reallocating an instance buffer does not reset motion vectors.

### Zia Example

```zia
module InstanceDemo;

bind Viper.Graphics3D.InstanceBatch3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Graphics3D.Material3D;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("Instancing", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    var tree = Mesh3D.NewCylinder(0.2, 2.0, 6);
    var mat = Material3D.NewColor(0.3, 0.5, 0.1);
    var batch = InstanceBatch3D.New(tree, mat);

    // Place 100 trees
    for i in 0..100 {
        var x = (i % 10) * 3.0;
        var z = (i / 10) * 3.0;
        InstanceBatch3D.Add(batch, Mat4.Translate(x, 0.0, z));
    }

    // Render loop
    Canvas3D.Begin(canvas, cam);
    Canvas3D.DrawInstanced(canvas, batch);
    Canvas3D.End(canvas);
}
```

---

## NavMesh3D

Navigation mesh with A* pathfinding for AI characters. `Build` requires manifold shared edges:
more than two triangles on one undirected edge is rejected because adjacency would be ambiguous.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `Build(mesh, agentRadius, agentHeight)` | `obj(obj, f64, f64)` | Build navmesh from Mesh3D geometry |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `TriangleCount` | Integer | read | Number of triangles in navmesh |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `FindPath(start, goal)` | `obj(obj, obj)` | A* pathfinding (Vec3 start/goal, returns waypoint list) |
| `SamplePosition(position)` | `obj(obj)` | Snap position to nearest point on navmesh (Vec3) |
| `IsWalkable(position)` | `i1(obj)` | Check if Vec3 position is on the navmesh |
| `SetMaxSlope(degrees)` | `void(f64)` | Update walkability slope threshold |
| `DebugDraw(canvas)` | `void(obj)` | Visualize navmesh wireframe on Canvas3D |

`Build()` stores the source walkable geometry separately from the filtered navigation triangles. `SetMaxSlope()` therefore immediately refilters the existing mesh and rebuilds adjacency instead of requiring a full rebuild. Slope tests use upward-facing triangle planes. `SamplePosition()` projects to the closest point on the nearest walkable triangle instead of snapping to a centroid, while `FindPath()` and `IsWalkable()` require the query height to be near the triangle plane so stacked floors or points far above the mesh do not alias to the wrong layer.

### Zia Example

```zia
module NavMeshDemo;

bind Viper.Graphics3D.NavMesh3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Math.Vec3;

func start() {
    var level_mesh = Mesh3D.FromOBJ("level.obj");
    var nav = NavMesh3D.Build(level_mesh, 0.4, 1.8);
    NavMesh3D.SetMaxSlope(nav, 45.0);

    var start = Vec3.New(0.0, 0.0, 0.0);
    var goal = Vec3.New(20.0, 0.0, 15.0);
    var path = NavMesh3D.FindPath(nav, start, goal);

    // Snap a position to the navmesh
    var snapped = NavMesh3D.SamplePosition(nav, Vec3.New(5.0, 2.0, 5.0));

    // Check walkability
    var ok = NavMesh3D.IsWalkable(nav, Vec3.New(10.0, 0.0, 10.0));
}
```

---

## NavAgent3D

Goal-driven navigation agent built on top of `NavMesh3D`. `NavAgent3D` owns a target, keeps an internal waypoint path, exposes steering state, and can either move a bound `Character3D` or directly reposition a bound `SceneNode3D`.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(navmesh, radius, height)` | `obj(obj, f64, f64)` | Create a navigation agent for a specific `NavMesh3D` |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Position` | `Vec3` | read | Current world-space agent position |
| `Velocity` | `Vec3` | read | Actual motion from the last update |
| `DesiredVelocity` | `Vec3` | read | Steering velocity requested by the path follower |
| `HasPath` | `Boolean` | read | Whether the agent currently has an active route |
| `RemainingDistance` | `Float` | read | Remaining linear distance along the current route |
| `StoppingDistance` | `Float` | read/write | Arrival radius around the final target |
| `DesiredSpeed` | `Float` | read/write | Preferred movement speed in world units per second |
| `AutoRepath` | `Boolean` | read/write | Periodically rebuild the path while a target is active |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetTarget(position)` | `void(obj)` | Set a new goal position and rebuild the path immediately |
| `ClearTarget()` | `void()` | Drop the current goal and clear steering state |
| `Update(dt)` | `void(f64)` | Advance path following and push motion into the current bindings |
| `Warp(position)` | `void(obj)` | Teleport the agent, clear stale motion, and rebuild if a target still exists |
| `BindCharacter(controller)` | `void(obj)` | Drive a `Character3D` with the agent's desired motion |
| `BindNode(node)` | `void(obj)` | Mirror the agent position into a `SceneNode3D` in world space |

### Recommended Pattern

1. Build or load a `NavMesh3D`.
2. Create a `NavAgent3D`.
3. Bind either a `Character3D` or a `SceneNode3D`.
4. Call `SetTarget(...)`.
5. Call `Update(dt)` every frame.

When both a `Character3D` and a `SceneNode3D` are bound, the character controller is authoritative and the node is updated to match it.

### Zia Example

```zia
module NavAgentDemo;

bind Viper.Graphics3D;
bind Viper.Math;
bind Viper.Terminal;

func start() {
    var mesh = Mesh3D.NewPlane(20.0, 20.0);
    var nav = NavMesh3D.Build(mesh, 0.4, 1.8);
    var agent = NavAgent3D.New(nav, 0.4, 1.8);
    var node = SceneNode3D.New();

    NavAgent3D.BindNode(agent, node);
    NavAgent3D.set_DesiredSpeed(agent, 5.0);
    NavAgent3D.Warp(agent, Vec3.New(0.0, 0.0, 0.0));
    NavAgent3D.SetTarget(agent, Vec3.New(4.0, 0.0, 3.0));

    var i = 0;
    while (i < 20) {
        NavAgent3D.Update(agent, 0.1);
        i = i + 1;
    }

    var pos = NavAgent3D.get_Position(agent);
    Say("Agent X = " + toString(Vec3.get_X(pos)));
    Say("Agent Z = " + toString(Vec3.get_Z(pos)));
    Say("RemainingDistance = " + toString(NavAgent3D.get_RemainingDistance(agent)));
}
```

---

## Path3D

Spline path for camera rails, patrol routes, and scripted movement.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Create empty path |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Length` | Float | read | Total path length |
| `PointCount` | Integer | read | Number of control points |
| `Looping` | Boolean | write | Connect last point to first |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddPoint(position)` | `void(obj)` | Add control point (Vec3) |
| `GetPositionAt(t)` | `obj(f64)` | Sample position at parameter t (0.0-1.0) |
| `GetDirectionAt(t)` | `obj(f64)` | Sample tangent direction at t |
| `Clear()` | `void()` | Remove all points |

Looping paths include the closing segment from the final control point back to the first point in both `Length` and `GetPositionAt`/`GetDirectionAt` sampling. Non-looping paths continue to clamp at the endpoints.

### Zia Example

```zia
module PathDemo;

bind Viper.Graphics3D.Path3D;
bind Viper.Math.Vec3;

func start() {
    var path = Path3D.New();
    Path3D.AddPoint(path, Vec3.New(0.0, 0.0, 0.0));
    Path3D.AddPoint(path, Vec3.New(5.0, 2.0, 0.0));
    Path3D.AddPoint(path, Vec3.New(10.0, 0.0, 5.0));
    Path3D.AddPoint(path, Vec3.New(15.0, 1.0, 10.0));
    Path3D.set_Looping(path, true);

    // Sample along path
    for i in 0..100 {
        var t = i / 100.0;
        var pos = Path3D.GetPositionAt(path, t);
        var dir = Path3D.GetDirectionAt(path, t);
    }
}
```

---

## AnimBlend3D

Weight-based animation blending for smooth transitions between clips.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(skeleton)` | `obj(obj)` | Create blender bound to a Skeleton3D |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `StateCount` | Integer | read | Number of registered states |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddState(name, animation)` | `i64(str, obj)` | Register animation clip (returns state index) |
| `SetWeight(stateIdx, weight)` | `void(i64, f64)` | Set blend weight (0.0-1.0) |
| `SetWeightByName(name, weight)` | `void(str, f64)` | Set blend weight by name |
| `GetWeight(stateIdx)` | `f64(i64)` | Query blend weight |
| `SetSpeed(stateIdx, speed)` | `void(i64, f64)` | Playback speed multiplier per state |
| `Update(dt)` | `void(f64)` | Advance all active animations |

Weights are clamped to `[0.0, 1.0]`, NaN weights become zero, and negative per-state speeds play the
state in reverse using the same loop/clamp behavior as `AnimPlayer3D`. `Update(0.0)` still
recomputes the blended pose, and newly added states inherit the source animation's looping flag.
Blending decomposes sampled bone matrices into TRS and uses quaternion slerp for rotation, which
avoids skewed matrices when rotations are mixed.

Draw blended mesh via `Canvas3D.DrawMeshBlended(canvas, mesh, transform, material, blender)`. The `AnimBlend3D` already owns its `Skeleton3D`, so no extra skeleton argument is required.

### Zia Example

```zia
module BlendDemo;

bind Viper.Graphics3D.AnimBlend3D;
bind Viper.Graphics3D.Skeleton3D;
bind Viper.Graphics3D.Animation3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Math.Mat4;

func start() {
    var canvas = Canvas3D.New("Blend", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);

    // Assume skel, walkAnim, runAnim are loaded
    var blend = AnimBlend3D.New(skel);
    var walk_idx = AnimBlend3D.AddState(blend, "walk", walkAnim);
    var run_idx = AnimBlend3D.AddState(blend, "run", runAnim);

    // Blend 70% walk + 30% run
    AnimBlend3D.SetWeight(blend, walk_idx, 0.7);
    AnimBlend3D.SetWeight(blend, run_idx, 0.3);

    // In render loop:
    AnimBlend3D.Update(blend, dt);
    Canvas3D.Begin(canvas, cam);
    Canvas3D.DrawMeshBlended(canvas, mesh, Mat4.Identity(), mat, blend);
    Canvas3D.End(canvas);
}
```

---

## AnimController3D

Stateful skeletal animation controller for gameplay code. `AnimController3D` builds on the same sampling path as `AnimPlayer3D` and `AnimBlend3D`, but adds named states, transition defaults, clip events, root-motion extraction, and simple masked overlay layers.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(skeleton)` | `obj(obj)` | Create a controller bound to a `Skeleton3D` |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `CurrentState` | String | read | Active base-layer state name |
| `PreviousState` | String | read | Prior base-layer state name |
| `IsTransitioning` | Boolean | read | True while the base layer is inside a timed crossfade |
| `StateCount` | Integer | read | Number of registered states |
| `RootMotionDelta` | `Vec3` | read | Accumulated root-motion delta since the last consume/reset |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddState(name, animation)` | `i64(str,obj)` | Register a named `Animation3D` state |
| `AddTransition(fromState, toState, blendSeconds)` | `i1(str,str,f64)` | Register a default timed transition between two states |
| `Play(stateName)` | `i1(str)` | Play a state, using a registered transition if one exists from the current state |
| `Crossfade(stateName, blendSeconds)` | `i1(str,f64)` | Force a timed transition to another state |
| `Stop()` | `void()` | Stop the base layer and all overlay layers |
| `Update(dt)` | `void(f64)` | Advance all active layers by `dt` seconds |
| `SetStateSpeed(name, speed)` | `void(str,f64)` | Override playback speed for a named state |
| `SetStateLooping(name, loop)` | `void(str,i1)` | Override looping behavior for a named state |
| `AddEvent(stateName, timeSeconds, eventName)` | `void(str,f64,str)` | Queue an event when playback crosses the specified state-local time |
| `PollEvent()` | `str()` | Dequeue the next event name, or `""` when none are pending |
| `SetRootMotionBone(boneIdx)` | `void(i64)` | Choose which bone contributes root motion; `-1` disables it |
| `ConsumeRootMotion()` | `obj()` | Return the accumulated `Vec3` delta and clear it |
| `SetLayerWeight(layer, weight)` | `void(i64,f64)` | Set overlay weight for layers `1..3` |
| `SetLayerMask(layer, rootBone)` | `void(i64,i64)` | Restrict an overlay layer to the subtree rooted at `rootBone` |
| `PlayLayer(layer, stateName)` | `i1(i64,str)` | Start a named state on an overlay layer |
| `CrossfadeLayer(layer, stateName, blendSeconds)` | `i1(i64,str,f64)` | Crossfade an overlay layer to a new state |
| `StopLayer(layer)` | `void(i64)` | Stop one overlay layer |
| `GetBoneMatrix(boneIdx)` | `obj(i64)` | Read the controller's final global/world matrix for a bone |

Event times are clamped into the owning clip's duration and are fired when playback crosses them in
forward, reverse, exact-loop, or multi-loop updates. State speeds may be negative for reverse
playback; non-finite speeds fall back to `1.0`. Overlay weights are finite and clamped, and overlay
composition uses TRS/quaternion blending so masked layers do not introduce matrix skew. Root motion
is disabled by default, preserves forward/reverse loop-wrap deltas, and can be reset with
`SetRootMotionBone(-1)`. `Stop()` returns the output pose to bind pose.

### When To Use Which API

- Use `AnimPlayer3D` when you just need to play one clip or crossfade directly between clips.
- Use `AnimBlend3D` when you want manual weight control over several simultaneously sampled clips.
- Use `AnimController3D` when gameplay code needs named states, default transitions, root motion, queued events, or masked upper-body/lower-body style overlays.

Current limitation:
- `AnimController3D` can now drive `SceneNode3D` root motion and skinned scene-node draws through `Scene3D.SyncBindings` + `Scene3D.Draw`.
- Direct standalone mesh submission still accepts `AnimPlayer3D` and `AnimBlend3D`; use scene-node binding when you want controller-driven scene composition.

### Zia Example

```zia
module AnimController3DDemo;

bind Viper.Graphics3D;
bind Viper.Math;
bind Viper.Terminal;

func start() {
    var skel = Skeleton3D.New();
    var rootBone = Skeleton3D.AddBone(skel, "root", -1, Mat4.Identity());
    var armBone = Skeleton3D.AddBone(skel, "arm", rootBone, Mat4.Identity());
    Skeleton3D.ComputeInverseBind(skel);

    var rot = Quat.Identity();
    var scl = Vec3.One();

    var walk = Animation3D.New("walk", 1.0);
    Animation3D.set_Looping(walk, true);
    Animation3D.AddKeyframe(walk, rootBone, 0.0, Vec3.Zero(), rot, scl);
    Animation3D.AddKeyframe(walk, rootBone, 1.0, Vec3.New(10.0, 0.0, 0.0), rot, scl);

    var wave = Animation3D.New("wave", 1.0);
    Animation3D.set_Looping(wave, true);
    Animation3D.AddKeyframe(wave, armBone, 0.0, Vec3.Zero(), rot, scl);
    Animation3D.AddKeyframe(wave, armBone, 1.0, Vec3.New(0.0, 2.0, 0.0), rot, scl);

    var controller = AnimController3D.New(skel);
    AnimController3D.AddState(controller, "walk", walk);
    AnimController3D.AddState(controller, "wave", wave);
    AnimController3D.AddEvent(controller, "walk", 0.5, "step");
    AnimController3D.SetRootMotionBone(controller, rootBone);
    AnimController3D.SetLayerMask(controller, 1, armBone);
    AnimController3D.SetLayerWeight(controller, 1, 1.0);

    AnimController3D.Play(controller, "walk");
    AnimController3D.PlayLayer(controller, 1, "wave");
    AnimController3D.Update(controller, 0.5);

    var delta = AnimController3D.ConsumeRootMotion(controller);
    var armMat = AnimController3D.GetBoneMatrix(controller, armBone);

    Say("RootMotion X = " + toString(Vec3.get_X(delta)));
    Say("Event = " + AnimController3D.PollEvent(controller));
    Say("Arm Y = " + toString(Mat4.Get(armMat, 1, 3)));
}
```

See `examples/apiaudit/graphics3d/animcontroller3d_demo.zia` and `examples/apiaudit/graphics3d/animcontroller3d_demo.bas` for compact runnable samples.

---

## TextureAtlas3D

Texture array for efficient multi-texture rendering.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(width, height)` | `obj(i64, i64)` | Create atlas with layer dimensions |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(pixels)` | `i64(obj)` | Add Pixels texture layer (returns layer index) |
| `GetTexture()` | `obj()` | Export combined atlas as single Pixels |

Each added layer is copied into the atlas with a duplicated 1-pixel edge/corner border. The padding prevents bilinear filtering from bleeding neighboring layers into each other when atlas UVs land near tile edges.

### Zia Example

```zia
module AtlasDemo;

bind Viper.Graphics3D.TextureAtlas3D;
bind Viper.IO.Pixels;

func start() {
    var atlas = TextureAtlas3D.New(256, 256);
    var grass_idx = TextureAtlas3D.Add(atlas, Pixels.Load("grass.png"));
    var rock_idx = TextureAtlas3D.Add(atlas, Pixels.Load("rock.png"));
    var combined = TextureAtlas3D.GetTexture(atlas);
}
```

---

## Vegetation3D

Procedural grass/foliage rendering with wind animation and LOD.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(terrain)` | `obj(obj)` | Create vegetation system bound to a Terrain3D |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetDensityMap(pixels)` | `void(obj)` | Set density map (Pixels grayscale, white = full density) |
| `SetWindParams(strength, frequency, turbulence)` | `void(f64, f64, f64)` | Set wind animation parameters |
| `SetLODDistances(near, far)` | `void(f64, f64)` | Set LOD transition distances |
| `SetBladeSize(width, height, variance)` | `void(f64, f64, f64)` | Set grass blade dimensions |
| `Populate(camera, maxBlades)` | `void(obj, i64)` | Generate blade instances around camera |
| `Update(dt, camX, camY, camZ)` | `void(f64, f64, f64, f64)` | Update wind animation and camera-relative LOD |

Draw via `Canvas3D.DrawVegetation(vegetation)`. `Update(0.0, camX, camY, camZ)` refreshes camera-relative visibility and LOD without advancing wind time, which is useful for paused scenes. `DrawVegetation` must run inside the 3D `Canvas3D.Begin`/`End` section.

### Zia Example

```zia
module VegetationDemo;

bind Viper.Graphics3D.Vegetation3D;
bind Viper.Graphics3D.Terrain3D;
bind Viper.Graphics3D.Canvas3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.IO.Pixels;

func start() {
    var canvas = Canvas3D.New("Vegetation", 800, 600);
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 200.0);

    var terrain = Terrain3D.New(64, 64);
    // ... set up terrain ...

    var veg = Vegetation3D.New(terrain);
    Vegetation3D.SetDensityMap(veg, Pixels.Load("grass_density.png"));
    Vegetation3D.SetBladeSize(veg, 0.1, 0.4, 0.15);
    Vegetation3D.SetWindParams(veg, 0.5, 1.2, 0.3);
    Vegetation3D.SetLODDistances(veg, 30.0, 60.0);
    Vegetation3D.Populate(veg, cam, 50000);

    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        var dt = Canvas3D.get_DeltaTime(canvas) / 1000.0;
        var pos = Camera3D.get_Position(cam);
        Vegetation3D.Update(veg, dt, Vec3.get_X(pos), Vec3.get_Y(pos), Vec3.get_Z(pos));

        Canvas3D.Clear(canvas, 0.5, 0.7, 0.9);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawTerrain(canvas, terrain);
        Canvas3D.DrawVegetation(canvas, veg);
        Canvas3D.End(canvas);
        Canvas3D.Flip(canvas);
    }
}
```

---

## VideoPlayer

Video playback for game cutscenes and GUI applications.

| Member | Description |
|--------|-------------|
| `Open(path)` | Load an AVI MJPEG or OGG/Theora video file |
| `Play()` | Start playback |
| `Pause()` | Pause playback |
| `Stop()` | Stop and rewind to start |
| `Seek(seconds)` | Seek to time position |
| `Update(dt)` | Advance by delta time in seconds (call each game frame) |
| `SetVolume(vol)` | Set audio volume [0.0-1.0] |
| `Width` | Frame width in pixels (read-only) |
| `Height` | Frame height in pixels (read-only) |
| `Duration` | Total duration in seconds (read-only) |
| `Position` | Current playback position in seconds (read-only) |
| `IsPlaying` | Whether playback is active (read-only) |
| `Frame` | Current decoded Pixels frame (read-only) |

### Supported Formats

| Container | Video Codec | Audio Codec | Extension |
|-----------|-------------|-------------|-----------|
| AVI (RIFF) | MJPEG | PCM WAV | `.avi` |
| OGG | Theora | Vorbis | `.ogv` |

**MJPEG notes:** MJPEG frames in AVI files often omit Huffman tables (DHT markers). The decoder automatically injects standard JPEG Annex K DHT tables when they are missing. MJPEG has no inter-frame compression — each frame is an independent JPEG image. File sizes are large (~100-200 MB per minute at 720p) but seeking is instant since every frame is a keyframe.

**Current limitation:** `.ogv` playback now handles mixed Theora/Vorbis containers through `VideoPlayer.Open`, including audio volume/seek integration and logical-stream demux, but video fidelity is still bounded by the current in-tree Theora frame decoder implementation.

### Game Cutscene Usage

```zia
var player = VideoPlayer.Open("cutscene.avi");
VideoPlayer.Play(player);

while (VideoPlayer.get_IsPlaying(player)) {
    Canvas.Poll(canvas);
    var dt = Canvas.get_DeltaTime(canvas) / 1000.0;
    VideoPlayer.Update(player, dt);
    Canvas.Blit(canvas, 0, 0, VideoPlayer.get_Frame(player));
    Canvas.Flip(canvas);
}
```

### 3D Video Texture Usage

```zia
VideoPlayer.Update(player, dt);
Material3D.SetTexture(screenMat, VideoPlayer.get_Frame(player));
Canvas3D.DrawMesh(canvas, screenMesh, screenXform, screenMat);
```

See `examples/apiaudit/graphics3d/video_demo.zia` for a complete example.

---

## Backend Selection

The GPU backend is selected automatically at startup:

| Platform | Primary | Fallback |
|----------|---------|----------|
| macOS | Metal | Software |
| Windows | Direct3D 11 | Software |
| Linux | OpenGL 3.3 | Software |

If the GPU backend fails to initialize (no GPU, driver issue), the software rasterizer is used automatically. Check `canvas.Backend` to see which renderer is active.

For feature gating, prefer `canvas.BackendCapabilities` or `canvas.BackendSupports(name)` over string comparisons against `canvas.Backend`. Capability names currently include `software`, `gpu`, `render_target`, `window_readback`, `shadows`, `skybox`, `hardware_instancing`, `postfx`, and `gpu_postfx`. The bitmask values are:

| Bit | Capability |
|-----|------------|
| `0x0001` | Software backend |
| `0x0002` | GPU backend |
| `0x0004` | RenderTarget3D binding |
| `0x0008` | Window framebuffer readback / screenshots |
| `0x0010` | Shadow map passes |
| `0x0020` | Cubemap skybox path |
| `0x0040` | Hardware instancing backend hook |
| `0x0080` | PostFX support |
| `0x0100` | GPU-owned PostFX presentation |

**Software renderer** — Always available. Gouraud shading by default, switches to per-pixel Blinn-Phong when a normal map is present. Supports nearest/bilinear material texture filtering with imported wrap modes, per-vertex colors, shadow mapping for up to two directional lights with 3x3 PCF filtering, specular maps, normal maps, and per-pixel terrain splatting.

**Metal** (macOS) — Near-full feature parity (94%): lit/unlit textures, the shared `Material3D` PBR path (metallic/roughness, AO, alpha modes, emissive intensity, normal scale), spot light cone attenuation, linear fog, wireframe, per-frame texture caching, GPU skinning (4-bone), morph targets, up to two directional shadow maps, instanced rendering, terrain splatting, and post-processing (bloom, FXAA, tone mapping, vignette, color grading).

**OpenGL 3.3** (Linux) — Full feature parity (OGL-01 through OGL-20): all texture types, the shared `Material3D` PBR path (metallic/roughness, AO, alpha modes, emissive intensity, normal scale), spot lights, fog, wireframe, render-to-texture, up to two directional shadow maps, post-processing, instancing, skinning, morph targets, terrain splatting, cubemap skybox, environment reflections, and advanced post-FX (SSAO, depth of field, motion blur).

**Direct3D 11** (Windows) — Full feature parity: same feature set as OpenGL, including the shared `Material3D` PBR path. On non-Windows hosts, validation depends on the Windows CI lane.

Backend correctness rules are shared where possible: skinning weights are normalized before application, oversized GPU bone palettes are clamped to backend shader limits, unused bone palette slots are identity transforms, terrain splatting requires a complete control-map-plus-four-layer texture set, masked materials alpha-test shadow casters, shadow slots are advertised only after the indexed pass completes, and invalid draw/readback/texture/shadow inputs are rejected or treated conservatively instead of being dereferenced.

## Performance Tips

- **Triangle budget:** Software renderer handles ~50K triangles at 30fps (640x480). GPU backends handle 1M+ at 60fps.
- **Mesh generators:** `NewSphere(r, 8)` is adequate for most uses. Higher segments (16-32) for close-up objects.
- **Lights:** Each additional light adds computation. Use 1-3 lights for best performance.
- **Backface culling:** Enabled by default. Disable only for double-sided geometry (leaves, glass).
- **Non-uniform scaling:** All 3D backends use inverse-transpose normal transforms, so non-uniform scale preserves lighting direction consistently across software, Metal, OpenGL, and D3D11.

## Resource Limits

| Resource | Limit |
|----------|-------|
| Canvas dimensions | 8192 x 8192 |
| Texture dimensions | 8192 x 8192 |
| Mesh vertices | 16M (32-bit index buffer) |
| Lights per scene | 8 |

## Error Handling

- Constructor failures (New/Load) trap with a descriptive message
- Out-of-bounds indices trap
- GPU allocation failure falls back to software (no trap)
- `VIPER_ENABLE_GRAPHICS=OFF` builds: constructors trap, all other functions are silent no-ops

## Threading

All Graphics3D operations must be called from the main thread. `Begin`/`End` must not nest. Do not modify scene node transforms during `Draw()` traversal.
