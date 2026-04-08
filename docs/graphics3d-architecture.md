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
    /* Lifecycle */
    void *(*create_ctx)(vgfx_window_t win, int32_t w, int32_t h);
    void (*destroy_ctx)(void *ctx);
    /* Frame */
    void (*clear)(void *ctx, vgfx_window_t win, float r, float g, float b);
    void (*resize)(void *ctx, int32_t w, int32_t h);
    void (*begin_frame)(void *ctx, const vgfx3d_camera_params_t *cam);
    void (*submit_draw)(void *ctx, vgfx_window_t win, const vgfx3d_draw_cmd_t *cmd,
                        const vgfx3d_light_params_t *lights, int32_t light_count,
                        const float *ambient, int8_t wireframe, int8_t backface_cull);
    void (*end_frame)(void *ctx);
    /* Render target */
    void (*set_render_target)(void *ctx, vgfx3d_rendertarget_t *rt);
    /* Shadow mapping */
    void (*shadow_begin)(void *ctx, float *depth_buf, int32_t w, int32_t h, const float *light_vp);
    void (*shadow_draw)(void *ctx, const vgfx3d_draw_cmd_t *cmd);
    void (*shadow_end)(void *ctx, float bias);
    /* Skybox */
    void (*draw_skybox)(void *ctx, const void *cubemap);
    /* Instanced rendering (NULL = N individual submit_draw fallback) */
    void (*submit_draw_instanced)(void *ctx, vgfx_window_t win, const vgfx3d_draw_cmd_t *cmd,
                                  const float *instance_matrices, int32_t instance_count, ...);
    /* Display */
    void (*present)(void *ctx);
    void (*readback_rgba)(void *ctx, uint8_t *out, int32_t w, int32_t h);
    void (*show_gpu_layer)(void *ctx, vgfx_window_t win);
    void (*hide_gpu_layer)(void *ctx, vgfx_window_t win);
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
                          Vertex color × diffuse_color used as base albedo
                          Result: lit RGBA color per vertex (Gouraud shading)
                          SKIPPED when normal map present (per-pixel instead)

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
     b. Texture sample    Bilinear filtering, perspective-correct UV: u = (u/w) / (1/w)
     c. Terrain splat     If has_splat: sample weight map + 4 layer textures per-pixel
     d. Normal map        If normal_map: sample TBN, perturb normal per-pixel
     e. Per-pixel light   If normal_map: full Blinn-Phong per-pixel (with specular map)
     f. Shadow lookup     If shadow_active: transform to light space, compare depth
     g. Emissive map      Additive emissive texture contribution
     h. Fog               Linear distance fog blend
     i. Color compose     Gouraud × texture (default) or per-pixel lit result
     j. Pixel write       Clamp to [0,255] → write RGBA to framebuffer
     k. Z-buffer update   Store new depth value

