# D3D-02: Fix Normal Matrix Bug

## Status

Implemented in the shared backend helpers and consumed by the D3D11 backend.

## Shipped

- D3D11 now uploads the true inverse-transpose normal matrix instead of reusing the model matrix
- singular transforms fall back cleanly instead of producing NaNs
- the same normal-matrix logic is reused for regular draws and instanced draws

## Validation

- helper behavior is covered by [`src/tests/unit/test_vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/tests/unit/test_vgfx3d_backend_utils.c)
- D3D11 consumes that logic from [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Files

- [`src/runtime/graphics/vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_utils.c)
- [`src/runtime/graphics/vgfx3d_backend_utils.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_utils.h)
- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/tests/unit/test_vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/tests/unit/test_vgfx3d_backend_utils.c)
