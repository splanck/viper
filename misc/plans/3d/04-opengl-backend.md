# Phase 5: OpenGL 3.3 Core GPU Backend (Linux)

## Goal

Bring the existing Linux OpenGL backend to feature parity with the current 3D runtime architecture. This is no longer a greenfield backend plan: [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c) already creates a GLX context, compiles GLSL, uploads per-draw vertex/index buffers, and renders a basic lit path.

The work now is closing the parity gap with software and Metal without fighting the current runtime structure.

## Current Baseline

Already implemented in the OpenGL backend:

- Custom `dlopen`/`glXGetProcAddress` loader embedded in the backend file
- GLX context creation on the existing X11 window
- One GLSL program with basic lighting
- Depth test, blending, backface culling toggle
- Deferred draw replay through [`vgfx3d_backend_t`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h)

Implemented as of 2026-03-28:

- Diffuse / normal / specular / emissive texture sampling
- Vertex-color modulation
- Correct inverse-transpose normal matrix
- Spot lights, fog, wireframe
- Render-to-texture with FBO ownership + CPU readback
- Shadow mapping
- GPU post-processing presentation hook
- Hardware instancing
- Terrain splatting
- Persistent dynamic mesh/index/instance/morph buffers
- Full OpenGL shader consumption for GPU skinning / GPU morph payloads
- Backend-owned cubemap skybox rendering
- Material environment reflections
- GPU morph normal-delta parity
- Depth/history-based GPU postfx: SSAO, depth of field, velocity-buffer motion blur

## Important Corrections To The Older Sketch

The earlier phase note is stale in several ways and should not be used as the implementation spec:

- The implementation lives in [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c), not in `src/lib/graphics/...`.
- There is already a backend abstraction and a functioning OpenGL backend. This phase is an expansion plan, not a new backend bootstrap.
- The current backend does not use UBOs, separate shader source files, or a standalone loader module. None of those are prerequisites for parity.
- The runtime already contains shared 3D infrastructure that the OpenGL plans must reuse:
  - draw-command fields for textures, terrain splat data, bone palettes, and morph payloads
  - the optional instanced backend hook
  - Canvas3D shadow-pass scheduling
  - backend-facing PostFX snapshot export
  - render-target binding through [`rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c)

## Shared Runtime Prerequisites

These shared prerequisites are now implemented:

- GPU post-processing:
  - [`rt_canvas3d_flip()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L1001) now hands a PostFX snapshot to backends through `present_postfx` and skips the CPU path when the backend owns presentation.
- GPU skinning:
  - [`rt_canvas3d_draw_mesh_skinned()`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c#L934) now bypasses CPU pre-skinning for GPU-capable backends and forwards the bone palette through the draw command.
- GPU morph targets:
  - [`rt_canvas3d_draw_mesh_morphed()`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c#L251) now packs morph payloads for GPU-capable backends, and [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L729) propagates them into `vgfx3d_draw_cmd_t`.
- GPU morph normal deltas:
  - [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c) now packs optional normal-delta payloads for GPU-capable backends, and [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L729) forwards them through `vgfx3d_draw_cmd_t`.
- GPU render-to-texture + skybox:
  - [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L772) now skips the CPU skybox write when a GPU backend owns an active render target, preventing the RTT readback path from silently clobbering the buffer.
- Material environment reflection payloads:
  - [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L664) now forwards `env_map` and `reflectivity` through `vgfx3d_draw_cmd_t`.
- Backend-owned skybox pass:
  - [`rt_canvas3d_end()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L762) now delegates skybox rendering through the optional backend `draw_skybox` hook when available.
- Advanced GPU postfx snapshot:
  - [`vgfx3d_postfx_get_snapshot()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c) now exports SSAO, DOF, and motion-blur parameters for GPU backends.
- Motion-vector prerequisites:
  - [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c), [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c), [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c), and [`rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) now preserve previous-frame state so OpenGL can generate per-pixel motion vectors instead of relying on camera reprojection alone.

OpenGL follow-on status:

- OGL-17 through OGL-20 are now implemented in the Linux backend.
- OGL-20 now includes a velocity-buffer motion-blur path backed by shared previous-frame model, instancing, skinning, and morph history where available.

## Backend-Local Work

The detailed implementation work is split across the OpenGL backend plan set in [`misc/plans/3d/backends`](/Users/stephen/git/viper/misc/plans/3d/backends):

1. `OGL-01` through `OGL-09`: correctness and material parity
2. `OGL-10`: render-to-texture and FBO ownership
3. `OGL-11`: GPU skinning and morph consumption
4. `OGL-12`: shadow mapping
5. `OGL-13`: GPU post-processing
6. `OGL-14`: persistent dynamic buffers
7. `OGL-15`: real hardware instancing
8. `OGL-16`: terrain splatting
9. `OGL-17`: backend-owned cubemap skybox rendering
10. `OGL-18`: material environment reflections
11. `OGL-19`: GPU morph normal-delta parity
12. `OGL-20`: depth/history-based GPU postfx

## Execution Principle

This phase is now implemented against the current runtime, not against the older phase sketch. The delivered shape is:

- keep the existing backend file unless a refactor is clearly warranted
- reuse the current Canvas3D scheduling and backend vtable
- document every shared prerequisite explicitly
- preserve the software fallback until each GPU feature is fully wired end to end

## Validation

Focused runtime coverage added for this implementation:

- [`test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp) validates the shared GPU skinning / morph producer paths, previous-frame motion-history propagation, and their CPU fallbacks.
- [`test_vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/tests/unit/test_vgfx3d_backend_utils.c) validates pixel unpacking, RTT row-flip, and inverse-transpose normal-matrix helpers.
- [`test_rt_postfx3d_snapshot.c`](/Users/stephen/git/viper/src/tests/unit/test_rt_postfx3d_snapshot.c) validates advanced GPU PostFX snapshot export for SSAO, DOF, and motion blur.
