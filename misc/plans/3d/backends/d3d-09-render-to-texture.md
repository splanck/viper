# D3D-09: Render-To-Texture

## Status

Implemented in the D3D11 backend resource ownership and frame-end readback path.

## Shipped

- `set_render_target()` now allocates and tears down D3D11 color, depth, and staging resources for `RenderTarget3D`
- `begin_frame()` selects the swap-chain target or RTT target with the correct viewport
- `end_frame()` copies GPU RTT color into the CPU-visible staging texture and populates `RenderTarget3D.AsPixels()`
- RTT works with the backend-owned cubemap skybox path instead of depending on the older CPU fallback

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c)
- [`misc/plans/3d/backends/d3d-17-cubemap-skybox.md`](/Users/stephen/git/viper/misc/plans/3d/backends/d3d-17-cubemap-skybox.md)
