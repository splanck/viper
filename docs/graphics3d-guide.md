# Viper.Graphics3D — User Guide

## Overview

Viper.Graphics3D is a 3D rendering module for the Viper runtime. It provides a software rasterizer (always available) with GPU-accelerated backends for Metal (macOS), Direct3D 11 (Windows), and OpenGL 3.3 (Linux). The GPU backend is selected automatically with software fallback.

**Namespace:** `Viper.Graphics3D`

**Runtime types:** Canvas3D, Mesh3D, Camera3D, Material3D, Light3D, RenderTarget3D, CubeMap3D, Scene3D, SceneNode3D, Skeleton3D, Animation3D, AnimPlayer3D, AnimBlend3D, MorphTarget3D, Particles3D, PostFX3D, Ray3D, RayHit3D, Physics3DWorld, Physics3DBody, Character3D, Trigger3D, Transform3D, Sprite3D, Decal3D, Water3D, Terrain3D, InstanceBatch3D, NavMesh3D, Path3D, TextureAtlas3D, Audio3D, FBX

## Quick Start

### Zia

```zia
module HelloCube;

bind Viper.Terminal;

func start() {
    var canvas = Viper.Graphics3D.Canvas3D.New("My 3D App", 640, 480);
    var cam = Viper.Graphics3D.Camera3D.New(60.0, 640.0 / 480.0, 0.1, 100.0);
    var eye = new Viper.Math.Vec3(0.0, 2.0, 5.0);
    var target = new Viper.Math.Vec3(0.0, 0.0, 0.0);
    var up = new Viper.Math.Vec3(0.0, 1.0, 0.0);
    Viper.Graphics3D.Camera3D.LookAt(cam, eye, target, up);

    var box = Viper.Graphics3D.Mesh3D.NewBox(1.0, 1.0, 1.0);
    var mat = Viper.Graphics3D.Material3D.NewColor(0.8, 0.2, 0.2);

    var light_dir = new Viper.Math.Vec3(-1.0, -1.0, -0.5);
    var light = Viper.Graphics3D.Light3D.NewDirectional(light_dir, 1.0, 1.0, 1.0);
    Viper.Graphics3D.Canvas3D.SetLight(canvas, 0, light);
    Viper.Graphics3D.Canvas3D.SetAmbient(canvas, 0.1, 0.1, 0.1);

    var angle = 0.0;
    while (Viper.Graphics3D.Canvas3D.get_ShouldClose(canvas) == 0) {
        Viper.Graphics3D.Canvas3D.Poll(canvas);
        Viper.Graphics3D.Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        var transform = Viper.Math.Mat4.RotateY(angle);
        Viper.Graphics3D.Canvas3D.Begin(canvas, cam);
        Viper.Graphics3D.Canvas3D.DrawMesh(canvas, box, transform, mat);
        Viper.Graphics3D.Canvas3D.End(canvas);
        Viper.Graphics3D.Canvas3D.Flip(canvas);
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

| Member | Type | Description |
|--------|------|-------------|
| `New(title, w, h)` | Constructor | Create canvas (1-8192 pixels per dimension) |
| `Clear(r, g, b)` | Method | Clear framebuffer and depth buffer (0.0-1.0 per channel) |
| `Begin(camera)` | Method | Start frame — must be called before DrawMesh |
| `DrawMesh(mesh, transform, material)` | Method | Draw a mesh with a Mat4 transform and material |
| `End()` | Method | End frame — must be called after all DrawMesh calls |
| `Flip()` | Method | Present frame to screen, compute DeltaTime |
| `Poll()` | Method | Process window events (call once per frame) |
| `SetLight(index, light)` | Method | Bind a light (0-7) to the scene |
| `SetAmbient(r, g, b)` | Method | Set ambient light color |
| `SetBackfaceCull(enabled)` | Method | Toggle backface culling (default: on) |
| `Wireframe` | Property | Toggle wireframe rendering (write-only, default: off) |
| `DrawLine3D(from, to, color)` | Method | Debug: draw 3D line (color = 0xRRGGBB) |
| `DrawPoint3D(pos, color, size)` | Method | Debug: draw 3D point |
| `Screenshot()` | Method | Capture framebuffer as Pixels object |
| `ShouldClose` | Property | True when user closes window |
| `Width` / `Height` | Property | Canvas dimensions |
| `Fps` | Property | Frames per second |
| `DeltaTime` | Property | Milliseconds since last Flip |
| `Backend` | Property | Active renderer: "software", "metal", "d3d11", "opengl" |

**Frame lifecycle:** `Poll → Clear → Begin → DrawMesh (repeated) → End → Flip`

**Important:** `Begin`/`End` must not nest. All `DrawMesh` calls go between `Begin` and `End`.

## Mesh3D

3D geometry with vertices and triangle indices.

### Procedural Generators

| Method | Description |
|--------|-------------|
| `NewBox(sx, sy, sz)` | Axis-aligned box (24 vertices, 12 triangles) |
| `NewSphere(radius, segments)` | UV sphere (min 4 segments) |
| `NewPlane(sx, sz)` | XZ plane facing +Y (4 vertices, 2 triangles) |
| `NewCylinder(radius, height, segments)` | Cylinder with caps (min 3 segments) |

### File Loading

| Method | Description |
|--------|-------------|
| `FromOBJ(path)` | Load Wavefront OBJ file. Supports v/vn/vt, quads, negative indices |

### Programmatic Construction

```zia
var mesh = Viper.Graphics3D.Mesh3D.New();
// AddVertex(x, y, z, nx, ny, nz, u, v)
Viper.Graphics3D.Mesh3D.AddVertex(mesh, -0.5, -0.5, 0.0, 0, 0, 1, 0, 0);
Viper.Graphics3D.Mesh3D.AddVertex(mesh,  0.5, -0.5, 0.0, 0, 0, 1, 1, 0);
Viper.Graphics3D.Mesh3D.AddVertex(mesh,  0.0,  0.5, 0.0, 0, 0, 1, 0.5, 1);
Viper.Graphics3D.Mesh3D.AddTriangle(mesh, 0, 1, 2);  // CCW winding
```

| Member | Description |
|--------|-------------|
| `Clear()` | Reset vertex/index counts to zero without freeing backing arrays (enables mesh reuse) |
| `RecalcNormals()` | Auto-compute vertex normals from face geometry |
| `Clone()` | Deep copy of mesh data |
| `Transform(mat4)` | Transform all vertices in-place |
| `VertexCount` | Number of vertices |
| `TriangleCount` | Number of triangles |

### Winding Order

All mesh generators and the OBJ loader produce **counter-clockwise (CCW)** winding for front faces. When constructing meshes programmatically, vertices must be ordered CCW when viewed from the front.

## Camera3D

Perspective or orthographic camera with view and projection matrices.

| Member | Description |
|--------|-------------|
| `New(fov, aspect, near, far)` | Create perspective camera (fov in degrees) |
| `NewOrtho(size, aspect, near, far)` | Create orthographic camera (size = half-height in world units) |
| `LookAt(eye, target, up)` | Point camera from eye toward target (Vec3 args) |
| `Orbit(target, distance, yaw, pitch)` | Orbit around target (angles in degrees) |
| `ScreenToRay(sx, sy, sw, sh)` | Unproject screen point to world-space ray (returns Vec3) |
| `Fov` | Field of view in degrees (read/write, perspective only) |
| `Position` | Camera world position (read/write, Vec3) |
| `Forward` | Camera forward direction (read-only, Vec3) |
| `Right` | Camera right direction (read-only, Vec3) |
| `IsOrtho` | Boolean — true for orthographic cameras |

**Orthographic cameras** have no perspective foreshortening. Use for isometric RPGs, strategy games, 2D-in-3D rendering, and UI overlays. The `size` parameter controls the visible area (half the viewport height in world units).

**Coordinate system:** Right-handed. +X right, +Y up, +Z toward viewer. Projection uses OpenGL NDC convention (Z: [-1,1]).

## Material3D

Surface appearance properties.

| Member | Description |
|--------|-------------|
| `New()` | Default white material |
| `NewColor(r, g, b)` | Colored material (0.0-1.0 per channel) |
| `NewTextured(pixels)` | Material with Pixels texture |
| `SetColor(r, g, b)` | Change diffuse color |
| `SetTexture(pixels)` | Set/change texture |
| `SetShininess(s)` | Specular exponent (default 32.0, higher = sharper highlights) |
| `SetUnlit(flag)` | Skip lighting (render flat color) |
| `Alpha` | Property | Opacity [0.0=invisible, 1.0=opaque]. Default 1.0. Transparent objects are sorted back-to-front automatically |
| `SetNormalMap(pixels)` | Set tangent-space normal map (Pixels) |
| `SetSpecularMap(pixels)` | Set specular intensity map (Pixels) |
| `SetEmissiveMap(pixels)` | Set emissive color map (Pixels) |
| `SetEmissiveColor(r, g, b)` | Set emissive color multiplier (additive glow) |

**Ownership:** Material3D holds a reference to Pixels objects (textures, maps). The user must keep Pixels alive while the material references them. If a Pixels object is collected while still set on a material, rendering may crash.

## Light3D

Light sources for the scene. Up to 8 lights simultaneously.

| Constructor | Description |
|-------------|-------------|
| `NewDirectional(direction, r, g, b)` | Sun-like light from a direction (Vec3) |
| `NewPoint(position, r, g, b, attenuation)` | Local point light with distance falloff |
| `NewSpot(position, direction, r, g, b, attenuation, innerAngle, outerAngle)` | Spot light with cone falloff. Angles in degrees. Full brightness inside inner cone, smoothstep to outer. |
| `NewAmbient(r, g, b)` | Uniform ambient light |

| Method | Description |
|--------|-------------|
| `SetIntensity(value)` | Brightness multiplier (default 1.0) |
| `SetColor(r, g, b)` | Change light color |

**Lighting model:** Blinn-Phong with per-vertex (software) or per-pixel (GPU) shading. Includes diffuse and specular components.

## RenderTarget3D

Offscreen rendering targets for render-to-texture effects (TV screens, mirrors, security cameras, post-processing).

| Member | Description |
|--------|-------------|
| `New(width, height)` | Create offscreen target (1-8192 pixels per dimension) |
| `Width` / `Height` | Target dimensions (read-only) |
| `AsPixels()` | Read back color buffer as a new Pixels object |

**Usage pattern:**
1. `canvas.SetRenderTarget(target)` — redirect rendering to offscreen target
2. `canvas.Clear / Begin / DrawMesh / End` — render scene to target
3. `material.SetTexture(target.AsPixels())` — use result as texture
4. `canvas.ResetRenderTarget()` — return to window rendering
5. Render main scene with the textured material

**Note:** `AsPixels()` returns a fresh copy each call. The render target's buffers are independent from the window framebuffer.

## Backend Selection

The GPU backend is selected automatically at startup:

| Platform | Primary | Fallback |
|----------|---------|----------|
| macOS | Metal | Software |
| Windows | Direct3D 11 | Software |
| Linux | OpenGL 3.3 | Software |

If the GPU backend fails to initialize (no GPU, driver issue), the software rasterizer is used automatically. Check `canvas.Backend` to see which renderer is active.

The software renderer is always available. It uses Gouraud shading by default but switches to per-pixel Blinn-Phong when a normal map is present. It supports bilinear texture filtering, per-vertex colors, shadow mapping (directional lights), specular maps, normal maps, and per-pixel terrain splatting.

The Metal backend (macOS) has near-full feature parity with the software renderer (94%). It supports: diffuse texture in both lit and unlit paths, spot light cone attenuation with smoothstep falloff, normal/specular/emissive map sampling (4 texture slots), linear distance fog, wireframe mode, per-frame texture caching, GPU skeletal skinning (4-bone vertex shader), GPU morph targets (vertex shader delta accumulation), shadow mapping (depth-only pass + comparison sampler), instanced rendering (shared vertex/index buffers), per-pixel terrain splatting (4-layer weight blend), and GPU post-processing (bloom, FXAA, tone mapping, vignette, color grading via fullscreen quad).

The OpenGL backend (Linux) now implements OGL-01 through OGL-20: diffuse/normal/specular/emissive textures, vertex-color modulation, inverse-transpose normal matrices, spot lights, fog, wireframe mode, render-to-texture readback, shadow mapping, GPU post-processing, hardware instancing, GPU skinning + morph payload consumption, terrain splatting, persistent dynamic buffers, backend-owned cubemap skyboxes, material environment reflections, GPU morph normal-delta parity, and depth/history-based GPU postfx additions including SSAO, depth of field, and velocity-buffer motion blur.

## Performance Tips

- **Triangle budget:** Software renderer handles ~50K triangles at 30fps (640x480). GPU backends handle 1M+ at 60fps.
- **Mesh generators:** `NewSphere(r, 8)` is adequate for most uses. Higher segments (16-32) for close-up objects.
- **Lights:** Each additional light adds computation. Use 1-3 lights for best performance.
- **Backface culling:** Enabled by default. Disable only for double-sided geometry (leaves, glass).
- **Non-uniform scaling:** Metal and OpenGL use a proper inverse-transpose normal matrix. The software and D3D11 paths still use the model matrix directly, so non-uniform scaling can distort lighting there.

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

---

## Scene3D / SceneNode3D

Hierarchical scene graph with parent-child transform propagation (TRS: translate, rotate, scale).

```zia
var scene = Scene3D.New()
var node = SceneNode3D.New()
SceneNode3D.SetPosition(node, 5.0, 0.0, 0.0)
SceneNode3D.set_Mesh(node, mesh)
SceneNode3D.set_Material(node, mat)
Scene3D.Add(scene, node)

