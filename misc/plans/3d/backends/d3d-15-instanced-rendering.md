# D3D-15: Hardware Instanced Rendering

## Status

Implemented through `submit_draw_instanced()` and a dedicated D3D11 instance stream.

## Shipped

- D3D11 now uses `DrawIndexedInstanced()` instead of replaying per-instance draws
- the instance stream carries current model matrices, inverse-transpose normal matrices, and previous model matrices
- instanced lighting stays correct under non-uniform scale because the backend computes per-instance normal matrices explicitly

## Shared Runtime

- [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) already routes batches through the backend hook and now forwards material maps plus previous-frame instance matrices correctly

## Validation

- history and instanced material payload forwarding are covered by [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c)
