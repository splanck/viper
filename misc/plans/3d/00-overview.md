# Viper.Graphics3D — Project Overview

## Project: Viper.Graphics3D

A multi-phase effort to add 3D graphics capabilities to the Viper runtime, starting with a software rasterizer and progressing to GPU-accelerated rendering via Metal (macOS), Direct3D 11 (Windows), and OpenGL 3.3 Core (Linux).

**Namespace:** `Viper.Graphics3D` (distinct from existing `Viper.Graphics` 2D namespace)

**Runtime types:** Canvas3D, Mesh3D, Camera3D, Material3D, Light3D, RenderTarget3D, CubeMap3D, Scene3D, SceneNode3D, Skeleton3D, Animation3D, AnimPlayer3D, FBX, MorphTarget3D, Particles3D, PostFX3D

**Input handling:** Canvas3D is rendering-only. All input via existing `Viper.Input.Keyboard` / `Viper.Input.Mouse` / `Viper.Input.Pad` modules.

**Phases:**

| # | Scope | Duration | Dependencies |
|---|-------|----------|--------------|
| 1 | Software 3D renderer | 6 weeks | None (uses existing framebuffer + math) |
| 2 | Backend abstraction (vtable) | 2 weeks | Phase 1 complete |
| 3 | Metal GPU backend (macOS) | 6 weeks | Phase 2 complete |
| 4 | Direct3D 11 GPU backend (Windows) | 6 weeks | Phase 2 complete |
| 5 | OpenGL 3.3 GPU backend (Linux) | 6 weeks | Phase 2 complete |
| 6 | Comprehensive testing | 3 weeks | Phases 1-5 |
| 7 | Documentation | 2 weeks | Phases 1-5 |
| 8 | Render-to-texture / offscreen rendering | 2 weeks | Phase 2 |
| 9 | Multi-texture materials (normal/specular/emissive maps) | 2 weeks | Phase 2 |
| 10 | Alpha blending / transparency | 1 week | Phase 2 |
| 11 | Cube map textures (skybox + reflections) | 2 weeks | Phase 9 |
| 12 | Scene graph / spatial hierarchy (3D) | 2 weeks | Phase 1 |
| 13 | Frustum culling | 1 week | Phase 12 |
| 14 | Skeletal animation & skinning | 3 weeks | Phase 9 |
| 15 | FBX loader (zero-dependency) | 4 weeks | Phase 14 |
| 16 | Morph targets / blend shapes | 1.5 weeks | Phase 14 |
| 17 | 3D particle system | 2 weeks | Phases 10, 12 |
| 18 | Post-processing effects (bloom, FXAA, etc.) | 2.5 weeks | Phases 8, 10 |

Phases 3-5 can run in parallel on different platforms.
Phase 12 (Scene Graph) only depends on Phases 1-2 and should be started as early as practical, since Phases 13, 15, and 17 depend on it.
Phases 11/12 can run in parallel. Phases 16/17 can run in parallel.

**Existing infrastructure leveraged:**
- Vec3, Mat4, Mat3, Quat (complete 3D math in `src/runtime/graphics/`)
- ViperGFX framebuffer + platform backends (`src/lib/graphics/`)
- Pixels image buffer (`src/runtime/graphics/rt_pixels.c`)
- Runtime class registration pipeline (`runtime.def` → rtgen → frontends)

## Frontend Integration

Adding new 3D types to Zia and BASIC requires **no manual frontend work**:

1. Add `RT_FUNC` / `RT_CLASS` entries to `src/il/runtime/runtime.def`
2. Add `RTCLS_*` enum value to `src/il/runtime/classes/RuntimeClasses.hpp` (RuntimeTypeId enum)
3. Add C implementation to `src/runtime/graphics/rt_*.c`
4. Add source file to `RT_GRAPHICS_SOURCES` in `src/runtime/CMakeLists.txt`
5. Build — `rtgen` (`src/tools/rtgen/rtgen.cpp`) auto-generates `.inc` files, both frontends discover types via `RuntimeRegistry::instance()`

New `RTCLS_*` entries for all 16 types (add to RuntimeTypeId enum):