// In render loop:
scene.Draw(canvas, cam)  // handles Begin/End internally
```

| Function | Description |
|----------|-------------|
| `Scene3D.New()` | Create scene with root node |
| `Scene3D.Add(scene, node)` | Add node to root |
| `Scene3D.Remove(scene, node)` | Detach node from parent |
| `Scene3D.Find(scene, name)` | Recursive depth-first name search |
| `Scene3D.Draw(scene, canvas, cam)` | Traverse + render (with frustum culling) |
| `Scene3D.Clear(scene)` | Remove all children from root |
| `Scene3D.get_NodeCount` | Total nodes in tree |
| `Scene3D.get_CulledCount` | Nodes culled in last Draw |
| `SceneNode3D.SetPosition(n, x, y, z)` | Set local position |
| `SceneNode3D.set_Rotation(n, quat)` | Set local rotation (quaternion) |
| `SceneNode3D.SetScale(n, x, y, z)` | Set local scale |
| `SceneNode3D.get_WorldMatrix` | Computed world transform (lazy) |
| `SceneNode3D.AddChild(parent, child)` | Attach child (auto-detaches from previous parent) |
| `SceneNode3D.set_Visible(n, bool)` | Hide node + all descendants |
| `SceneNode3D.set_Name(n, str)` | Set name for Find() lookup |

Transform order: `world = parent_world * Translate * Rotate * Scale`. Dirty flags propagate to descendants automatically.

## Skeleton3D / Animation3D / AnimPlayer3D

Skeletal animation with bone hierarchy, keyframe interpolation, and CPU skinning.

```zia
var skel = Skeleton3D.New()
Skeleton3D.AddBone(skel, "root", -1, Mat4.Identity())
Skeleton3D.AddBone(skel, "arm", 0, Mat4.Translate(1.0, 0.0, 0.0))
Skeleton3D.ComputeInverseBind(skel)

