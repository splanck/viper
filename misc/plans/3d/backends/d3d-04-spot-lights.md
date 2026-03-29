# D3D-04: Spot Light Cone Attenuation

## Status

Implemented in the D3D11 light constant-buffer layout and main lighting shader.

## Shipped

- D3D11 now consumes `inner_cos` and `outer_cos` from the shared light payload
- spot lights use distance attenuation plus smooth cone falloff instead of falling through the ambient branch
- directional and point-light behavior remains unchanged

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
