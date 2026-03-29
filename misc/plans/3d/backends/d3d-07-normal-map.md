# D3D-07: Normal Map Sampling

## Status

Implemented in the D3D11 material binding path and lighting shader.

## Shipped

- tangents are consumed on the D3D11 GPU path and used to build a TBN basis
- normal maps bind at `t1` and are enabled only when the SRV exists
- degenerate tangent data falls back to the geometric normal path instead of reading undefined data

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