var anim = Animation3D.New("walk", 1.0)
Animation3D.AddKeyframe(anim, 0, 0.0, pos0, rot0, scl)
Animation3D.AddKeyframe(anim, 0, 1.0, pos1, rot1, scl)

var player = AnimPlayer3D.New(skel)
AnimPlayer3D.Play(player, anim)

// In render loop:
AnimPlayer3D.Update(player, dt)
Canvas3D.DrawMeshSkinned(canvas, mesh, transform, material, player)
```

- Bones must be added in topological order (parent before child)
- Max 128 bones per skeleton
- Rotation keyframes use SLERP; position/scale use linear interpolation
- `Crossfade(player, newAnim, duration)` blends between animations using TRS decomposition: position and scale are linearly interpolated, rotation uses quaternion SLERP for artifact-free blending
- `DrawMeshSkinned` applies CPU skinning via weighted bone palette

## MorphTarget3D

Blend shapes for facial animation, muscle flex, and shape-based deformation.

```zia
var mt = MorphTarget3D.New(vertexCount)
var smile = MorphTarget3D.AddShape(mt, "smile")
MorphTarget3D.SetDelta(mt, smile, vertexIdx, dx, dy, dz)
MorphTarget3D.SetWeight(mt, smile, 0.7)
Canvas3D.DrawMeshMorphed(canvas, mesh, transform, material, mt)
```

- Max 32 shapes per MorphTarget3D
- Normal deltas optional (lazy-allocated on first `SetNormalDelta`)
- Weights can be negative (reverse deformation)
- CPU-applied: `finalPos = basePos + sum(weight * delta)` per vertex

## FBX Loader

Load meshes, skeletons, and materials from binary FBX files (v7100-7700).

```zia
var asset = FBX.Load("model.fbx")
var mesh = FBX.GetMesh(asset, 0)
var skel = FBX.GetSkeleton(asset)
var mat = FBX.GetMaterial(asset, 0)
```

| Function | Description |
|----------|-------------|
| `FBX.Load(path)` | Parse binary FBX file |
| `FBX.get_MeshCount` | Number of geometry objects |
| `FBX.GetMesh(asset, i)` | Get Mesh3D by index |
| `FBX.GetSkeleton(asset)` | Get Skeleton3D (or null) |
| `FBX.get_AnimationCount` | Number of animation stacks |
| `FBX.GetAnimation(asset, i)` | Get Animation3D by index |
| `FBX.get_MaterialCount` | Number of materials |
| `FBX.GetMaterial(asset, i)` | Get Material3D by index |

Supports zlib-compressed array properties, negative polygon indices, Z-up to Y-up coordinate conversion. **Note:** Animation keyframe extraction is not yet implemented — loaded animations have correct names but no keyframe data. Construct keyframes manually via `Animation3D.AddKeyframe`.

## Particles3D

Emitter-based 3D particle effects with physics, lifetime, and billboard rendering.

```zia
var sparks = Particles3D.New(500)
Particles3D.SetPosition(sparks, 0.0, 0.0, 0.0)
Particles3D.SetDirection(sparks, 0.0, 1.0, 0.0, 0.4)  // dir + spread
Particles3D.SetSpeed(sparks, 2.0, 5.0)
Particles3D.SetLifetime(sparks, 0.5, 1.5)
Particles3D.SetSize(sparks, 0.2, 0.05)  // start, end
Particles3D.SetGravity(sparks, 0.0, -9.8, 0.0)
Particles3D.SetColor(sparks, 0xFFAA22, 0xFF2200)  // start, end
Particles3D.SetAlpha(sparks, 1.0, 0.0)  // fade out
Particles3D.SetRate(sparks, 50.0)
Particles3D.Start(sparks)

