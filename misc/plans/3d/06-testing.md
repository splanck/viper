# Phase 6: Comprehensive Testing

## Test Inventory

#### Software Renderer Tests (Phase 1)

| File | Tests | Description |
|------|-------|-------------|
| `RT3DRasterTests.cpp` | 15 | Triangle rasterizer: degenerate (zero-area), sub-pixel, large, off-screen, clipped, exact edge, single pixel, Y-flip correctness |
| `RT3DClipTests.cpp` | 12 | Frustum clipping: fully inside, fully outside, partial (each of 6 planes), multiple planes, degenerate after clip, attribute interpolation at clip edges |
| `RT3DVertexTests.cpp` | 12 | Transform pipeline: identity, translate, rotate, scale, perspective divide, viewport, combined MVP, Y-flip in viewport, NDC Z-range [-1,1] verified |
| `RT3DZBufferTests.cpp` | 10 | Depth: front occludes back, equal depth (first-write wins), clear resets to FLT_MAX, near/far precision, OpenGL NDC Z range |
| `RT3DMeshTests.cpp` | 18 | Mesh: empty, add vertices, add triangles, box generator (24 verts / 12 tris), sphere generator, plane generator, cylinder generator, OBJ load (valid/invalid/missing/quads), recalc normals, clone, transform, finalizer cleanup |
| `RT3DCameraTests.cpp` | 12 | Camera: perspective matrix matches Mat4.Perspective, lookAt matches Mat4.LookAt, orbit, screen-to-ray, get forward/right/up, FOV change updates projection, near/far validation |
| `RT3DMaterialTests.cpp` | 8 | Material: default values, color set, texture bind (Pixels reference), shininess range, unlit flag, NewColor factory, NewTextured factory |
| `RT3DLightTests.cpp` | 10 | Light: directional, point, ambient, intensity, attenuation, color, zero-length direction normalization, NewDirectional/NewPoint/NewAmbient factories, set after creation |
| `RT3DIntegrationTests.cpp` | 15 | Full pipeline: single triangle flat, cube wireframe, textured quad, lit sphere, multiple meshes, depth sorting, transparent blend, fog, billboard, DeltaTime nonzero, Backend property returns "software", backface cull toggling, 2D+3D coexistence |
| `RT3DObjLoaderTests.cpp` | 8 | OBJ parser: triangulated faces, quad auto-split, vertex/normal/UV all present, negative indices, comments, empty file, missing file, face formats (v, v/vt, v/vt/vn, v//vn) |

#### GPU Backend Tests (Phases 3-5)

| File | Platform | Tests | Description |
|------|----------|-------|-------------|
| `RT3DMetalTests.cpp` | macOS | 10 | Device creation, buffer upload, PSO compile, render pass, texture upload, MSAA, resize, present, cleanup, fallback |
| `RT3DD3D11Tests.cpp` | Windows | 10 | Device+swap chain, buffer create, shader compile, constant buffer map, texture SRV, MSAA, resize, present, cleanup, fallback |
| `RT3DOpenGLTests.cpp` | Linux | 10 | Context creation, GL loader, VAO/VBO, shader compile+link, texture upload, FBO MSAA, resize, present, cleanup, fallback |

#### Shared GPU-Path Runtime Tests (Current)

| File | Platform | Tests | Description |
|------|----------|-------|-------------|
| `test_rt_canvas3d_gpu_paths.cpp` | Cross-platform | 15 | Shared producer/runtime payload checks: GPU skinning bypass, GPU morph payloads, morph-normal payloads, env-map forwarding, backend skybox hook usage, previous-frame history propagation, and instanced material forwarding |
| `test_vgfx3d_backend_utils.c` | Cross-platform | 9 | Backend utility checks: RGBA unpack, cubemap-face unpack, row flip, inverse-transpose normal matrix fallback + correctness, and 4x4 inversion success/failure paths |

#### Current D3D11 Validation Path

The current practical DX11 validation path is:

1. Windows CI build compiles `viper`, `test_rt_canvas3d_gpu_paths`, and `test_vgfx3d_backend_utils`
2. Windows CI runs the focused shared graphics tests:
   - `test_rt_canvas3d_gpu_paths`
   - `test_vgfx3d_backend_utils`
3. local focused validation can use the same `ctest` entries directly:
   - `ctest --test-dir build --output-on-failure -R 'test_rt_canvas3d_gpu_paths|test_vgfx3d_backend_utils'`
4. device-level D3D11 renderer tests remain desirable, but they are not the current baseline gate

This keeps backend code in [`vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c) from landing entirely unvalidated when local development happens on macOS/Linux.

#### Cross-Backend Parity Tests

| File | Tests | Description |
|------|-------|-------------|
| `RT3DBackendParityTests.cpp` | 8 | Render identical scenes on software + GPU, compare output (epsilon tolerance for float rounding) |

#### Golden Tests

| File | Description |
|------|-------------|
| `graphics3d_wireframe_cube.bas` + `.out` | Wireframe cube, fixed camera, deterministic output |
| `graphics3d_flat_triangle.bas` + `.out` | Single flat-shaded triangle, known pixel positions |
| `graphics3d_gouraud_sphere.bas` + `.out` | Gouraud-shaded sphere with directional light |
| `graphics3d_textured_quad.bas` + `.out` | Textured quad with UV mapping |
| `graphics3d_depth_ordering.bas` + `.out` | Two overlapping meshes verifying Z-buffer |
| `graphics3d_camera_orbit.bas` + `.out` | Camera orbiting, fixed frame capture |

## CTest Registration

```cmake
# Phase 1: Software renderer tests
if (NOT TARGET test_rt_3d_raster)
    viper_add_test(test_rt_3d_raster ${VIPER_TESTS_DIR}/runtime/RT3DRasterTests.cpp)
    target_link_libraries(test_rt_3d_raster PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_3d_raster test_rt_3d_raster)
endif ()
# ... repeat for each test file

# GPU backend tests: platform-conditional
if (APPLE AND NOT TARGET test_rt_3d_metal)
    viper_add_test(test_rt_3d_metal ${VIPER_TESTS_DIR}/runtime/RT3DMetalTests.cpp)
    target_link_libraries(test_rt_3d_metal PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_3d_metal test_rt_3d_metal)
endif ()

# Labels: runtime, graphics3d, (platform-specific)
```

## Test Totals

| Category | Tests |
|----------|-------|
| Software renderer (10 files) | 120 |
| Metal backend | 10 |
| D3D11 backend | 10 |
| OpenGL backend | 10 |
| Backend parity | 8 |
| Golden tests (BASIC programs) | 6 |
| **Total** | **~164** |
