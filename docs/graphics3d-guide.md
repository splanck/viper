# Viper.Graphics3D — User Guide

## Overview

Viper.Graphics3D is a 3D rendering module for the Viper runtime. It provides a software rasterizer (always available) with GPU-accelerated backends for Metal (macOS), Direct3D 11 (Windows), and OpenGL 3.3 (Linux). The GPU backend is selected automatically with software fallback.

**Namespace:** `Viper.Graphics3D`

**Runtime types:** Canvas3D, Mesh3D, Camera3D, Material3D, Light3D

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
| `RecalcNormals()` | Auto-compute vertex normals from face geometry |
| `Clone()` | Deep copy of mesh data |
| `Transform(mat4)` | Transform all vertices in-place |
| `VertexCount` | Number of vertices |
| `TriangleCount` | Number of triangles |

### Winding Order

All mesh generators and the OBJ loader produce **counter-clockwise (CCW)** winding for front faces. When constructing meshes programmatically, vertices must be ordered CCW when viewed from the front.

## Camera3D

Perspective camera with view and projection matrices.

| Member | Description |
|--------|-------------|
| `New(fov, aspect, near, far)` | Create camera (fov in degrees) |
| `LookAt(eye, target, up)` | Point camera from eye toward target (Vec3 args) |
| `Orbit(target, distance, yaw, pitch)` | Orbit around target (angles in degrees) |
| `ScreenToRay(sx, sy, sw, sh)` | Unproject screen point to world-space ray (returns Vec3) |
| `Fov` | Field of view in degrees (read/write) |
| `Position` | Camera world position (read/write, Vec3) |
| `Forward` | Camera forward direction (read-only, Vec3) |
| `Right` | Camera right direction (read-only, Vec3) |

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

## Light3D

Light sources for the scene. Up to 8 lights simultaneously.

| Constructor | Description |
|-------------|-------------|
| `NewDirectional(direction, r, g, b)` | Sun-like light from a direction (Vec3) |
| `NewPoint(position, r, g, b, attenuation)` | Local point light with distance falloff |
| `NewAmbient(r, g, b)` | Uniform ambient light |

| Method | Description |
|--------|-------------|
| `SetIntensity(value)` | Brightness multiplier (default 1.0) |
| `SetColor(r, g, b)` | Change light color |

**Lighting model:** Blinn-Phong with per-vertex (software) or per-pixel (GPU) shading. Includes diffuse and specular components.

## Backend Selection

The GPU backend is selected automatically at startup:

| Platform | Primary | Fallback |
|----------|---------|----------|
| macOS | Metal | Software |
| Windows | Direct3D 11 | Software |
| Linux | OpenGL 3.3 | Software |

If the GPU backend fails to initialize (no GPU, driver issue), the software rasterizer is used automatically. Check `canvas.Backend` to see which renderer is active.

The software renderer is always available and produces identical visual output (with Gouraud shading instead of per-pixel Phong on GPU backends).

## Performance Tips

- **Triangle budget:** Software renderer handles ~50K triangles at 30fps (640x480). GPU backends handle 1M+ at 60fps.
- **Mesh generators:** `NewSphere(r, 8)` is adequate for most uses. Higher segments (16-32) for close-up objects.
- **Lights:** Each additional light adds computation. Use 1-3 lights for best performance.
- **Backface culling:** Enabled by default. Disable only for double-sided geometry (leaves, glass).

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