// In render loop (between Begin/End):
Particles3D.Update(sparks, dt)
Particles3D.Draw(sparks, canvas, cam)
```

- `Burst(count)` — instant spawn N particles
- `SetAdditive(true)` — additive blending (fire, sparks)
- `SetTexture(pixels)` — particle sprite (or NULL for solid quads)
- `SetEmitterShape(1)` — 0=point, 1=sphere, 2=box
- Particles are billboarded (camera-facing) and batched into a single draw call
- Alpha blend mode sorts particles back-to-front; additive is order-independent

## PostFX3D

Full-screen post-processing effect chain applied automatically in `Canvas3D.Flip()`.

```zia
var fx = PostFX3D.New()
PostFX3D.AddBloom(fx, 0.8, 0.5, 5)         // threshold, intensity, blur_passes
PostFX3D.AddTonemap(fx, 1, 1.2)            // mode (0=Reinhard, 1=ACES), exposure
PostFX3D.AddFXAA(fx)                        // edge-aware anti-aliasing
PostFX3D.AddColorGrade(fx, 0.05, 1.1, 1.2) // brightness, contrast, saturation
PostFX3D.AddVignette(fx, 0.6, 0.4)         // radius, softness
Canvas3D.SetPostFX(canvas, fx)
```

Effects are applied in chain order (first added = first applied). Max 8 effects. Set `PostFX3D.set_Enabled(fx, false)` to bypass temporarily.

## Ray3D / AABB3D / RayHit3D

3D raycasting and collision detection for picking, shooting, and physics.

```zia
var origin = Camera3D.get_Position(cam)
var dir = Camera3D.get_Forward(cam)

