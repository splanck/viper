# Phase 4: Direct3D 11 GPU Backend (Windows)

## Goal

Bring the existing Windows D3D11 backend to parity with the current 3D runtime architecture. This is not a greenfield backend plan anymore: [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c) already creates a device and swap chain, compiles HLSL at runtime, uploads per-draw buffers, and renders a basic lit path.

The remaining work is expanding that backend to match the shared runtime and the feature set already present in software and partially present in Metal.

## Current Baseline

Already implemented:

- `D3D11CreateDeviceAndSwapChain`
- runtime HLSL compilation via `D3DCompile`
- one vertex shader + one pixel shader
- depth test, alpha blending, back-buffer presentation
- basic directional / point lighting
- row-major matrix upload convention
- clip-space Z remap from OpenGL-style `[-1, 1]` to D3D `[0, 1]`

Still missing or incomplete:

- diffuse / normal / specular / emissive texture sampling
- vertex-color modulation
- correct normal matrix
- spot lights, fog, wireframe, two-sided control
- render-to-texture
- shadow mapping
- GPU post-processing
- real hardware instancing
- terrain splatting
- efficient dynamic vertex/index buffers
- full GPU skinning / GPU morph support

## Important Corrections To The Older Sketch

The older phase note should not be used as the implementation spec:

- the implementation lives in [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c), not in `src/lib/graphics/...`
- shaders are compiled at runtime from a C string today; the plan does not require a separate `.hlsl` file or build-time `fxc` step
- there is already a working backend abstraction and a functioning D3D11 backend; this phase is an expansion plan, not an initial bootstrap
- the D3D NDC depth remap is already implemented in the vertex shader and must remain in place unless the matrix convention changes globally

## Shared Runtime Prerequisites

Some D3D11 feature plans require work outside the backend file:

- GPU post-processing:
  - [`rt_canvas3d_flip()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L993) still always applies CPU PostFX before `backend->present()`
- GPU skinning:
  - [`rt_canvas3d_draw_mesh_skinned()`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c#L923) still CPU-skins before enqueueing
- GPU morph targets:
  - draw-command morph fields exist, but [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L722) does not populate them yet
- GPU render-to-texture + skybox:
  - Canvas3D currently writes the skybox directly into the CPU render-target buffer when `render_target` is active

## Backend-Local Work

The detailed implementation work is split across the D3D11 backend plan set in [`misc/plans/3d/backends`](/Users/stephen/git/viper/misc/plans/3d/backends):

1. `D3D-01` through `D3D-08`: material and render-state parity
2. `D3D-09`: render-to-texture and readback ownership
3. `D3D-10`: GPU skinning and morph consumption
4. `D3D-11`: GPU post-processing
5. `D3D-12`: persistent dynamic buffers
6. `D3D-13`: HRESULT / failure-path rigor
7. `D3D-14`: shadow mapping
8. `D3D-15`: true hardware instancing
9. `D3D-16`: terrain splatting

## Execution Principle

Implement against the current runtime, not against the earlier architecture sketch:

- keep the existing backend file unless a refactor is clearly justified
- reuse the current Canvas3D scheduling and backend vtable
- document shared prerequisites explicitly
- preserve software fallbacks until each D3D11 feature is wired end to end
