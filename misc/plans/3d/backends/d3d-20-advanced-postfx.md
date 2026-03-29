# D3D-20: Depth/History-Based GPU PostFX

## Status

Implemented on top of the D3D11 fullscreen postfx pipeline.

## Shipped

- the scene path now preserves shader-readable depth plus a motion/velocity target for post-processing
- `present_postfx` consumes SSAO, depth-of-field, and motion-blur settings from the shared PostFX snapshot
- motion blur uses object history when available and falls back to camera reprojection only when history is missing
- previous-frame model, skinning, morph, and instancing data all feed the velocity path without changing the shared draw ABI again

## Notes

- the advanced passes extend the D3D-11 fullscreen composite rather than replacing it, so bloom, tone mapping, color grade, vignette, and FXAA remain part of the final chain
- RTT output stays defined as scene-color output rather than a postfx composite

## Validation

- shared history payload forwarding is covered by [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)
- device-level D3D11 validation still depends on the focused Windows CI lane

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h)
- [`src/tests/unit/test_rt_canvas3d_gpu_paths.cpp`](/Users/stephen/git/viper/src/tests/unit/test_rt_canvas3d_gpu_paths.cpp)
