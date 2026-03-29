# D3D-11: GPU Post-Processing Pipeline

## Depends On

- D3D-09
- shared flip/present handoff

## Current State

The runtime already exports a backend-facing PostFX snapshot via [`vgfx3d_postfx_get_snapshot()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h#L65). The missing piece is not just the D3D11 fullscreen-quad pass, but also shared presentation ownership:

- [`rt_canvas3d_flip()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L993) still always applies the CPU PostFX path before calling `backend->present()`

As written, that prevents a D3D11 GPU postfx path from owning the final image end to end.

## Required Shared Prerequisite

Before D3D-11 can be finished, the runtime must define one of these shared mechanisms:

1. add a backend hook that receives the PostFX snapshot and owns GPU postfx presentation, or
2. extend `present()` so Canvas3D can pass the PostFX snapshot into it

Either way, `rt_canvas3d_flip()` must skip the CPU postfx path when the active backend is handling postfx on the GPU.

## D3D11 Backend Scope

v1 should implement:

- bloom
- tone mapping
- FXAA
- color grade
- vignette

Do not expand this plan to SSAO, DOF, or motion blur without adding the required depth/history inputs.

## Rendering Model

For onscreen rendering with GPU postfx enabled:

1. render the scene into an offscreen color target
2. in `present()`, bind the swap-chain RTV
3. draw a fullscreen quad
4. present the swap chain

For `RenderTarget3D`:

- do not apply GPU postfx unless the product requirement explicitly changes
- preserve the current behavior where RTT readback represents the scene render, not an implicit postfx composite

## D3D11 Implementation

Add:

- offscreen color texture + RTV + SRV
- postfx vertex shader / pixel shader
- postfx constant buffer
- cached shader/resource state for the fullscreen pass

Use `SV_VertexID` for the fullscreen quad so no vertex buffer is required.

Remember to unbind the offscreen SRV after the postfx pass to avoid the standard D3D11 RTV/SRV hazard.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- shared runtime change in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)

## Done When

- GPU postfx owns the onscreen final image
- CPU postfx is skipped for the D3D11 path when GPU postfx is active
- RTT output remains well-defined
