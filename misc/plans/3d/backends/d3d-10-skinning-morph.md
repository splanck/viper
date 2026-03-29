# D3D-10: GPU Skinning + Morph Targets

## Status

Implemented end to end across the shared runtime producers and the D3D11 backend.

## Shipped

- D3D11 consumes bone palettes, previous bone palettes, morph deltas, morph weights, and previous morph weights
- the vertex shader applies morph deformation before skinning and preserves previous-frame data for later postfx history use
- the backend guards the GPU skinning path with an explicit bone-count limit and falls back cleanly when the payload exceeds it

## Shared Runtime

- [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c) now bypasses CPU skinning for GPU-capable backends
- [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c) packs morph payloads for GPU consumption
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) forwards those payloads into draw commands

## Validation

- shared payload propagation is covered by [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c)
- [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`misc/plans/3d/backends/d3d-19-morph-normal-deltas.md`](/Users/stephen/git/viper/misc/plans/3d/backends/d3d-19-morph-normal-deltas.md)