// Ray-sphere test (returns distance or -1)
var dist = Ray3D.IntersectSphere(origin, dir, center, radius)

// Ray-mesh test (returns RayHit3D with point, normal, triangle)
var hit = Ray3D.IntersectMesh(origin, dir, mesh, transform)
if (hit != null) {
    var point = RayHit3D.get_Point(hit)
    var normal = RayHit3D.get_Normal(hit)
}

// AABB overlap test
var overlaps = AABB3D.Overlaps(minA, maxA, minB, maxB)
var pushout = AABB3D.Penetration(minA, maxA, minB, maxB)  // Vec3
```

| Function | Description |
|----------|-------------|
| `Ray3D.IntersectTriangle(o, d, v0, v1, v2)` | Möller-Trumbore, returns distance |
| `Ray3D.IntersectMesh(o, d, mesh, transform)` | Test all triangles, closest hit |
| `Ray3D.IntersectAABB(o, d, min, max)` | Slab method |
| `Ray3D.IntersectSphere(o, d, center, r)` | Quadratic formula |
| `AABB3D.Overlaps(minA, maxA, minB, maxB)` | Boolean overlap test |
| `AABB3D.Penetration(minA, maxA, minB, maxB)` | Minimum push-out Vec3 |

## FPS Camera

First-person camera controller with yaw/pitch mouse look and WASD movement.

```zia
Camera3D.LookAt(cam, eye, target, up)
Camera3D.FPSInit(cam)  // extract yaw/pitch from current view

