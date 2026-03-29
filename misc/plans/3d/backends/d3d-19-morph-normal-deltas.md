# D3D-19: GPU Morph Normal-Delta Parity

## Status

Implemented in the D3D11 morph buffer path and vertex shader.

## Shipped

- D3D11 now uploads morph normal deltas separately from morph position deltas
- the vertex shader applies weighted normal deltas when present and falls back cleanly when the payload is absent
- the morph layout remains compatible with previous-frame weights and bone palettes used by D3D-20 motion blur

## Validation

- shared D3D11 morph-normal payload forwarding is covered by [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)
- [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)
