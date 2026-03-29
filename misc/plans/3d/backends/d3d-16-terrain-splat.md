# D3D-16: Per-Pixel Terrain Splatting

## Status

Implemented in the D3D11 material path and pixel shader.

## Shipped

- D3D11 now consumes `has_splat`, the splat map, four splat layers, and per-layer UV scales from the shared terrain draw payload
- splat textures bind on `t5` through `t9` without colliding with the shadow or material texture slots
- degenerate splat weights fall back to the regular diffuse path instead of producing black terrain

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
