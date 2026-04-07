# Viper.Graphics3D — User Guide

## Overview

Viper.Graphics3D is a 3D rendering module for the Viper runtime. It provides a software rasterizer (always available) with GPU-accelerated backends for Metal (macOS), Direct3D 11 (Windows), and OpenGL 3.3 (Linux). The GPU backend is selected automatically with software fallback.

**Namespace:** `Viper.Graphics3D`

---

### Table of Contents

**Getting Started**
- [Quick Start](#quick-start)

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

**Animation**
- [Skeleton3D, Animation3D, AnimPlayer3D](#skeleton3d) — Skeletal animation
- [AnimBlend3D](#animblend3d) — Animation blending
- [MorphTarget3D](#morphtarget3d) — Morph target (blend shape) animation

**Environment**
- [Terrain3D](#terrain3d) — Heightmap terrain with LOD and splatting
- [Water3D](#water3d) — Gerstner wave water simulation
- [Vegetation3D](#vegetation3d) — Instanced grass and foliage
- [InstanceBatch3D](#instancebatch3d) — GPU instanced rendering
- [Sprite3D](#sprite3d) — Billboard sprites in 3D space
- [Decal3D](#decal3d) — Projected decals

**Physics**
- [Physics3DWorld, Collider3D, Physics3DBody](#physics3dworld) — Rigid body physics
- [Character3D](#character3d) — Character controller
- [Trigger3D](#trigger3d) — Trigger volumes
- [DistanceJoint3D, SpringJoint3D](#distancejoint3d) — Constraints

**Navigation**
- [NavMesh3D](#navmesh3d) — Navigation mesh pathfinding
- [Path3D](#path3d) — 3D path waypoints

**Collision Queries**
- [Ray3D, RayHit3D, AABB3D, Sphere3D, Segment3D, Capsule3D](#particles3d) — Geometry queries

**Media**
- [VideoPlayer](#videoplayer) — Video playback (MJPEG/AVI, OGV)

**Format Loaders**
- [FBX](#fbx) — FBX file loader
- [GLTF](#gltf) — glTF 2.0 loader

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
| `Width` | Integer | read | Canvas width in pixels |
| `Height` | Integer | read | Canvas height in pixels |
| `Fps` | Integer | read | Frames per second |
| `DeltaTime` | Integer | read | Milliseconds since last Flip |
| `Backend` | String | read | Active renderer: "software", "metal", "d3d11", "opengl" |
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
| `Begin2D()` | `void()` | Start 2D overlay mode (for HUD drawing between End and Flip) |
| `End()` | `void()` | End frame — must be called after all draw calls |
| `Flip()` | `void()` | Present frame to screen, compute DeltaTime |
| `Poll()` | `i64()` | Process window events (call once per frame) |

### Drawing Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `DrawMesh(mesh, transform, material)` | `void(obj, obj, obj)` | Draw a mesh with Mat4 transform and material |
| `DrawMeshSkinned(mesh, transform, material, animPlayer)` | `void(obj, obj, obj, obj)` | Draw with skeletal animation (CPU skinning) |
| `DrawMeshMorphed(mesh, transform, material, morphTarget)` | `void(obj, obj, obj, obj)` | Draw with morph target deformation |
| `DrawMeshBlended(mesh, transform, material, skeleton, blender)` | `void(obj, obj, obj, obj)` | Draw with animation blend tree |
| `DrawInstanced(batch)` | `void(obj)` | Draw InstanceBatch3D (hardware instancing) |
| `DrawTerrain(terrain)` | `void(obj)` | Draw Terrain3D |
| `DrawDecal(decal)` | `void(obj)` | Draw Decal3D |
| `DrawSprite3D(sprite, camera)` | `void(obj, obj)` | Draw billboard Sprite3D |
| `DrawWater(water, camera)` | `void(obj, obj)` | Draw Water3D surface |
| `DrawVegetation(vegetation)` | `void(obj)` | Draw Vegetation3D |

### Lighting & Environment

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetLight(index, light)` | `void(i64, obj)` | Bind a Light3D to slot (0-7) |
| `SetAmbient(r, g, b)` | `void(f64, f64, f64)` | Set ambient light color |
| `SetSkybox(cubemap)` | `void(obj)` | Set CubeMap3D skybox |
| `ClearSkybox()` | `void()` | Remove skybox |
| `SetFog(r, g, b, near, far)` | `void(f64, f64, f64, f64, f64)` | Enable linear distance fog |
| `ClearFog()` | `void()` | Disable fog |
| `EnableShadows(mapSize)` | `void(i64)` | Enable shadow mapping (mapSize = shadow map resolution) |
| `DisableShadows()` | `void()` | Disable shadow mapping |
| `SetShadowBias(bias)` | `void(f64)` | Set shadow acne bias |

### Render Settings

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetBackfaceCull(enabled)` | `void(i1)` | Toggle backface culling (default: on) |
| `SetDTMax(ms)` | `void(i64)` | Cap DeltaTime to prevent spiral-of-death |
| `SetRenderTarget(target)` | `void(obj)` | Redirect rendering to offscreen RenderTarget3D |
| `ResetRenderTarget()` | `void()` | Return to window rendering |
| `SetPostFX(fx)` | `void(obj)` | Set PostFX3D chain (applied in Flip) |
| `SetOcclusionCulling(enabled)` | `void(i1)` | Toggle occlusion culling |

### Debug Drawing

| Method | Signature | Description |
|--------|-----------|-------------|
| `DrawLine3D(from, to, color)` | `void(obj, obj, i64)` | Draw 3D line (color = 0xRRGGBB) |
| `DrawPoint3D(pos, color, size)` | `void(obj, i64, i64)` | Draw 3D point |
| `DrawAABBWire(min, max, color)` | `void(obj, obj, i64)` | Draw wireframe AABB |
| `DrawSphereWire(center, radius, color)` | `void(obj, f64, i64)` | Draw wireframe sphere |
| `DrawDebugRay(origin, dir, length, color)` | `void(obj, obj, f64, i64)` | Draw debug ray |
| `DrawAxis(transform, size)` | `void(obj, f64)` | Draw XYZ axes at a Mat4 position |
| `Screenshot()` | `obj()` | Capture framebuffer as Pixels object |

### HUD Overlay (2D)

| Method | Signature | Description |
|--------|-----------|-------------|
| `DrawRect2D(x, y, w, h, color)` | `void(i64, i64, i64, i64, i64)` | Draw 2D rectangle on screen |
| `DrawText2D(x, y, text, color)` | `void(i64, i64, str, i64)` | Draw 2D text on screen |
| `DrawCrosshair(color, size)` | `void(i64, i64)` | Draw centered crosshair |

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
    Canvas3D.SetFog(canvas, 0.5, 0.5, 0.6, 10.0, 50.0);
    Canvas3D.EnableShadows(canvas, 1024);

    var angle = 0.0;
    while (Canvas3D.get_ShouldClose(canvas) == 0) {
        Canvas3D.Poll(canvas);
        Canvas3D.Clear(canvas, 0.1, 0.1, 0.2);
        Canvas3D.Begin(canvas, cam);
        Canvas3D.DrawMesh(canvas, box, Mat4.RotateY(angle), mat);
        Canvas3D.End(canvas);

        // HUD overlay (between End and Flip)
        Canvas3D.DrawText2D(canvas, 10, 10, "Hello 3D!", 0xFFFFFF);
        Canvas3D.DrawCrosshair(canvas, 0xFFFFFF, 12);

        Canvas3D.Flip(canvas);
        angle = angle + 0.02;
    }
}
```

**Frame lifecycle:** `Poll → Clear → Begin → DrawMesh (repeated) → End → [HUD overlay] → Flip`

**Important:** `Begin`/`End` must not nest. All `DrawMesh` calls go between `Begin` and `End`. HUD overlay calls (`DrawRect2D`, `DrawText2D`, `DrawCrosshair`) go between `End` and `Flip`.

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

### Skeletal Extensions (standalone functions)

| Function | Signature | Description |
|----------|-----------|-------------|
| `Mesh3D.SetSkeleton(mesh, skeleton)` | `void(obj, obj)` | Bind a Skeleton3D to the mesh |
| `Mesh3D.SetBoneWeights(mesh, vtx, b0, w0, b1, w1, b2, w2, b3, w3)` | `void(obj, i64, i64, f64, i64, f64, i64, f64, i64, f64)` | Set bone indices + weights for a vertex (4 bones max) |
| `Mesh3D.SetMorphTargets(mesh, morphTarget)` | `void(obj, obj)` | Bind a MorphTarget3D to the mesh |

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

**OBJ loader:** Supports v/vn/vt, quads, and negative indices. `.mtl`, `usemtl`, `g`, and `o` directives are parsed and flattened but do not create per-material submeshes.

**STL loader:** Auto-detects binary vs ASCII format, computes normals.

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
| `ScreenToRay(sx, sy, sw, sh)` | `obj(i64, i64, i64, i64)` | Unproject screen point to world-space ray (returns Vec3) |
| `Shake(intensity, duration, decay)` | `void(f64, f64, f64)` | Apply camera shake effect |
| `SmoothFollow(target, speed, height, distance, dt)` | `void(obj, f64, f64, f64, f64)` | Smoothly follow a Vec3 target position |
| `SmoothLookAt(target, speed, dt)` | `void(obj, f64, f64)` | Smoothly rotate toward a Vec3 target |
| `FPSInit()` | `void()` | Extract yaw/pitch from current view matrix |
| `FPSUpdate(mdx, mdy, fwd, right, up, speed, dt)` | `void(f64, f64, f64, f64, f64, f64, f64)` | FPS mouse look + WASD movement |

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

Surface appearance properties.

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New()` | `obj()` | Default white material |
| `NewColor(r, g, b)` | `obj(f64, f64, f64)` | Colored material (0.0-1.0 per channel) |
| `NewTextured(pixels)` | `obj(obj)` | Material with Pixels texture |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Alpha` | Float | read/write | Opacity [0.0=invisible, 1.0=opaque]. Default 1.0. Transparent objects sorted back-to-front |
| `Reflectivity` | Float | read/write | Environment reflection strength [0.0-1.0] |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetColor(r, g, b)` | `void(f64, f64, f64)` | Change diffuse color |
| `SetTexture(pixels)` | `void(obj)` | Set/change texture (Pixels) |
| `SetShininess(s)` | `void(f64)` | Specular exponent (default 32.0, higher = sharper highlights) |
| `SetUnlit(flag)` | `void(i1)` | Skip lighting (render flat color) |
| `SetNormalMap(pixels)` | `void(obj)` | Set tangent-space normal map (Pixels) |
| `SetSpecularMap(pixels)` | `void(obj)` | Set specular intensity map (Pixels) |
| `SetEmissiveMap(pixels)` | `void(obj)` | Set emissive color map (Pixels) |
| `SetEmissiveColor(r, g, b)` | `void(f64, f64, f64)` | Set emissive color multiplier (additive glow) |
| `SetShadingModel(model)` | `void(i64)` | Set shading model (see table below) |
| `SetCustomParam(index, value)` | `void(i64, f64)` | Set custom shader parameter (index 0-7) |
| `SetEnvMap(cubemap)` | `void(obj)` | Set environment CubeMap3D for reflections |

**Shading models:** `SetShadingModel` selects how the surface is shaded:
- **0 (BlinnPhong)**: Default. Diffuse + specular highlight.
- **1 (Toon)**: Quantized diffuse bands. `custom[0]` = number of bands (default 4).
- **2 (Reserved)**: Accepted for forward compatibility. Current backends fall back to the default Blinn-Phong path.
- **3 (Unlit)**: Same visual result as `SetUnlit(true)`.
- **4 (Fresnel)**: Angle-dependent alpha — edges glow brighter. `custom[0]` = power (default 3), `custom[1]` = bias.
- **5 (Emissive)**: Boosted emissive glow. `custom[0]` = strength multiplier (default 2).

See `examples/apiaudit/graphics3d/shading_demo.zia` for a complete example.

**Ownership:** Material3D holds a reference to Pixels objects (textures, maps). The user must keep Pixels alive while the material references them. If a Pixels object is collected while still set on a material, rendering may crash.

### Zia Example

```zia
module MaterialDemo;

bind Viper.Graphics3D.Material3D;
bind Viper.Graphics3D.CubeMap3D;

func start() {
    // Simple colored material
    var red = Material3D.NewColor(0.8, 0.2, 0.2);
    Material3D.SetShininess(red, 64.0);

    // Textured material with normal map
    var tex = Material3D.NewTextured(diffusePixels);
    Material3D.SetNormalMap(tex, normalPixels);
    Material3D.SetSpecularMap(tex, specPixels);
    Material3D.SetEmissiveColor(tex, 0.5, 0.0, 0.0);

    // Transparent material
    Material3D.set_Alpha(tex, 0.5);

    // Environment-mapped reflective material
    var chrome = Material3D.NewColor(0.9, 0.9, 0.9);
    Material3D.SetEnvMap(chrome, cubemap);
    Material3D.set_Reflectivity(chrome, 0.8);

    // Toon shading
    var toon = Material3D.NewColor(0.4, 0.6, 1.0);
    Material3D.SetShadingModel(toon, 1);   // 1 = Toon
    Material3D.SetCustomParam(toon, 0, 3.0); // 3 bands
}
```

## Light3D

Light sources for the scene. Up to 8 lights simultaneously.

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

**Lighting model:** Blinn-Phong with per-vertex (software) or per-pixel (GPU) shading. Includes diffuse and specular components.

## RenderTarget3D

Offscreen rendering targets for render-to-texture effects (TV screens, mirrors, security cameras, post-processing).

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(width, height)` | `obj(i64, i64)` | Create offscreen target (1-8192 pixels per dimension) |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
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

**Note:** `AsPixels()` returns a fresh copy each call. The render target's buffers are independent from the window framebuffer.

## CubeMap3D

Six-face cube texture for skyboxes and environment reflections.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(right, left, top, bottom, front, back)` | `obj(obj, obj, obj, obj, obj, obj)` | Create cubemap from 6 Pixels faces |

CubeMap3D has no methods or properties — it is a data object used by `Canvas3D.SetSkybox` and `Material3D.SetEnvMap`.

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
| `ChildCount` | Integer | read | Number of child nodes |
| `Parent` | SceneNode3D | read | Parent node (null if root) |
| `Visible` | Boolean | read/write | Visibility (hides node + all descendants) |
| `Name` | String | read/write | Name for Find() lookup |
| `Mesh` | Mesh3D | write | Mesh to render |
| `Material` | Material3D | write | Material for rendering |
| `AABBMin` | Vec3 | read | Axis-aligned bounding box minimum |
| `AABBMax` | Vec3 | read | Axis-aligned bounding box maximum |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Set local position |
| `SetScale(x, y, z)` | `void(f64, f64, f64)` | Set local scale |
| `AddChild(child)` | `void(obj)` | Attach child (auto-detaches from previous parent) |
| `RemoveChild(child)` | `void(obj)` | Detach child node |
| `GetChild(index)` | `obj(i64)` | Get child by index |
| `Find(name)` | `obj(str)` | Recursive name search in subtree |
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

Transform order: `world = parent_world * Translate * Rotate * Scale`. Dirty flags propagate to descendants automatically. `Scene3D.Save` is a structural scene export; it does not embed mesh or material payloads.

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

Bones must be added in topological order (parent before child). Max 128 bones per skeleton.

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

Rotation keyframes use SLERP; position/scale use linear interpolation.

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
| `Stop()` | `void()` | Stop playback |
| `Update(dt)` | `void(f64)` | Advance animation by dt seconds |
| `GetBoneMatrix(boneIdx)` | `obj(i64)` | Get current Mat4 for a bone |

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

- `DrawMeshSkinned` applies CPU skinning via weighted bone palette
- `Crossfade` blends using TRS decomposition: position/scale linearly interpolated, rotation via quaternion SLERP

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
| `SetWeight(shapeIdx, weight)` | `void(i64, f64)` | Set blend weight for a shape |
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

- Max 32 shapes per MorphTarget3D
- Normal deltas optional (lazy-allocated on first `SetNormalDelta`)
- Weights can be negative (reverse deformation)
- CPU-applied: `finalPos = basePos + sum(weight * delta)` per vertex

## FBX Loader

Load meshes, skeletons, materials, and morph targets from binary FBX files (v7100-7700).

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

Supports zlib-compressed array properties, negative polygon indices, Z-up to Y-up coordinate conversion. **Note:** Animation keyframe extraction is not yet implemented — loaded animations have correct names but no keyframe data. Construct keyframes manually via `Animation3D.AddKeyframe`.

---

## GLTF Loader

Load meshes and materials from glTF 2.0 files.

### Functions (standalone — no RT_CLASS)

| Function | Signature | Description |
|----------|-----------|-------------|
| `GLTF.Load(path)` | `obj(str)` | Parse glTF file |
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

**Note:** GLTF functions are standalone (not a class with methods). Unlike FBX, the GLTF loader does not currently extract skeletons or animations.

## Particles3D

Emitter-based 3D particle effects with physics, lifetime, and billboard rendering.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(maxParticles)` | `obj(i64)` | Create particle emitter with max capacity |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Count` | Integer | read | Number of active particles |
| `Emitting` | Boolean | read | Whether emitter is active |
| `Additive` | Boolean | write | Additive blending mode (fire, sparks). Default: false |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `void(f64, f64, f64)` | Set emitter world position |
| `SetDirection(dx, dy, dz, spread)` | `void(f64, f64, f64, f64)` | Set emission direction + cone spread |
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
    Particles3D.SetDirection(sparks, 0.0, 1.0, 0.0, 0.4);
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

- Particles are billboarded (camera-facing) and batched into a single draw call
- Alpha blend mode sorts particles back-to-front; additive is order-independent

## PostFX3D

Full-screen post-processing effect chain applied automatically in `Canvas3D.Flip()`.

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
| `AddBloom(threshold, intensity, passes)` | `void(f64, f64, i64)` | Bloom glow effect |
| `AddTonemap(mode, exposure)` | `void(i64, f64)` | Tone mapping (0=Reinhard, 1=ACES) |
| `AddFXAA()` | `void()` | Fast approximate anti-aliasing |
| `AddColorGrade(brightness, contrast, saturation)` | `void(f64, f64, f64)` | Color grading |
| `AddVignette(radius, softness)` | `void(f64, f64)` | Screen-edge darkening |
| `AddSSAO(radius, intensity, samples)` | `void(f64, f64, i64)` | Screen-space ambient occlusion |
| `AddDOF(focusDist, focusRange, blurAmount)` | `void(f64, f64, f64)` | Depth of field |
| `AddMotionBlur(strength, samples)` | `void(f64, i64)` | Velocity-buffer motion blur |
| `Clear()` | `void()` | Remove all effects from chain |

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

Effects are applied in chain order (first added = first applied). Max 8 effects.

## Ray3D / AABB3D / Sphere3D / Segment3D / Capsule3D / RayHit3D

3D raycasting and collision detection for picking, shooting, and physics. These are all standalone functions (no classes).

### Ray3D — Raycasting

| Function | Signature | Description |
|----------|-----------|-------------|
| `Ray3D.IntersectTriangle(o, d, v0, v1, v2)` | `f64(obj, obj, obj, obj, obj)` | Möller-Trumbore; returns distance or -1 |
| `Ray3D.IntersectMesh(o, d, mesh, transform)` | `obj(obj, obj, obj, obj)` | Test all triangles, returns closest RayHit3D or null |
| `Ray3D.IntersectAABB(o, d, min, max)` | `f64(obj, obj, obj, obj)` | Slab method; returns distance or -1 |
| `Ray3D.IntersectSphere(o, d, center, radius)` | `f64(obj, obj, obj, f64)` | Quadratic formula; returns distance or -1 |

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
- `Yaw`/`Pitch` properties allow reading/writing the current angles
- Use `Mouse.Capture()` to hide cursor and enable warp-to-center mouse tracking

## Audio3D

Spatial audio with distance attenuation and stereo panning. These are standalone functions (no class).

### Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Audio3D.SetListener(position, forward)` | `void(obj, obj)` | Set listener position and forward direction (Vec3s). Call each frame |
| `Audio3D.PlayAt(sound, position, maxDist, volume)` | `i64(obj, obj, f64, i64)` | Play sound at world position. Returns voice handle |
| `Audio3D.UpdateVoice(voice, position, maxDist)` | `void(i64, obj, f64)` | Update moving sound position. Pass 0.0 for maxDist to use per-voice default |

### Zia Example

```zia
module Audio3DDemo;

bind Viper.Graphics3D.Audio3D;
bind Viper.Graphics3D.Camera3D;
bind Viper.Sound;
bind Viper.Math.Vec3;

func start() {
    var cam = Camera3D.New(60.0, 800.0 / 600.0, 0.1, 100.0);
    var sfx = Sound.Load("explosion.wav");

    // Set listener from camera each frame
    Audio3D.SetListener(Camera3D.get_Position(cam), Camera3D.get_Forward(cam));

    // Play at world position with max audible distance 20 units
    var voice = Audio3D.PlayAt(sfx, Vec3.New(10.0, 0.0, -5.0), 20.0, 100);

    // Update moving sound
    Audio3D.UpdateVoice(voice, Vec3.New(12.0, 0.0, -5.0), 20.0);
}
```

- Linear distance attenuation: `volume * max(0, 1 - dist/maxDist)`
- Pan computed from dot product of source direction with listener's right vector
- Per-voice max_distance: each `PlayAt` records its max_distance. `UpdateVoice` with `maxDist=0.0` uses the per-voice value
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
Bodies now track quaternion orientation and angular velocity in addition to linear motion.
Shape-specific narrow-phase collision: sphere-sphere uses radial distance (not AABB),
AABB-sphere uses closest-point projection. Coulomb friction and Baumgarte positional correction.

**Current limitation:** rotational state is fully integrated for all bodies, but the simple
box/capsule collision backend still treats AABB and capsule primitives as axis-aligned /
upright collision shapes. Sphere bodies are the best fit today for fully-physical rotation.

### Physics3DWorld

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(gx, gy, gz)` | `obj(f64, f64, f64)` | Create world with gravity vector |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `BodyCount` | Integer | read | Number of active bodies |
| `CollisionCount` | Integer | read | Number of contacts from last Step |
| `JointCount` | Integer | read | Number of active joints |

| Method | Signature | Description |
|--------|-----------|-------------|
| `Step(dt)` | `void(f64)` | Advance simulation by dt seconds |
| `Add(body)` | `void(obj)` | Add body to world |
| `Remove(body)` | `void(obj)` | Remove body from world |
| `SetGravity(x, y, z)` | `void(f64, f64, f64)` | Update gravity |
| `AddJoint(joint, type)` | `void(obj, i64)` | Add joint (type: 0=distance, 1=spring) |
| `RemoveJoint(joint)` | `void(obj)` | Remove joint |
| `GetCollisionBodyA(index)` | `obj(i64)` | Get first body in contact pair |
| `GetCollisionBodyB(index)` | `obj(i64)` | Get second body in contact pair |
| `GetCollisionNormal(index)` | `obj(i64)` | Get contact normal Vec3 (A->B) |
| `GetCollisionDepth(index)` | `f64(i64)` | Get penetration depth |

---

### Collider3D

`Collider3D` is the reusable shape object for 3D physics. Prefer authoring colliders first and
then attaching them to `Physics3DBody`; the old body shape constructors remain as convenience
wrappers for simple cases.

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `NewBox(hx, hy, hz)` | `obj(f64, f64, f64)` | Box collider with half-extents |
| `NewSphere(radius)` | `obj(f64)` | Sphere collider |
| `NewCapsule(radius, height)` | `obj(f64, f64)` | Upright capsule collider |
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
| `NewAABB(sx, sy, sz, mass)` | `obj(f64, f64, f64, f64)` | AABB box body (mass=0 for static) |
| `NewSphere(radius, mass)` | `obj(f64, f64)` | Sphere body |
| `NewCapsule(radius, height, mass)` | `obj(f64, f64, f64)` | Capsule body |

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Collider` | Object | read/write | Active `Collider3D` shape for the body |
| `Position` | Vec3 | read | World position (set via `SetPosition`) |
| `Orientation` | Quat | read | World orientation (set via `SetOrientation`) |
| `Velocity` | Vec3 | read | Linear velocity (set via `SetVelocity`) |
| `AngularVelocity` | Vec3 | read | Angular velocity in radians/sec (set via `SetAngularVelocity`) |
| `Restitution` | Float | read/write | Bounciness 0-1 |
| `Friction` | Float | read/write | Surface friction |
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
2. **Native fast path:** Call `GeneratePerlin(noise, scale, octaves, persistence)` with a `PerlinNoise` object. This writes directly to the internal float heightmap, bypassing the Pixels intermediate for better performance on large terrains. The `noise` parameter is a `PerlinNoise` object, `scale` controls coordinate frequency, `octaves` sets detail layers (typically 4-8), and `persistence` controls amplitude decay (typically 0.4-0.6).

**Texture splatting:** When a splat map is set, the terrain blends 4 layer textures per-pixel during rasterization, weighted by the splat map RGBA channels. Each layer can have its own UV tiling scale for detail repetition. The software, Metal, OpenGL, and D3D11 backends all perform per-pixel splat sampling.

**LOD (Level of Detail):** Terrain chunks use 3 resolution levels based on distance from the camera:
- LOD 0 (full): 16x16 quads per chunk (nearest chunks)
- LOD 1 (half): 8x8 quads per chunk (mid-range)
- LOD 2 (quarter): 4x4 quads per chunk (distant)

Configure with `SetLODDistances(nearDist, farDist)` — chunks closer than `nearDist` use LOD 0, between `nearDist` and `farDist` use LOD 1, beyond `farDist` use LOD 2. Default: 100/250. Chunks outside the camera frustum are culled entirely (not drawn). Skirt geometry (`SetSkirtDepth(depth)`) hides cracks at LOD transitions by extending chunk edges downward.

Draw via `Canvas3D.DrawTerrain(terrain)`.

See `examples/apiaudit/graphics3d/procedural_terrain_demo.zia` and `terrain_lod_demo.zia` for complete examples.

---

## InstanceBatch3D

Draw many copies of one mesh with different transforms in a single draw call.

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

Navigation mesh with A* pathfinding for AI characters.

### Constructor

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `Build(mesh, walkableHeight, maxSlope)` | `obj(obj, f64, f64)` | Build navmesh from Mesh3D geometry |

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

### Zia Example

```zia
module NavMeshDemo;

bind Viper.Graphics3D.NavMesh3D;
bind Viper.Graphics3D.Mesh3D;
bind Viper.Math.Vec3;

func start() {
    var level_mesh = Mesh3D.FromOBJ("level.obj");
    var nav = NavMesh3D.Build(level_mesh, 1.8, 45.0);

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

Draw blended mesh via `Canvas3D.DrawMeshBlended(canvas, mesh, transform, material, skeleton, blender)`.

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
    Canvas3D.DrawMeshBlended(canvas, mesh, Mat4.Identity(), mat, skel, blend);
    Canvas3D.End(canvas);
}
```

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

Draw via `Canvas3D.DrawVegetation(vegetation)`.

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

**Software renderer** — Always available. Gouraud shading by default, switches to per-pixel Blinn-Phong when a normal map is present. Supports bilinear texture filtering, per-vertex colors, shadow mapping (directional lights), specular maps, normal maps, and per-pixel terrain splatting.

**Metal** (macOS) — Near-full feature parity (94%): lit/unlit textures, spot light cone attenuation, normal/specular/emissive maps, linear fog, wireframe, per-frame texture caching, GPU skinning (4-bone), morph targets, shadow mapping, instanced rendering, terrain splatting, and post-processing (bloom, FXAA, tone mapping, vignette, color grading).

**OpenGL 3.3** (Linux) — Full feature parity (OGL-01 through OGL-20): all texture types, spot lights, fog, wireframe, render-to-texture, shadow mapping, post-processing, instancing, skinning, morph targets, terrain splatting, cubemap skybox, environment reflections, and advanced post-FX (SSAO, depth of field, motion blur).

**Direct3D 11** (Windows) — Full feature parity: same feature set as OpenGL. On non-Windows hosts, validation depends on the Windows CI lane.

## Performance Tips

- **Triangle budget:** Software renderer handles ~50K triangles at 30fps (640x480). GPU backends handle 1M+ at 60fps.
- **Mesh generators:** `NewSphere(r, 8)` is adequate for most uses. Higher segments (16-32) for close-up objects.
- **Lights:** Each additional light adds computation. Use 1-3 lights for best performance.
- **Backface culling:** Enabled by default. Disable only for double-sided geometry (leaves, glass).
- **Non-uniform scaling:** Metal, OpenGL, and D3D11 use a proper inverse-transpose normal matrix. The software path still uses the model matrix directly, so non-uniform scaling can distort lighting there.

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
