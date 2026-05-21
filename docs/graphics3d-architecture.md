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
    void (*shadow_begin)(void *ctx, int32_t slot, float *depth_buf,
                         int32_t w, int32_t h, const float *light_vp);
    void (*shadow_draw)(void *ctx, const vgfx3d_draw_cmd_t *cmd);
    void (*shadow_end)(void *ctx, int32_t slot, float bias);
    /* Skybox */
    void (*draw_skybox)(void *ctx, const void *cubemap);
    /* Instanced rendering (NULL = N individual submit_draw fallback) */
    void (*submit_draw_instanced)(void *ctx, vgfx_window_t win, const vgfx3d_draw_cmd_t *cmd,
                                  const float *instance_matrices, int32_t instance_count, ...);
    /* Display */
    void (*present)(void *ctx);
    int (*readback_rgba)(void *ctx, uint8_t *out, int32_t w, int32_t h, int32_t stride);
    void (*present_postfx)(void *ctx, const vgfx3d_postfx_chain_t *postfx);
    void (*set_gpu_postfx_enabled)(void *ctx, int8_t enabled);
    void (*set_gpu_postfx_snapshot)(void *ctx, const vgfx3d_postfx_chain_t *postfx);
    void (*show_gpu_layer)(void *ctx);
    void (*hide_gpu_layer)(void *ctx);
} vgfx3d_backend_t;
```

**Selection logic** (`vgfx3d_select_backend`):
1. Try platform GPU backend (Metal/D3D11/OpenGL)
2. If `create_ctx` returns NULL → fall back to software
3. Software backend always succeeds

Canvas exposes the selected backend through `Canvas3D.Backend`. Production code should use
`Canvas3D.BackendCapabilities` or `Canvas3D.BackendSupports(name)` when deciding whether to
enable optional paths such as window readback, GPU post effects, hardware instancing, skybox, or
shadow maps. Those queries are derived from the active vtable hooks plus Canvas-owned software
fallbacks, so they remain stable if backend names or platform selection change.

## Runtime Input Guards

Graphics3D clamps public numeric state before it enters renderer-facing structs. `Canvas3D` clamps
clear, ambient, fog, and frame-delta inputs before they reach backend color state or camera shake.
Mesh, skinned, morphed, blended, and instanced draw entry points validate finite model matrices before
they reach culling, terrain splat capture, or backend command queues.
`Camera3D` rejects non-finite projection, `LookAt`, orbit, FPS, shake, and follow inputs so
view/projection matrices and picking rays stay finite. `SceneNode3D` and `Transform3D` sanitize TRS
components, normalize quaternions, and keep LOD distances non-negative; node animation clips reject
non-finite samples and non-increasing key times before they reach the sampler. `Collider3D` sanitizes
primitive dimensions and heightfield scales, rejects compound-child cycles, and guards heightfield
allocation sizes. `Physics3D` keeps world gravity, time steps, body motion state, damping, impulses,
and character-controller settings finite before they feed integration and broadphase code; capsule
primitive narrow-phase uses the body's quaternion orientation when deriving its world-space axis, and
ray queries use analytic sphere/capsule/box tests with AABB fallback for complex colliders.
`Mesh3D` rejects invalid procedural dimensions, non-finite OBJ/STL attributes, and overflowing OBJ face
indices; generated UV spheres avoid zero-area pole triangles, importers skip isolated degenerate faces,
bone weights are filtered and renormalized, and failed mesh builds are not cloned as drawable meshes.
`Particles3D` bounds emitter ranges,
rates, alpha, spread, shape, and update time. `InstanceBatch3D` stores only finite matrix elements
for culling and backend submission. `Light3D` clamps colors, intensities, attenuations, spot angles,
and fallback directions before the light list is copied into backend parameters. `PostFX3D` bounds
every effect parameter and exports the sanitized ordered chain, including bloom pass counts, to GPU
backends. `Mat4` operations validate matrix handles before reading storage and reject non-finite
inverse determinants. `MorphTarget3D`, `Skeleton3D`, animation players, and animation blenders
bound vertex counts, keyframe growth, blend times, speeds, and authored matrices before draw calls
can copy them into backend commands. These guards are intentionally in the runtime classes instead
of individual backends so software, Metal, D3D11, and OpenGL receive the same clean state.

Graphics3D object handles use stable internal class IDs from `rt_graphics3d_ids.h`. Public APIs that
store or dereference opaque Graphics3D handles validate those IDs before casting, which prevents a
Mesh3D/Path3D/Terrain3D-style handle mix-up from being interpreted as another runtime struct. The
IDs are negative and module-scoped so they do not collide with legacy class-id `0` objects.
The handle guard policy covers render targets, cubemaps, scene nodes, physics worlds/bodies/joints,
colliders, particles, and water resources: wrong-class handles no-op or return safe defaults before
any retained reference or struct dereference happens. Resource setters such as `Particles3D.Texture`
and `Water3D.Texture`/`NormalMap`/`EnvMap` keep the previous valid binding when given a foreign
object. Physics triggers also treat tracked bodies as weak world membership: a body removed from the
world emits one exit transition and is then forgotten.
Renderer-internal stack fixtures are available only when the graphics runtime is built with
`RT_G3D_ALLOW_STACK_FIXTURES=1` for tests. Production builds validate `Canvas3D` and `Camera3D`
handles by class id and reject arbitrary non-heap pointers before reading object fields.

The advanced helpers follow the same numeric guard policy. `TextureAtlas3D` validates atlas and
Pixels handles before copying image data, duplicates tile edge padding for bilinear correctness, and
keeps dirty cached Pixels intact on rebuild failure.
`Terrain3D`, `Sprite3D`, `Decal3D`, `Water3D`, `Vegetation3D`, `Path3D`, `NavMesh3D`, and
`NavAgent3D` clamp or reject non-finite sizes, frame rectangles, normals, Perlin parameters, wave
parameters, density maps, spline points, empty navigation meshes, and steering distances at the
runtime boundary. Water and vegetation allow zero-delta updates for dirty geometry rebuilds and
camera-relative culling refreshes without advancing simulation time.

### Metal Window Presentation Model

The Metal backend now follows the same split as the other GPU runtimes:

- direct mode: when GPU postfx is disabled, window-backed draws render straight into the current CAMetalLayer drawable and `present()` just schedules that drawable for display
- postfx mode: the main scene renders into an HDR `RGBA16F` scene target, optional overlays render into a separate UNORM overlay target, and `present_postfx` composites the final tonemapped image to the swapchain
- overlay composition: screen-space overlays are blended after bloom / tonemap / SSAO / DOF / motion blur, so UI stays crisp and the post stack keeps using the main 3D scene camera, depth, and motion history

This keeps the no-postfx path cheap while preserving the correct scene-history inputs required by the GPU postfx path.

### D3D11 Window Presentation Model

The D3D11 backend now uses two window-backed presentation modes:

- direct mode: when GPU postfx is disabled, draws render straight into the swapchain backbuffer
- postfx mode: the main scene renders into HDR scene targets, optional overlays render into a separate UNORM overlay target, and `present_postfx` composites the final image to the swapchain
- overlay composition: the first overlay pass clears the overlay target to transparent black, while later overlay passes in the same frame preserve the existing overlay contents before final compositing
- motion history: only opaque scene draws write the D3D11 motion-vector render target; alpha-blended and additive draws write color only so they do not corrupt motion blur / temporal reconstruction inputs
- texture-space conversion: D3D11 shader code converts clip/NDC coordinates to top-left-origin texture UVs for shadow maps, post-FX world reconstruction, and motion-vector sampling so vertical motion and shadow lookups match the rest of the runtime
- skinning robustness: D3D11 normalizes non-zero bone weights in the shader, falls back to the original position/vector when a skinned vertex has no usable weights, and identity-pads unused palette entries to avoid collapsing malformed or partially weighted meshes
- resource lifetime: scene resolves fall back to a backend pass-through composite instead of presenting stale swapchain contents, target binding requires complete texture/RTV/DSV/SRV/staging resource sets, texture/cubemap caches prune aged entries while preserving a resident floor, and shadow slots are advertised only as a contiguous complete prefix so shader-visible indices always correspond to bound SRVs
- allocation fallback/readback: if an offscreen D3D11 target cannot be allocated, the backend downgrades to an available target before clear/draw; readback and render-target sync unbind output resources for `CopyResource` and restore the previous target binding afterward; RTT mirrors are marked dirty only when the target handle plus color/depth/staging resources are all live
- descriptor validation: D3D11 samplers are initialized with valid comparison/max-anisotropy defaults, constant/static buffers clear stale output pointers and validate device state before `CreateBuffer`, constant buffers are aligned and bounded, instanced uploads are checked against D3D11 `ByteWidth`, and morph-target cache reuse includes normal-delta presence so position-only payloads cannot satisfy normal-morphed draws

This split keeps the no-postfx path cheap while preserving correct motion/depth history for SSAO, DOF, and motion blur when the GPU postfx path is active.

### OpenGL Window Presentation Model

The OpenGL backend now follows the same high-level split, adapted to its GLX/swapchain model:

- direct mode: when GPU postfx is disabled, window-backed draws render straight into the default framebuffer and `present()` only swaps buffers
- postfx mode: the main scene renders into an HDR scene FBO, screenshots/readback can composite that scene through the backend-owned postfx shader, and 2D overlay passes preserve scene history instead of overwriting it
- overlay composition: when a screen overlay follows a GPU-postfx main scene, OpenGL first composites the postfx result to the default framebuffer, then renders the overlay directly on top so SSAO / DOF / motion-blur history remains sourced from the 3D scene
- texture origin normalization: `Pixels` and `CubeMap3D` faces use a top-left origin, so OpenGL flips RGBA rows before `glTexImage2D` / cubemap face upload to match software, Metal, and D3D11 sampling
- cubemap seam filtering: OpenGL enables `GL_TEXTURE_CUBE_MAP_SEAMLESS`, while the software backend remaps bilinear taps across neighboring faces so skyboxes and reflections do not introduce backend-specific face seams

Like D3D11, this keeps the no-postfx path cheap while preserving the scene depth/history inputs required by the advanced GPU postfx path.

### RenderTarget3D Readback Ownership

GPU backends now treat `RenderTarget3D` color buffers as lazily synchronized CPU mirrors instead of forcing a readback at the end of every RTT frame.

- backends mark the render target color as dirty when an RTT pass finishes
- [`rt_rendertarget3d_as_pixels()`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c) and [`rt_canvas3d_screenshot()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d_overlay.c) call the backend-owned sync hook only when CPU pixels are actually requested
- [`rt_canvas3d_begin()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) synchronizes the camera's effective projection aspect against the active output size before `begin_frame`, so window resizes and RTT passes share the correct frustum
- while a render target is bound, Canvas3D overlay sizing, screenshots, and `Width`/`Height` queries follow the active target dimensions instead of the window dimensions
- `SetRenderTarget` validates the Canvas3D and RenderTarget3D handles and ensures color/depth backing storage before changing canvas/backend state
- `SetRenderTarget` and `ResetRenderTarget` are rejected while `Canvas3D` is inside `Begin`/`End`, so all queued draws in a frame flush to one consistent output
- `RenderTarget3D.NewHdr()` keeps the GPU color attachment in `RGBA16F` on GPU backends; backend sync hooks now fill both a `Pixels`-compatible tonemapped RGBA8 mirror and a linear RGBA32F CPU mirror so CPU-supported render-target postfx can operate before final `AsPixels()` conversion
- this avoids unconditional GPU stalls on RTT-heavy frames while preserving the `RenderTarget3D.AsPixels()` contract

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
     b. Texture sample    Per-slot sampler state, UV0/UV1 selection, texture transform,
                          and perspective-correct UV: u = (u/w) / (1/w)
     c. Terrain splat     If has_splat: sample weight map + 4 layer textures per-pixel
     d. Normal map        If normal_map: sample TBN, perturb normal per-pixel
     e. Per-pixel light   If normal_map: full Blinn-Phong per-pixel (with specular map)
     f. Shadow lookup     If shadow_active: transform to light space, 3x3 PCF depth compare
     g. Emissive map      Additive emissive texture contribution
     h. Environment map   Roughness-aware cubemap reflection (optional)
     i. Fog               Linear fog using projection-aware view distance
     j. Color compose     Gouraud × texture (default) or per-pixel lit result
     k. Pixel write       Clamp to [0,255] → write RGBA to framebuffer
     l. Z-buffer update   Store new depth value

Shadow Pass (before main pass, when shadows enabled):
  For each opaque mesh:
    Transform vertices by light view-projection (orthographic)
    Clip triangles against the homogeneous light frustum
    Rasterize normalized [0,1] depth into shadow depth buffer (1024×1024)
    No lighting, no texturing, no color — depth writes only
  Main pass samples shadows through percentage-closer filtering to soften single-texel edges.
```

## Vertex Format (vgfx3d_vertex_t — 92 bytes)

```c
float pos[3];              //  0: object-space position
float normal[3];           // 12: vertex normal
float uv[2];               // 24: TEXCOORD_0
float uv1[2];              // 32: TEXCOORD_1
float color[4];            // 40: RGBA vertex color
float tangent[4];          // 56: tangent.xyz + handedness sign
uint8_t bone_indices[4];   // 72: bone palette indices
float bone_weights[4];     // 76: blend weights
```

`Mesh3D.AddVertex` initializes `uv1` from `uv` for manually authored geometry and legacy assets. VSCN accepts both the older `vgfx3d_vertex_le_v1` 84-byte layout and the current `vgfx3d_vertex_le_v2` 92-byte layout, converting old vertices with `uv1 = uv` during load.

## Skybox And Cubemap Notes

- `begin_frame` forwards the camera forward vector plus an orthographic flag so every backend can reconstruct skybox directions, view vectors, and fog distances consistently
- GPU skyboxes use a full-screen triangle path with inverse-projection and inverse-view-rotation reconstruction for perspective cameras, and a direct forward-vector sample for orthographic cameras
- cubemap caches key uploads by both a stable cubemap identity and per-face `Pixels` generations, preventing stale skyboxes or environment maps from surviving allocator address reuse
- the CPU skybox fallback keeps a tight RGBA cache keyed by cubemap generation, output size, and camera state; stable fallback frames are row blits instead of full per-pixel cubemap resampling
- D3D11 now prunes cubemap cache residency by age, matching the bounded-cache policy already used by OpenGL and Metal
- environment reflections read roughness from either the scalar material value or the metallic-roughness texture and choose cubemap mips where available; the software backend mirrors this with a roughness-dependent blur kernel

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
│   ├── rt_graphics3d_ids.h        Stable class IDs and handle-validation helpers
│   ├── rt_mesh3d.c                Mesh3D (construction, generators, OBJ loader, Clear)
│   ├── rt_camera3d.c              Camera3D (projection, view, orbit, FPS, ray cast)
│   ├── rt_material3d.c            Material3D (legacy + PBR surface state, maps, clone/instance)
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
│   ├── vgfx3d_backend_d3d11_shared.c/h  Shared D3D11 packing/history helper layer
│   ├── vgfx3d_backend_opengl.c    OpenGL 3.3 GPU backend (Linux)
│   └── vgfx3d_backend_opengl_shared.c/h Shared OpenGL target/history helper layer
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
│   ├── rt_navagent3d.c/h          NavAgent3D path following, steering, and character/node bindings
│   └── rt_path3d.c/h              Path3D spline following
├── Asset Loading
│   ├── rt_fbx_loader.c/h          FBX binary format loader
│   ├── rt_gltf.c/h                glTF 2.0 format loader
│   └── rt_model3d.c/h             Model3D unified prefab/import wrapper
└── Audio
    ├── rt_audio3d.c/h             Audio3D spatial helpers and compatibility wrappers
    ├── rt_audiolistener3d.h       AudioListener3D public surface
    ├── rt_audiosource3d.h         AudioSource3D public surface
    └── rt_audio3d_objects.c       Object-backed listener/source bindings and voice updates
```

## Asset Import Hardening

`Model3D.Load` is the production-facing import path. It routes `.vscn`, `.fbx`, `.gltf`, `.glb`, and geometry-only `.obj` files into one retained asset container and now treats resource-list allocation failures as hard load failures instead of returning partially populated models.

The glTF loader enforces the following importer contract before renderer-facing objects are created:

1. GLB input must be GLB 2.0 with a matching declared length, a first JSON chunk, aligned chunk sizes, and non-overrunning chunks.
2. `asset.version` must identify glTF 2.x.
3. Buffer, bufferView, accessor, and sparse-accessor byte ranges use checked integer arithmetic. Negative offsets, overflowing spans, invalid strides, and out-of-buffer ranges fail the view resolution.
4. External `.gltf` buffers and images are relative-only. Absolute paths, URI schemes, and `.` / `..` traversal segments are rejected before opening files.
5. Valid primitives without authored materials get a shared default white PBR material so scene-graph rendering does not silently skip them.
6. Runtime skin import respects the 256-bone palette limit. Skins above the supported limit are skipped instead of overflowing the fixed backend palette.
7. Node hierarchies are validated for invalid child references, duplicate parents, and cycles before a scene root is built.

glTF material import maps core metallic-roughness PBR plus selected extensions onto `Material3D`. The vertex format carries `TEXCOORD_0` and `TEXCOORD_1`; each material texture slot stores its own `textureInfo.texCoord`, `KHR_texture_transform`, wrap mode, and nearest/linear filter state. Canvas draw commands forward that per-slot metadata to software, Metal, D3D11, and OpenGL. OpenGL uses sampler objects so one uploaded texture can be reused by multiple material slots without sampler-state aliasing.

glTF animation import now covers both skeletal clips and scene-node clips. Bone-targeted transform channels still feed `Skeleton3D` / `Animation3D`; non-joint node translation, rotation, scale, and morph `weights` channels are stored as retained node animation clips and bound automatically when a `Model3D` is instantiated. `Scene3D.SyncBindings(dt)` advances those node clips and applies morph weights before draw submission.

VSCN saves the current vertex layout as `vgfx3d_vertex_le_v2` and serializes material `textureSlots` alongside texture references, so saved imported scenes preserve UV-set choices, transforms, and sampler state on reload. The loader still accepts the older `vgfx3d_vertex_le_v1` vertex blob for compatibility.

## Shader Architecture

All three GPU backends now share the same material contract: the legacy Blinn-Phong path remains for compatibility, and `Material3D.NewPBR` uses the same direct-light metallic/roughness PBR path across Metal, D3D11, and OpenGL.

Metal, D3D11, and OpenGL now all use small shared helper layers to keep target selection, frame-history updates, cache growth, and upload/readback policy consistent with the portable tests. Metal also now caches morph payloads by `morph_key` / `morph_revision`, applies morph normal deltas in the MSL vertex path, and keeps mipmapped texture/cubemap caches pruned by frame age.

For D3D11 specifically, the CPU and HLSL sides also share explicit packed `float4` layouts for morph weights, material custom parameters, and per-slot material UV transforms.

| Stage | Software | Metal (MSL) | D3D11 (HLSL) | OpenGL (GLSL 330) |
|-------|----------|-------------|---------------|-------------------|
| Vertex | CPU transform | `vertex_main` | `VSMain` | `main()` vertex |
| Lighting | Per-vertex or per-pixel PBR in software | Per-pixel legacy/PBR | Per-pixel legacy/PBR | Per-pixel legacy/PBR |
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
| Post-processing | separate pipeline | Ordered snapshot chain: bloom, FXAA, tonemap, vignette, color grade, SSAO, DOF, motion blur |

## GC Integration

All Graphics3D objects are GC-managed via `rt_obj_new_i64`:

| Type | Finalizer | What it frees |
|------|-----------|---------------|
| Canvas3D | Yes | Backend context + vgfx window |
| Mesh3D | Yes | Vertex array + index array |
| Camera3D | No | Scalar fields only |
| Material3D | No | Scalar fields + object reference (GC-managed) |
| Light3D | No | Scalar fields only |
| Terrain3D | Yes | Height data, chunk mesh caches, splat textures |
| TextureAtlas3D | Yes | Atlas pixel buffer + cached Pixels copy |
| Vegetation3D | Yes | Instance buffers, density map, blade mesh/material |
| Sprite3D | Yes | Texture ref, cached billboard mesh/material |
| Decal3D | Yes | Texture ref, lazy mesh/material |
| Water3D | Yes | Texture refs, mesh, material |
| NavMesh3D | Yes | Baked vertex/triangle arrays |
| NavAgent3D | Yes | Path point buffer and bound nav/scene references |
| AudioListener3D / AudioSource3D | Yes | Bound node/camera/sound references and global-list links |

Temporary Vec3/Mat4 objects created for debug drawing, audio node binding, path integration, and
navigation path conversion must be released in the same call that creates them. Deferred Canvas3D
draws keep their own queued values, frame-temp object references, and per-frame snapshots of mesh
vertex/index data; helper functions should not leak local objects just because the draw command is
later consumed by the backend.

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

`Canvas3D.SetFrustumCulling(true)` applies the same coarse AABB-vs-frustum rejection to the
deferred canvas draw queue before opaque front-to-back sorting. The older
`SetOcclusionCulling` name remains as a compatibility alias; it is not a hardware occlusion-query
or Hi-Z visibility system.

`Scene3D.Draw()` performs depth-first traversal of the scene node tree:

1. Extract VP matrix from camera, build frustum planes (Gribb-Hartmann)
2. For each visible node: recompute world matrix if dirty (lazy TRS propagation)
3. If node has a mesh: transform its object-space AABB to world space (8-corner expansion), test against frustum (p-vertex/n-vertex method). Animated or morph-capable meshes use an inflated conservative AABB instead of disabling culling entirely. Skip draw if fully outside.
4. Children are ALWAYS traversed even if parent mesh is culled (child transforms may place them inside the frustum independently).

When a node is bound to an `AnimController3D`, the draw path forwards the controller's blended bone palette into the deferred draw command so skinned meshes render through the scene graph without manually calling `DrawMeshAnimated`.

## Skeletal Animation Pipeline

Bone palette computation (per-frame, in `compute_bone_palette`):

1. Start with bind pose for all bones (local transforms)
2. Override with sampled animation channels (keyframes are kept sorted by time; interpolation uses SLERP for rotation and lerp for position/scale)
3. Optional crossfade: blend local transforms between outgoing and incoming animations
4. Two-phase global computation:
   - Phase 1: compute global transforms (`globals[i] = globals[parent] * local[i]`) — requires topological order
   - Phase 2: compute palette (`palette[i] = globals[i] * inverse_bind[i]`)
5. CPU skinning: for each vertex, `pos = sum(weight[b] * palette[b] * base_pos)`, normals renormalized

## Particle Billboard Rendering

`Particles3D.Draw()` uses one temporary billboard build per call, then chooses submission strategy by blend mode:

1. Extract camera right/up vectors from the view matrix (rows 0 and 1)
2. For alpha blend mode: sort particles back-to-front by distance from camera (insertion sort)
3. Build vertex buffer: 4 vertices per particle (center ± right*halfSize ± up*halfSize), with per-vertex color/alpha from lifetime interpolation
4. Build index buffer: 2 triangles per quad (CCW winding)
5. Additive mode submits one batched `DrawMesh` call through a dedicated additive blend state and preserves each particle's interpolated alpha; alpha mode submits one keyed quad draw per particle so blending sorts correctly against the rest of the scene

## Post-Processing Chain

`PostFX3D` applies effects to the software framebuffer in `Canvas3D.Flip()` and exports the same
ordered effect chain for GPU `present_postfx` / readback paths:

1. Convert framebuffer RGBA8 → float RGB buffer
2. Apply each enabled effect in chain order:
   - **Bloom**: bright extract (half-res) → separable Gaussian blur (N passes) → additive composite; GPU paths receive the authored pass count and use it as bloom radius
   - **Tone mapping**: Reinhard (`c/(c+1)`) or ACES filmic (Narkowicz approximation) + gamma correction
   - **FXAA**: luminance-based edge detection → 3x3 average on high-contrast pixels
   - **Color grading**: brightness/contrast/saturation adjustments
   - **Vignette**: radial distance falloff from center
   - **SSAO / DOF / motion blur**: exported to GPU backends with bounded sample counts; these require scene depth/history/velocity buffers, so CPU render-target/software postfx traps if such effects would otherwise be silently skipped
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