```
RTCLS_Canvas3D, RTCLS_Mesh3D, RTCLS_Camera3D, RTCLS_Material3D, RTCLS_Light3D,
RTCLS_RenderTarget3D, RTCLS_CubeMap3D, RTCLS_Scene3D, RTCLS_SceneNode3D,
RTCLS_Skeleton3D, RTCLS_Animation3D, RTCLS_AnimPlayer3D, RTCLS_FBX,
RTCLS_MorphTarget3D, RTCLS_Particles3D, RTCLS_PostFX3D
```

**Key design decisions:**
- Software renderer writes into existing `uint8_t *pixels` RGBA framebuffer
- GPU backends create native surfaces (CAMetalLayer, DXGI swap chain, GLX context) alongside existing 2D presentation
- 2D and 3D can coexist in same window (3D scene + 2D HUD overlay)
- All math stays double precision at API boundary; rasterizer/GPU uses float internally
- Backend selected at runtime: `auto` tries GPU first, falls back to software
- DeltaTime is i64 milliseconds (matching existing Canvas). Animation internals use double seconds internally. Conversion: `seconds = (double)dt_ms / 1000.0`
- Static batching (mesh merging for draw call reduction) is deferred to a post-Phase 18 optimization pass

**Performance targets:**

| Metric | Software Renderer | GPU Backends |
|--------|-------------------|-------------|
| Triangle throughput | ≥50K tris/frame at 30fps (640x480) | ≥1M tris at 60fps |
| Scene graph traversal | ≤1ms for 1000 nodes | Same |
| FBX load time | ≤2s for 10MB file | Same |
| Skeletal animation update | ≤0.5ms for 128 bones | Same |
| Particle budget | 1K at 60fps | 10K at 60fps |

**Critical technical constraints verified from codebase:**
- Mat4 is **row-major**, right-multiply with column vectors (`M(mat, r, c) = mat->m[r*4+c]`)
- Mat4 perspective uses **OpenGL NDC convention (Z: [-1,1])** — Z-buffer range is [-1,1], not [0,1]
- Vec3 is **right-handed** (+X right, +Y up, +Z toward viewer)
- Screen space is **Y-down** (top-left origin) — rasterizer must flip Y in NDC→screen transform
- Framebuffer pixel format is **RGBA** (`uint8_t[4]` per pixel: R, G, B, A in byte order)
- Framebuffer stride is always `width * 4` (tightly packed)
- Canvas color `0x00RRGGBB` → internal RGBA via `(color << 8) | 0xFF`
- `VIPER_ENABLE_GRAPHICS` is both a CMake option and a `#ifdef` guard in .c files; stubs compiled when OFF
- No GPU APIs exist anywhere in the codebase currently — this is greenfield
- OBJ loading must be implemented from scratch (existing loaders: BMP, PNG only)

## Threading Model

All `Viper.Graphics3D` operations must be called from the main thread (consistent with existing `Viper.Graphics`). Specific constraints:

- **Canvas3D:** single instance per window. `Begin`/`End` must not nest. No concurrent access.
- **AnimPlayer3D:** `Update()` and `Canvas3D.DrawMeshSkinned()` must not execute concurrently (`bone_palette` is modified in-place during `Update`).
- **Scene3D:** calling `SetPosition`/`SetRotation`/`SetScale` during `Draw()` traversal is undefined behavior.
- **FBX.Load():** synchronous blocking I/O on the calling thread.
- **Multiple windows:** supported but each Canvas3D must be driven from the main thread sequentially.

## Error Handling Policy

Consistent with existing runtime patterns (`rt_canvas.c`):

