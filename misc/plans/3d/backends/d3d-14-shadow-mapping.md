# D3D-14: Shadow Mapping

## Status

Implemented through the backend shadow hooks and main-pass shadow sampling.

## Shipped

- D3D11 now implements `shadow_begin`, `shadow_draw`, and `shadow_end`
- the backend creates a shader-readable shadow depth texture plus comparison sampler
- directional lighting samples the shadow map during the main pass and attenuates direct light
- shadow state restores back to the main window or RTT target so the rest of the frame continues normally

## Notes

- the slot layout keeps the shadow map at `t4`, leaving terrain-splat textures on `t5` through `t9`

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