// In render loop:
var mdx = Mouse.DeltaX() * sensitivity
var mdy = Mouse.DeltaY() * sensitivity
Camera3D.FPSUpdate(cam, mdx, -mdy, fwd, right, up, speed, dt)
```

- `FPSInit` decomposes the current view matrix to extract yaw/pitch
- `FPSUpdate` accumulates yaw/pitch, clamps pitch to ±89°, applies WASD movement
- Use `Mouse.Capture()` to hide cursor and enable warp-to-center mouse tracking

## HUD Overlay

Screen-space 2D drawing on top of the 3D scene (between `End()` and `Flip()`).

```zia
Canvas3D.DrawRect2D(canvas, x, y, width, height, 0xRRGGBB)
Canvas3D.DrawCrosshair(canvas, 0xFFFFFF, 12)  // centered crosshair
Canvas3D.DrawText2D(canvas, 10, 10, "Score: 100", 0xCCCCCC)
```

**Note:** On GPU backends, these draw to the software framebuffer rather than the GPU scene surface. For GPU-visible HUD elements, render as 3D geometry (tiny unlit quads at the camera's near plane).

## Audio3D

Spatial audio with distance attenuation and stereo panning.

```zia
Audio3D.SetListener(camPosition, camForward)  // call each frame
var voice = Audio3D.PlayAt(sound, worldPos, maxDistance, volume)
Audio3D.UpdateVoice(voice, newPosition)  // for moving sounds
```

- Linear distance attenuation: `volume * max(0, 1 - dist/maxDist)`
- Pan computed from dot product of source direction with listener's right vector
- Per-voice max_distance: each `PlayAt` records its max_distance. `UpdateVoice` without explicit max_distance uses the per-voice value (not a shared global).
- Listener must be updated manually each frame (not auto-tracked from Camera3D)

## Mouse Capture

For FPS-style games, capture the mouse to prevent it from leaving the window:

```zia
Mouse.Capture()   // hides cursor + warps to center each frame
Mouse.Release()   // restores cursor
```

When captured, `Mouse.DeltaX()`/`Mouse.DeltaY()` report movement from center. The cursor is hidden and warped to the window center each `Canvas3D.Poll()` call. Only active when the window has focus.

---

## Physics3D

Impulse-based 3D rigid body simulation with AABB, sphere, and capsule collision shapes.
Shape-specific narrow-phase collision: sphere-sphere uses radial distance (not AABB),
AABB-sphere uses closest-point projection. Coulomb friction and Baumgarte positional correction.

### Physics3DWorld

| Member | Description |
|--------|-------------|
| `New(gx, gy, gz)` | Create world with gravity vector |
| `Step(dt)` | Advance simulation by dt seconds |
| `Add(body)` | Add body to world |
| `Remove(body)` | Remove body from world |
| `SetGravity(x, y, z)` | Update gravity |
| `BodyCount` | Number of active bodies |
| `CollisionCount` | Number of contacts from last Step (read after Step) |
| `GetCollisionBodyA(index)` | Get first body in contact pair |
| `GetCollisionBodyB(index)` | Get second body in contact pair |
| `GetCollisionNormal(index)` | Get contact normal (Vec3, A→B direction) |
| `GetCollisionDepth(index)` | Get penetration depth (Float) |
| `AddJoint(joint, type)` | Add joint constraint (type: 0=distance, 1=spring) |
| `RemoveJoint(joint)` | Remove joint |
| `JointCount` | Number of active joints |

---

## DistanceJoint3D

Maintains a fixed distance between two body centers. Uses positional correction + velocity damping.

| Member | Description |
|--------|-------------|
| `New(bodyA, bodyB, distance)` | Create distance joint |
| `Distance` | Target distance (read/write, Float) |

## SpringJoint3D

Hooke's law spring with configurable stiffness and damping. Bodies oscillate around rest length.

| Member | Description |
|--------|-------------|
| `New(bodyA, bodyB, restLength, stiffness, damping)` | Create spring joint |
| `Stiffness` | Spring constant (read/write, Float) |
| `Damping` | Velocity damping factor (read/write, Float) |
| `RestLength` | Equilibrium distance (read-only, Float) |

### Physics3DBody

**Constructors:** `NewAABB(sx, sy, sz, mass)`, `NewSphere(radius, mass)`, `NewCapsule(radius, height, mass)`. Mass=0 creates a static body.

| Property | Type | Description |
|----------|------|-------------|
| `Position` | Vec3 | World position (read-only; set via `SetPosition`) |
| `Velocity` | Vec3 | Linear velocity (read-only; set via `SetVelocity`) |
| `Restitution` | Float | Bounciness 0-1 |
| `Friction` | Float | Surface friction |
| `CollisionLayer` | Integer | Bitmask layer |
| `CollisionMask` | Integer | Bitmask for which layers to collide with |
| `Static` | Boolean | Immovable body (mass-independent) |
| `Trigger` | Boolean | Overlap detection only, no physics response |
| `Grounded` | Boolean | Touching ground surface |
| `GroundNormal` | Vec3 | Surface normal of ground contact |
| `Mass` | Float | Body mass |

| Method | Description |
|--------|-------------|
| `SetPosition(x, y, z)` | Teleport body |
| `SetVelocity(vx, vy, vz)` | Set linear velocity |
| `ApplyForce(fx, fy, fz)` | Accumulate force (applied per step) |
| `ApplyImpulse(ix, iy, iz)` | Instant velocity change |

### Character3D

Controller-based character movement with slide-and-step collision response. Attempts to move
in the requested direction; on collision, slides along the surface (up to 3 iterations).
Respects slope limiting and step height for stairs.

| Member | Description |
|--------|-------------|
| `New(radius, height, mass)` | Create character controller |
| `Move(direction_vec3, dt)` | Move with collision response |
| `SetPosition(x, y, z)` | Teleport |
| `SetSlopeLimit(degrees)` | Max climbable slope angle |
| `StepHeight` | Float — max step-up height |
| `Grounded` | Boolean — on ground |
| `JustLanded` | Boolean — landed this frame |
| `Position` | Vec3 — current position |

### Trigger3D

AABB zone for enter/exit detection.

| Member | Description |
|--------|-------------|
| `New(x0, y0, z0, x1, y1, z1)` | Create AABB trigger zone |
| `Contains(position_vec3)` | Point-in-zone test |
| `Update(body)` | Check body against zone (call per frame per body) |
| `SetBounds(x0, y0, z0, x1, y1, z1)` | Update zone bounds |
| `EnterCount` | Bodies that entered since last check |
| `ExitCount` | Bodies that exited since last check |

---

## Transform3D

Standalone 3D transform (position, rotation, scale) with lazy matrix computation.

| Member | Description |
|--------|-------------|
| `New()` | Create identity transform |
| `SetPosition(x, y, z)` | Set position |
| `SetEuler(pitch, yaw, roll)` | Set rotation from Euler angles (degrees) |
| `SetScale(sx, sy, sz)` | Set scale |
| `Translate(delta_vec3)` | Move relative |
| `Rotate(axis_vec3, angle)` | Rotate around axis (radians) |
| `LookAt(target_vec3, up_vec3)` | Orient toward target |
| `Position` | Vec3 |
| `Rotation` | Quat (read/write) |
| `Scale` | Vec3 |
| `Matrix` | Mat4 — computed world matrix |

---

## Sprite3D

Camera-facing billboard sprite in 3D space. Useful for particles, foliage, and NPC labels.

| Member | Description |
|--------|-------------|
| `New(texture)` | Create billboard from Pixels texture |
| `SetPosition(x, y, z)` | World position |
| `SetScale(width, height)` | Billboard size in world units |
| `SetAnchor(ax, ay)` | Pivot point (0-1, default 0.5/0.5 = center) |
| `SetFrame(x, y, w, h)` | Sprite sheet sub-rectangle (pixels) |

Draw via `Canvas3D.DrawSprite3D(sprite, camera)`. Mesh and material are cached internally — no per-frame allocation.

---

## Decal3D

Surface-projected quad for bullet holes, blood splatters, footprints.

| Member | Description |
|--------|-------------|
| `New(position_vec3, normal_vec3, size, material)` | Create decal aligned to surface |
| `SetLifetime(seconds)` | Auto-fade duration |
| `Update(dt)` | Advance lifetime |
| `Expired` | Boolean — true when lifetime reached |

Draw via `Canvas3D.DrawDecal(decal)`.

---

## Water3D

Animated water surface with sine-based waves.

| Member | Description |
|--------|-------------|
| `New(width, depth)` | Create water plane |
| `SetHeight(y)` | World Y position |
| `SetWaveParams(speed, amplitude, frequency)` | Wave animation parameters |
| `SetColor(r, g, b, a)` | Water tint (0-1 float RGBA) |
| `Update(dt)` | Advance wave animation |

Draw via `Canvas3D.DrawWater(water, camera)`.

---

## Terrain3D

Heightmap-based terrain with chunked rendering and texture splatting.

| Member | Description |
|--------|-------------|
| `New(widthSegments, depthSegments)` | Create terrain grid |
| `SetHeightmap(pixels)` | Load height from red channel of Pixels |
| `SetMaterial(material)` | Set surface material |
| `SetScale(sx, sy, sz)` | Set terrain world size (Y = height scale) |
| `SetSplatMap(pixels)` | Set splat map (RGBA Pixels: R/G/B/A = weight for layers 0-3) |
| `SetLayerTexture(layer, pixels)` | Set texture for splat layer (0-3) |
| `SetLayerScale(layer, scale)` | Set UV tiling scale per layer (default 1.0) |
| `GetHeightAt(x, z)` | Query height at world XZ position |
| `GetNormalAt(x, z)` | Query surface normal at world XZ position |

**Texture splatting:** When a splat map is set, the terrain blends 4 layer textures per-pixel during rasterization, weighted by the splat map RGBA channels. Each layer can have its own UV tiling scale for detail repetition. The software and OpenGL backends perform per-pixel splat sampling. A baked fallback texture is still used on GPU backends that have not implemented splat shaders yet.

Draw via `Canvas3D.DrawTerrain(terrain)`.

---

## InstanceBatch3D

Draw many copies of one mesh with different transforms in a single draw call.

| Member | Description |
|--------|-------------|
| `New(mesh, material)` | Create batch for a mesh+material pair |
| `Add(transform)` | Add instance with Mat4 transform |
| `Remove(index)` | Remove instance by index |
| `Set(index, transform)` | Update instance transform |
| `Clear()` | Remove all instances |
| `Count` | Number of instances |

Draw via `Canvas3D.DrawInstanced(batch)`.

---

## NavMesh3D

Navigation mesh with A* pathfinding for AI characters.

| Member | Description |
|--------|-------------|
| `Build(mesh, walkableHeight, maxSlope)` | Constructor — build from Mesh3D geometry |
| `FindPath(start_vec3, goal_vec3)` | Returns path as object (list of Vec3 waypoints) |
| `SamplePosition(position_vec3)` | Snap position to nearest point on navmesh |
| `IsWalkable(position_vec3)` | Check if position is on the navmesh |
| `SetMaxSlope(degrees)` | Update walkability threshold |
| `DebugDraw(canvas)` | Visualize navmesh wireframe |
| `TriangleCount` | Number of triangles in mesh |

---

## Path3D

Spline path for camera rails, patrol routes, and scripted movement.

| Member | Description |
|--------|-------------|
| `New()` | Create empty path |
| `AddPoint(position_vec3)` | Add control point |
| `GetPositionAt(t)` | Sample position at parameter t (0-1) |
| `GetDirectionAt(t)` | Sample tangent direction at t |
| `Clear()` | Remove all points |
| `Length` | Total path length |
| `PointCount` | Number of control points |
| `Looping` | Boolean (write-only) — connect last point to first |

---

## AnimBlend3D

Weight-based animation blending for smooth transitions between clips.

| Member | Description |
|--------|-------------|
| `New(skeleton)` | Create blender bound to a Skeleton3D |
| `AddState(name, animation)` | Register animation clip (returns state index) |
| `SetWeight(stateIdx, weight)` | Set blend weight (0-1) for a state |
| `SetWeightByName(name, weight)` | Set blend weight by name |
| `GetWeight(stateIdx)` | Query blend weight |
| `SetSpeed(stateIdx, speed)` | Playback speed multiplier per state |
| `Update(dt)` | Advance all active animations |
| `StateCount` | Number of registered states |

Draw blended mesh via `Canvas3D.DrawMeshBlended(mesh, transform, material, skeleton, blender)`.

---

## TextureAtlas3D

Texture array for efficient multi-texture rendering.

| Member | Description |
|--------|-------------|
| `New(width, height)` | Create atlas with layer dimensions |
| `Add(pixels)` | Add texture layer (returns layer index) |
| `GetTexture()` | Export combined atlas as single Pixels |
