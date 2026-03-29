# D3D-03: Texture Cache + SRV Management

## Status

Implemented in [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c).

## Shipped

- `Pixels` textures are cached by raw object identity for the duration of the frame
- cache misses create `Texture2D + SRV` pairs once and reuse them across draws in that frame
- overflow falls back to temporary SRVs that are released after use instead of leaking
- all cached resources are released at frame boundaries and again at context teardown

## Notes

- the same conservative lifetime model is now used for D3D11 cubemaps as part of D3D-17 and D3D-18
- this keeps mutable runtime `Pixels` objects safe without adding version tracking

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`misc/plans/3d/backends/d3d-17-cubemap-skybox.md`](/Users/stephen/git/viper/misc/plans/3d/backends/d3d-17-cubemap-skybox.md)
