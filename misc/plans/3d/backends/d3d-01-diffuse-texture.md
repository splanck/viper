# D3D-01: Diffuse Texture Sampling + Vertex Color Modulation

## Status

Implemented in [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c).

## Shipped

- the main D3D11 pixel shader samples diffuse textures from `t0` when present
- base color and alpha are built from material color, sampled texture, and per-vertex color together
- untextured draws clear the SRV slot so stale state does not leak across draws
- the same material path is used by both regular and instanced draws

## Notes

- per-vertex color parity is now aligned with the software and OpenGL paths
- texture upload and reuse are handled through the per-frame cache described in D3D-03

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`misc/plans/3d/backends/d3d-03-texture-cache.md`](/Users/stephen/git/viper/misc/plans/3d/backends/d3d-03-texture-cache.md)
