# D3D-18: Environment Reflections From CubeMap Materials

## Status

Implemented in the D3D11 material shader and cubemap binding path.

## Shipped

- reflective materials now consume `env_map` and `reflectivity` on D3D11
- the main pixel shader samples the cubemap from `t10` and blends it into the lit result
- reflective draws reuse the D3D-17 cubemap cache instead of uploading a separate cubemap per material

## Shared Runtime Note

- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) already forwards env-map payloads for regular draws
- [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) now forwards env maps and reflectivity correctly for instanced draws as well

## Validation

- payload forwarding is covered by [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c)
- [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)
