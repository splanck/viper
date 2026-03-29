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

Still missing or incomplete:

- Diffuse / normal / specular / emissive texture sampling
- Correct normal matrix
- Spot lights, fog, wireframe
- Render-to-texture
- Shadow mapping
- GPU post-processing
- Real OpenGL instancing
- Terrain splatting
- Efficient dynamic buffer reuse
- Full GPU skinning / GPU morph support

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

Some OpenGL feature plans require work outside the backend file:

- GPU post-processing:
  - [`rt_canvas3d_flip()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L993) currently always runs the CPU PostFX path before `backend->present()`. A shared handoff is required so GPU backends can own postfx presentation when enabled.
- GPU skinning:
  - [`rt_canvas3d_draw_mesh_skinned()`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c#L923) still CPU-skins vertices before enqueueing the draw. The OpenGL backend can consume `bone_palette`, but true GPU skinning also needs a producer-side bypass of that CPU pre-skin step.
- GPU morph targets:
  - `vgfx3d_draw_cmd_t` already has morph fields, but [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L722) still leaves them null. A producer path is required before the OpenGL morph shader path can be considered complete.
- GPU render-to-texture + skybox:
  - Canvas3D currently paints the skybox directly into the CPU render-target buffer when `render_target` is active. A GPU RTT implementation must not silently overwrite that behavior.

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

## Execution Principle

Implement against the current runtime, not against the older phase sketch. That means:

- keep the existing backend file unless a refactor is clearly warranted
- reuse the current Canvas3D scheduling and backend vtable
- document every shared prerequisite explicitly
- preserve the software fallback until each GPU feature is fully wired end to end