- **Constructors** (New/Load): trap on failure with descriptive message. E.g., `"Canvas3D.New: failed to create window"`, `"FBX.Load: file not found: <path>"`
- **Methods on NULL self:** undefined behavior (caller's responsibility, matches existing Canvas)
- **Out-of-bounds indices:** trap with message. E.g., `"GetMesh: index 5 out of range [0, 3)"`
- **Invalid arguments:** trap. E.g., `"CubeMap3D.New: faces must be square and same dimensions"`
- **Resource limits exceeded:** trap. E.g., `"Skeleton3D.AddBone: bone count exceeds maximum (128)"`
- **GPU allocation failure:** fall back to software renderer. Log warning, do not trap.
- **NaN/Inf in vertex data:** undefined rendering output (no validation in hot path)
- **Stubs** (`VIPER_ENABLE_GRAPHICS=OFF`): constructors trap, all other functions are silent no-ops returning 0/NULL/false.

## Resource Limits

| Resource | Limit | Rationale |
|----------|-------|-----------|
| Canvas3D dimensions | 8192 x 8192 max | Software Z-buffer = 256MB at 8K |
| RenderTarget3D dimensions | 8192 x 8192 max | Same as canvas |
| Texture dimensions | 8192 x 8192 max | GPU compatibility (GL 3.3 guarantees 8K) |
| Mesh vertex count | 16M (`uint32_t` index buffer) | 32-bit index range |
| Skeleton bones | 128 (`VGFX3D_MAX_BONES`) | GPU uniform buffer limit |
| Morph target shapes | 32 (`VGFX3D_MAX_MORPH_SHAPES`) | Memory per shape |
| Particle count | Constructor arg (user-chosen) | malloc'd pool |
| PostFX chain length | 8 effects max | Fixed array in struct |
| Lights per scene | 8 (`VGFX3D_MAX_LIGHTS`) | Uniform buffer slots |
| Scene graph depth | 256 levels (recursion guard) | Stack overflow prevention |
| FBX file size | 256MB | Matches `rt_compress.c` `INFLATE_MAX_OUTPUT` |

## Additional APIs

APIs missing from individual phase specs that should be added:

**Phase 1 — Canvas3D:**
- `Canvas3D.Screenshot()` → Pixels — capture current framebuffer as Pixels object
- `RT_FUNC(Canvas3DScreenshot, rt_canvas3d_screenshot, "Viper.Graphics3D.Canvas3D.Screenshot", "obj(obj)")`

**Phase 12 — SceneNode3D:**
- `SceneNode3D.AABBMin` / `SceneNode3D.AABBMax` → Vec3 — public bounding box getters for game logic
- `RT_FUNC(SceneNode3DGetAABBMin, rt_scene_node3d_get_aabb_min, "Viper.Graphics3D.SceneNode3D.get_AABBMin", "obj(obj)")`
- `RT_FUNC(SceneNode3DGetAABBMax, rt_scene_node3d_get_aabb_max, "Viper.Graphics3D.SceneNode3D.get_AABBMax", "obj(obj)")`

**Phase 14 — AnimPlayer3D:**
- `AnimPlayer3D.GetBoneMatrix(boneIndex)` → Mat4 — world-space bone transform for object attachment
- `RT_FUNC(AnimPlayer3DGetBoneMatrix, rt_anim_player3d_get_bone_matrix, "Viper.Graphics3D.AnimPlayer3D.GetBoneMatrix", "obj(obj,i64)")`

**Phase 17 — Particles3D:**
- `Particles3D.SetEmitterShape(shape)` — 0=point (default), 1=sphere, 2=box
- `Particles3D.SetEmitterSize(x, y, z)` — radius for sphere, half-extents for box
- `RT_FUNC(Particles3DSetEmitterShape, rt_particles3d_set_emitter_shape, "Viper.Graphics3D.Particles3D.SetEmitterShape", "void(obj,i64)")`
- `RT_FUNC(Particles3DSetEmitterSize, rt_particles3d_set_emitter_size, "Viper.Graphics3D.Particles3D.SetEmitterSize", "void(obj,f64,f64,f64)")`

## Cross-Phase Integration Notes

- **RenderTarget3D.AsPixels():** returns a NEW Pixels object each call (fresh copy of GPU/CPU color buffer). Caller owns the result. Modifying the Pixels does not affect the render target.
- **FBX tangent precedence:** if the FBX mesh includes pre-computed tangents, use them. If not, `CalcTangents()` must be called explicitly by the user. `CalcTangents()` always overwrites existing tangent data.
- **Scene3D.Draw() and Canvas3D Begin/End:** `Scene3D.Draw(canvas, camera)` calls `canvas.Begin(camera)` and `canvas.End()` internally. The user must NOT wrap `Scene3D.Draw` in their own `Begin`/`End` block.
- **Particles3D and Scene Graph:** Particles3D is standalone (world-space). To attach to a scene node, update `Particles3D.SetPosition()` each frame from the node's world position. No automatic attachment.
- **FBX bone ordering:** FBX loader must output bones in topological order (parent index < child index). `AnimPlayer3D.New(skeleton)` relies on this for correct bone palette computation.