Shadow Pass (before main pass, when shadows enabled):
  For each opaque mesh:
    Transform vertices by light view-projection (orthographic)
    Rasterize depth-only into shadow depth buffer (1024×1024)
    No lighting, no texturing, no color — depth writes only
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
| Metal | Transpose to column-major | `MTLWindingCounterClockwise` |
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
├── Core Rendering
│   ├── rt_canvas3d.h/c            Canvas3D lifecycle + vtable dispatch
│   ├── rt_canvas3d_internal.h     Internal struct definitions
│   ├── rt_mesh3d.c                Mesh3D (construction, generators, OBJ loader, Clear)
│   ├── rt_camera3d.c              Camera3D (projection, view, orbit, FPS, ray cast)
│   ├── rt_material3d.c            Material3D (color, texture, shininess, maps)
│   └── rt_light3d.c               Light3D (directional, point, ambient)
├── Scene Graph
│   ├── rt_scene3d.c/h             Scene3D + SceneNode3D hierarchy, frustum culling, LOD, binding sync
│   ├── rt_transform3d.c           Transform3D (standalone TRS transform)
│   └── vgfx3d_frustum.c/h        Frustum culling math
├── Physics
│   ├── rt_physics3d.c/h           Physics3DWorld + Body3D (AABB/sphere/capsule)
│   └── rt_raycast3d.c/h           Ray3D + RayHit3D intersection tests
├── Animation
│   ├── rt_skeleton3d.c/h          Skeleton3D + Animation3D + AnimPlayer3D
│   ├── rt_animcontroller3d.c/h    AnimController3D state flow, events, root motion, masks
│   ├── rt_morphtarget3d.c         MorphTarget3D blend shapes
│   └── vgfx3d_skinning.c/h       Vertex skinning math
├── Rendering Backends
│   ├── vgfx3d_backend.h           Backend vtable interface
│   ├── vgfx3d_backend_sw.c        Software rasterizer (always available)
│   ├── vgfx3d_backend_metal.m     Metal GPU backend (macOS)
│   ├── vgfx3d_backend_d3d11.c     D3D11 GPU backend (Windows)
│   └── vgfx3d_backend_opengl.c    OpenGL 3.3 GPU backend (Linux)
├── Effects & Advanced
│   ├── rt_particles3d.c/h         Particles3D emitter system
│   ├── rt_postfx3d.c/h            PostFX3D (bloom, FXAA, tonemap, vignette, SSAO, DOF, motion blur)
│   ├── rt_sprite3d.c/h            Sprite3D billboards (cached mesh/material)
│   ├── rt_decal3d.c/h             Decal3D surface projections
│   ├── rt_water3d.c/h             Water3D animated Gerstner wave surface
│   ├── rt_terrain3d.c/h           Terrain3D heightmap terrain (LOD, splatting)
│   ├── rt_vegetation3d.c/h        Vegetation3D procedural grass/foliage
│   ├── rt_instbatch3d.c/h         InstanceBatch3D instanced rendering
│   ├── rt_cubemap3d.c             CubeMap3D environment/skybox
│   ├── rt_rendertarget3d.c        RenderTarget3D offscreen rendering
│   └── rt_texatlas3d.c/h          TextureAtlas3D texture arrays
├── Animation (extended)
│   └── rt_anim_blend3d            AnimBlend3D weight-based animation blending
├── Physics (extended)
│   └── rt_joints3d.c/h            DistanceJoint3D + SpringJoint3D constraints
├── Navigation & Paths
│   ├── rt_navmesh3d.c/h           NavMesh3D A* pathfinding
│   └── rt_path3d.c/h              Path3D spline following
├── Asset Loading
│   ├── rt_fbx_loader.c/h          FBX binary format loader
│   ├── rt_gltf.c/h                glTF 2.0 format loader
│   └── rt_model3d.c/h             Model3D unified prefab/import wrapper
└── Audio
    └── rt_audio3d.c/h             Audio3D spatial audio
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

### Metal Fragment Shader Features

The Metal fragment shader supports the following features (MTL-01 through MTL-08):

| Feature | Texture Slot | Shader Path |
|---------|-------------|-------------|
| Diffuse texture | `[[texture(0)]]` | `baseColor *= sample` in both lit/unlit paths |
| Normal map | `[[texture(1)]]` | TBN perturbation with orthonormalized tangent |
| Specular map | `[[texture(2)]]` | Modulates `specColor` before Blinn-Phong |
| Emissive map | `[[texture(3)]]` | `emissive *= sample` added to final result |
| Spot lights | — | Smoothstep cone via `inner_cos`/`outer_cos` |
| Distance fog | — | Linear blend via `fogNear`/`fogFar` in PerScene |
| Wireframe | — | `setTriangleFillMode:MTLTriangleFillModeLines` |
| Texture cache | — | Per-frame `NSMutableDictionary` keyed by Pixels pointer |
| Skeletal skinning | `buffer(3)` | 4-bone palette in vertex shader |
| Morph targets | `buffer(4-5)` | Per-vertex delta accumulation in vertex shader |
| Shadow mapping | `[[texture(4)]]` | Depth-only pass + `sample_compare` in fragment |
| Terrain splat | `[[texture(5-9)]]` | 4-layer weight blend with per-layer UV scale |
| Instanced draw | vtable hook | N draws with shared vertex/index buffers |
| Post-processing | separate pipeline | Fullscreen quad: bloom, FXAA, tonemap, vignette, color grade |

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
- Canvas3D.Poll() forwards keyboard/mouse events to the input modules

