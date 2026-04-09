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

## 2026-04 Runtime Hardening Notes

The Windows backend was reworked further on 2026-04-09 to close the remaining correctness and performance gaps from the runtime audit:

- animation uploads:
  - bone palettes are now zero-padded into fixed 128-bone upload buffers instead of blindly reading past the caller payload
  - failed bone uploads now disable skinning flags for that draw instead of silently reusing stale constant-buffer contents
  - morph payloads now flow through stable `morph_key` / `morph_revision` identities and are cached on the backend side
- postfx and presentation:
  - window-backed scene rendering uses an HDR scene color target (`R16G16B16A16_FLOAT`) for the GPU postfx path
  - screen overlays no longer overwrite the scene-history inputs used by SSAO / DOF / motion blur
  - overlays render into a separate overlay target when GPU postfx is active, then composite after tonemap
  - when GPU postfx is not active, the backend now renders directly to the swapchain instead of forcing an unnecessary offscreen pass
- shader / cbuffer packing:
  - D3D11 `PerObject` morph weights and `PerMaterial` custom params now use explicitly packed `float4` arrays on both the C and HLSL sides
  - shared helper code now owns the packing, upload-status, frame-history, capacity-growth, and target-selection rules that need portable regression tests
- resource lifetime and quality:
  - instanced draws reuse persistent CPU staging storage instead of allocating and freeing per draw
  - imported textures and cubemaps now build full mip chains
  - texture and cubemap caches now grow dynamically instead of falling back to temporary D3D resources once the old fixed caps were reached

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
   - `test_rt_morphtarget3d`
   - `test_vgfx3d_backend_d3d11_shared`
   - `test_vgfx3d_backend_utils`
2. A Windows CI lane that builds `viper` plus the runtime test targets above and runs them on `windows-latest`

Local macOS/Linux builds still cannot validate D3D11 device creation or HLSL compilation directly, so Windows CI remains the authoritative backend gate.

## Remaining Follow-Up

The remaining work is no longer feature implementation. It is validation breadth:

- keep the Windows D3D11 CI lane green
- add dedicated device-level Windows renderer tests when practical
- preserve software fallback behavior when backend initialization fails

## Plan Mapping

The detailed per-feature notes remain in [`misc/plans/3d/backends`](/Users/stephen/git/viper/misc/plans/3d/backends), but D3D-01 through D3D-20 should now be treated as implemented status records rather than pending execution steps.
