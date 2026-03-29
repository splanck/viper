# D3D-06: Linear Distance Fog

## Status

Implemented in the D3D11 per-scene constant buffer and main pixel shader.

## Shipped

- camera fog settings are stored during `begin_frame()`
- fog color, near, and far values are uploaded with the scene constants
- the final fog blend is applied after lit or unlit shading so unlit materials do not bypass fog

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
