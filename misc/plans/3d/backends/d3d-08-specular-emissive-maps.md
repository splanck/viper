# D3D-08: Specular + Emissive Map Sampling

## Status

Implemented in the D3D11 material constant buffer and texture binding path.

## Shipped

- specular maps bind at `t2` and modulate specular color per texel
- emissive maps bind at `t3` and add emissive contribution after direct lighting
- material flags are derived from the SRVs actually bound for the draw, which avoids stale-state mismatches

## Shared Runtime Note

- instanced material forwarding in [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) now passes specular color, emissive maps, and alpha correctly to the backend

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c)
