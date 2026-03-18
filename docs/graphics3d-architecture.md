# Viper.Graphics3D — Architecture

## System Overview

```
Zia / BASIC program
    ↓ (runtime.def → rtgen → RuntimeRegistry)
Canvas3D / Mesh3D / Camera3D / Material3D / Light3D  (rt_canvas3d.c, rt_mesh3d.c, ...)
    ↓ (vgfx3d_backend_t vtable dispatch)
┌─────────────────────────────────────────────────┐
│ Software    │ Metal      │ D3D11     │ OpenGL   │
│ (always)    │ (macOS)    │ (Windows) │ (Linux)  │
│ _sw.c       │ _metal.m   │ _d3d11.c  │ _opengl.c│
└─────────────────────────────────────────────────┘
    ↓
ViperGFX (vgfx.h) — window management, framebuffer, input
    ↓
Platform (NSWindow / HWND / X11 Window)
```

## Backend Abstraction (vgfx3d_backend.h)

All rendering dispatches through a vtable:

```c
typedef struct vgfx3d_backend {
    const char *name;
    void *(*create_ctx)(vgfx_window_t win, int32_t w, int32_t h);
    void (*destroy_ctx)(void *ctx);
    void (*clear)(void *ctx, vgfx_window_t win, float r, float g, float b);
    void (*begin_frame)(void *ctx, const vgfx3d_camera_params_t *cam);
    void (*submit_draw)(void *ctx, vgfx_window_t win, const vgfx3d_draw_cmd_t *cmd,
                        const vgfx3d_light_params_t *lights, int32_t light_count,
                        const float *ambient, int8_t wireframe, int8_t backface_cull);
    void (*end_frame)(void *ctx);
} vgfx3d_backend_t;
```

**Selection logic** (`vgfx3d_select_backend`):
1. Try platform GPU backend (Metal/D3D11/OpenGL)
2. If `create_ctx` returns NULL → fall back to software
3. Software backend always succeeds

## Software Renderer Pipeline (vgfx3d_backend_sw.c)

```
Per mesh:
  1. Vertex Transform    model_matrix × position → world space
                         MVP × position → clip space (4D homogeneous)
                         model_matrix × normal → world normal

  2. Per-Vertex Lighting  Blinn-Phong: ambient + Σ(diffuse + specular) per light
                          Result: lit RGBA color per vertex (Gouraud shading)

Per triangle:
  3. Frustum Clipping     Sutherland-Hodgman against 6 clip planes
                          Input: 3 vertices → Output: 0-9 vertices (convex polygon)
                          Interpolates all attributes at clip boundaries

  4. Fan Triangulation    Polygon → N-2 triangles (fan from vertex 0)

Per sub-triangle:
  5. Perspective Divide   clip.xyz / clip.w → NDC [-1,1]³

  6. Viewport Transform   NDC → screen pixels (Y-flip: NDC +Y = screen top)

  7. Rasterization        Half-space edge functions with incremental stepping
                          Bounding box → per-pixel test → barycentric interpolation

  8. Per-Fragment:
     a. Z-test            Compare interpolated depth against Z-buffer
     b. Texture sample    Perspective-correct UV: u = (u/w) / (1/w)
     c. Color multiply    lit_color × texture_color
     d. Pixel write       Clamp to [0,255] → write RGBA to framebuffer
     e. Z-buffer update   Store new depth value
```

## Vertex Format (vgfx3d_vertex_t — 80 bytes)

```c
float pos[3];              //  0: object-space position
float normal[3];           // 12: vertex normal
float uv[2];               // 24: texture coordinates
float color[4];            // 32: RGBA vertex color
float tangent[3];           // 48: tangent vector (Phase 9)
uint8_t bone_indices[4];   // 60: bone palette indices (Phase 14)
float bone_weights[4];     // 64: blend weights (Phase 14)
```

Defined upfront at 80 bytes for all phases. Unused fields are zero-initialized.

## Matrix Conventions

