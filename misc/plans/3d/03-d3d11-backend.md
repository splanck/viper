# Phase 4: Direct3D 11 GPU Backend (Windows)

## Goal

Bring the Windows D3D11 backend to parity with the current 3D runtime architecture and the shared GPU feature set.

That goal is now met in [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c).

## Implemented Scope

The backend now owns the full DX11 plan set:

- material/render-state parity:
  - diffuse, normal, specular, and emissive textures
  - vertex-color modulation
  - inverse-transpose normal matrix upload
  - spot lights, fog, wireframe, and cull toggle
- render targets and scene resources:
  - GPU render-to-texture with CPU readback into `RenderTarget3D`
  - shadow-map depth targets + comparison sampling
  - backend-owned cubemap skybox rendering
  - environment reflections from `Material3D.env_map` / `reflectivity`
- animation and batching:
  - GPU skeletal skinning
  - GPU morph target consumption
  - GPU morph normal-delta consumption
  - true hardware instancing with previous-frame instance matrices
- presentation:
  - fullscreen GPU post-processing path
  - advanced postfx support using depth/history payloads (SSAO, DOF, motion blur)
- backend foundation:
  - persistent dynamic vertex/index/instance buffers
  - HRESULT-checked resource creation/update paths
  - per-frame texture/cubemap caching

## Shared Runtime Dependencies Now Exercised End To End

The D3D11 backend now consumes the shared runtime hooks and payloads that were added across the 3D stack:

- `present_postfx` handoff from [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- `shadow_begin` / `shadow_draw` / `shadow_end` scheduling from [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- GPU skinning producer bypass from [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c)
- morph payload + morph-normal payload production from [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)
- instanced previous-transform and material-map forwarding from [`rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c)
- RTT ownership/readback flow from [`rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c)

## Validation

The active validation path is now:

1. Focused shared `ctest` coverage for GPU-path payloads and backend utility helpers:
   - `test_rt_canvas3d_gpu_paths`
   - `test_vgfx3d_backend_utils`
2. A Windows CI lane that builds `viper` plus those two test targets and runs them on `windows-latest`

Local macOS/Linux builds still cannot validate D3D11 device creation or HLSL compilation directly, so Windows CI remains the authoritative backend gate.

## Remaining Follow-Up

The remaining work is no longer feature implementation. It is validation breadth:

- keep the Windows D3D11 CI lane green
- add dedicated device-level Windows renderer tests when practical
- preserve software fallback behavior when backend initialization fails

## Plan Mapping

The detailed per-feature notes remain in [`misc/plans/3d/backends`](/Users/stephen/git/viper/misc/plans/3d/backends), but D3D-01 through D3D-20 should now be treated as implemented status records rather than pending execution steps.