## Scene Graph and Frustum Culling

`Scene3D.SyncBindings(dt)` is the explicit integration step for node bindings. It applies body-driven transforms, node-driven kinematic pushes, and animator root motion before rendering.

`Scene3D.Draw()` performs depth-first traversal of the scene node tree:

1. Extract VP matrix from camera, build frustum planes (Gribb-Hartmann)
2. For each visible node: recompute world matrix if dirty (lazy TRS propagation)
3. If node has a mesh: transform its object-space AABB to world space (8-corner expansion), test against frustum (p-vertex/n-vertex method). Skip draw if fully outside.
4. Children are ALWAYS traversed even if parent mesh is culled (child transforms may place them inside the frustum independently).

When a node is bound to an `AnimController3D`, the draw path forwards the controller's blended bone palette into the deferred draw command so skinned meshes render through the scene graph without manually calling `DrawMeshAnimated`.

## Skeletal Animation Pipeline

Bone palette computation (per-frame, in `compute_bone_palette`):

1. Start with bind pose for all bones (local transforms)
2. Override with sampled animation channels (keyframe interpolation: SLERP for rotation, lerp for position/scale)
3. Optional crossfade: blend local transforms between outgoing and incoming animations
4. Two-phase global computation:
   - Phase 1: compute global transforms (`globals[i] = globals[parent] * local[i]`) — requires topological order
   - Phase 2: compute palette (`palette[i] = globals[i] * inverse_bind[i]`)
5. CPU skinning: for each vertex, `pos = sum(weight[b] * palette[b] * base_pos)`, normals renormalized

## Particle Billboard Rendering

All particles are batched into a single draw call per `Particles3D.Draw()`:

1. Extract camera right/up vectors from the view matrix (rows 0 and 1)
2. For alpha blend mode: sort particles back-to-front by distance from camera (insertion sort)
3. Build vertex buffer: 4 vertices per particle (center ± right*halfSize ± up*halfSize), with per-vertex color/alpha from lifetime interpolation
4. Build index buffer: 2 triangles per quad (CCW winding)
5. Submit as one `DrawMesh` call with an unlit material (vertex colors handle tinting)

## Post-Processing Chain

`PostFX3D` applies effects to the software framebuffer in `Canvas3D.Flip()`:

1. Convert framebuffer RGBA8 → float RGB buffer
2. Apply each enabled effect in chain order:
   - **Bloom**: bright extract (half-res) → separable Gaussian blur (N passes) → additive composite
   - **Tone mapping**: Reinhard (`c/(c+1)`) or ACES filmic (Narkowicz approximation) + gamma correction
   - **FXAA**: luminance-based edge detection → 3x3 average on high-contrast pixels
   - **Color grading**: brightness/contrast/saturation adjustments
   - **Vignette**: radial distance falloff from center
3. Convert float RGB → RGBA8 back to framebuffer

PostFX is stored per-canvas (on the `rt_canvas3d` struct), allowing independent effect chains on multiple windows.

## FPS Mouse Capture

When `Mouse.Capture()` is active, `Canvas3D.Poll()` uses a warp-to-center approach:

1. Read current platform cursor position
2. Compute delta as `(cursor_pos - window_center)`
3. Force-set mouse deltas (bypasses normal begin_frame delta computation)
4. Process keyboard/mouse button events (mouse move events skipped when captured)
5. Warp cursor back to window center (`CGWarpMouseCursorPosition` on macOS)
6. Only warps when window has focus (`isKeyWindow` check prevents affecting other apps)