- **Storage:** Row-major (`m[r*4+c]`)
- **Multiply:** Right-multiply with column vectors: `result = M × v`
- **Composition:** `MVP = P × V × M` (projection × view × model)
- **NDC:** OpenGL convention, Z in [-1, 1]
- **Coordinate system:** Right-handed (+X right, +Y up, +Z toward viewer)
- **Screen space:** Y-down (top-left origin)

### GPU Backend Matrix Handling

| Backend | Matrix Upload | Winding |
|---------|--------------|---------|
| Software | Direct (row-major float) | CCW tested in clip space |
| Metal | Transpose to column-major | `MTLWindingClockwise` (Y-flip) |
| D3D11 | `row_major` HLSL qualifier | `FrontCounterClockwise = FALSE` (Y-flip) |
| OpenGL | `glUniformMatrix4fv(..., GL_TRUE, ...)` transposes on upload | `GL_CCW` (no Y-flip) |

### Depth Range Correction

The projection matrix outputs OpenGL NDC (Z: [-1,1]). Metal and D3D11 expect Z: [0,1]. The vertex shader remaps:

```glsl
// Metal (MSL) and D3D11 (HLSL):
output.z = output.z * 0.5 + output.w * 0.5;
```

OpenGL natively supports [-1,1], so no correction is needed.

## File Structure

```
src/runtime/graphics/
├── rt_canvas3d.h              Public API declarations (all 5 types)
├── rt_canvas3d_internal.h     Internal struct definitions
├── rt_canvas3d.c              Canvas3D lifecycle + vtable dispatch
├── rt_mesh3d.c                Mesh3D (construction, generators, OBJ loader)
├── rt_camera3d.c              Camera3D (projection, view, orbit, ray cast)
├── rt_material3d.c            Material3D (color, texture, shininess)
├── rt_light3d.c               Light3D (directional, point, ambient)
├── vgfx3d_backend.h           Backend vtable interface
├── vgfx3d_backend_sw.c        Software rasterizer backend
├── vgfx3d_backend_metal.m     Metal GPU backend (macOS)
├── vgfx3d_backend_d3d11.c     D3D11 GPU backend (Windows)
└── vgfx3d_backend_opengl.c    OpenGL 3.3 GPU backend (Linux)
```

## Shader Architecture

All three GPU backends use the same lighting model (Blinn-Phong) with equivalent shaders:

| Stage | Software | Metal (MSL) | D3D11 (HLSL) | OpenGL (GLSL 330) |
|-------|----------|-------------|---------------|-------------------|
| Vertex | CPU transform | `vertex_main` | `VSMain` | `main()` vertex |
| Lighting | Per-vertex (Gouraud) | Per-pixel (Phong) | Per-pixel (Phong) | Per-pixel (Phong) |
| Fragment | CPU rasterizer | `fragment_main` | `PSMain` | `main()` fragment |

Shaders are embedded as C string literals and compiled at runtime:
- Metal: `[device newLibraryWithSource:...]`
- D3D11: `D3DCompile(...)`
- OpenGL: `glCompileShader` + `glLinkProgram`

## GC Integration

All Graphics3D objects are GC-managed via `rt_obj_new_i64`:

| Type | Finalizer | What it frees |
|------|-----------|---------------|
| Canvas3D | Yes | Backend context + vgfx window |
| Mesh3D | Yes | Vertex array + index array |
| Camera3D | No | Scalar fields only |
| Material3D | No | Scalar fields + object reference (GC-managed) |
| Light3D | No | Scalar fields only |

## Threading Model

All operations are main-thread-only. No concurrent access to Canvas3D, no nested Begin/End, no scene mutation during Draw traversal.

## Integration with 2D

Canvas3D coexists with the existing 2D Canvas system:
- Both use the same ViperGFX window infrastructure
- GPU backends render via their own surface (CAMetalLayer / swap chain / GLX)
- Software backend writes to the same vgfx framebuffer as 2D Canvas
- Input handling (Keyboard, Mouse, Pad) is shared
