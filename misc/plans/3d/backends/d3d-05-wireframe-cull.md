# D3D-05: Wireframe Mode + Backface Culling Toggle

## Status

Implemented in [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c).

## Shipped

- D3D11 now pre-creates solid and wireframe rasterizer variants for cull and no-cull modes
- regular draws and instanced draws both select the correct rasterizer state from the incoming flags
- the backend keeps the existing winding convention while honoring `wireframe` and `backface_cull`

## Notes

- the skybox pass uses its own no-cull state, so this plan does not regress cubemap background rendering

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`misc/plans/3d/backends/d3d-17-cubemap-skybox.md`](/Users/stephen/git/viper/misc/plans/3d/backends/d3d-17-cubemap-skybox.md)
