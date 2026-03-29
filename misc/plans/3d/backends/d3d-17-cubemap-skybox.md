# D3D-17: Backend-Owned CubeMap Skybox Rendering

## Status

Implemented in the D3D11 cubemap upload/cache path and dedicated skybox pass.

## Shipped

- Canvas3D now delegates skybox rendering to D3D11 through `draw_skybox()`
- D3D11 uploads cubemaps as `TEXTURECUBE` resources, caches them per frame, and renders a backend-owned cube skybox
- the skybox path works for both the onscreen framebuffer and GPU RTT targets

## Validation

- cubemap face unpacking is covered by [`src/tests/unit/test_vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/tests/unit/test_vgfx3d_backend_utils.c)
- shared skybox hook usage is covered by [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_utils.c)
- [`src/tests/unit/test_vgfx3d_backend_utils.c`](/Users/stephen/git/viper/src/tests/unit/test_vgfx3d_backend_utils.c)
- [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)
