# Viper 3D Engine Roadmap — Next Features

## Current State

18 phases complete, FPS demo working, 9 demos compile natively, 1334 tests passing. The rendering foundation is comprehensive. The gaps are in physics, gameplay systems, rendering optimization, and developer ergonomics.

## Phase Summary

| Phase | Features | Est. LOC | Impact | Enables |
|-------|----------|----------|--------|---------|
| **A** | Physics3D, Triggers, Math | 1,200 | **Critical** | Real gameplay, not floating cameras |
| **B** | Shadows, Fog, Gizmos | 600 | **High** | Professional visual quality |
| **C** | Transform3D, Camera, Paths | 500 | **High** | Developer productivity, polish |
| **D** | Instancing, Terrain | 800 | **High** | Large scenes, open-world |
| **E** | NavMesh, Anim Blending | 600 | **High** | Enemy AI, smooth animation |
| **F** | LOD, Decals, SSAO, etc. | 2,000+ | **Medium** | AAA polish |

**Recommended order: A → B → C → D → E → F**

## Parallelization

- **Phase A** must be first (Physics3D is foundation for gameplay)
- **Phase B and C** can be implemented in parallel (no cross-dependencies)
- **Phase D** can start after B (instancing uses shader infrastructure)
- **Phase E** depends on A (NavMesh needs Physics for ground queries)
- **Phase F** features are independent of each other

## Common Requirements (Apply to ALL Phases)

### Cross-Platform
Every feature must work on all 3 backends + software rasterizer:
- **Metal** (macOS): MSL shader changes, buffer binding
- **D3D11** (Windows): HLSL shader changes, cbuffer updates
- **OpenGL** (Linux): GLSL shader changes, uniform updates
- **Software**: CPU-side implementation in `vgfx3d_backend_sw.c`
- All shader changes must be explicit code (not "add same uniform")

### Documentation Updates (per phase)
- Update `docs/graphics3d-guide.md` with new API sections
- Update `docs/graphics3d-architecture.md` with pipeline changes
- Update file headers on modified files

### Registration Checklist (per phase)
1. Add `RTCLS_*` to `RuntimeClasses.hpp` (before closing `}`)
2. Add `RT_FUNC` + `RT_CLASS` to `runtime.def`
3. Add `#include "rt_newtype.h"` to `RuntimeSignatures.cpp`
4. Add `graphics/rt_newtype.c` to `src/runtime/CMakeLists.txt`
5. Add stubs to `rt_graphics_stubs.c`
6. Add test to `src/tests/unit/CMakeLists.txt`:
   ```cmake
   viper_add_test(test_name ${VIPER_TESTS_DIR}/unit/TestFile.cpp)
   target_link_libraries(test_name PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
   viper_add_ctest(test_name test_name)
   ```

### Feature Toggles
No new compile-time feature toggles. All 3D features compile under `VIPER_ENABLE_GRAPHICS` (existing flag). Non-graphics builds get stubs via `rt_graphics_stubs.c`.

### Verification (per phase)
1. `cmake --build build -j` — zero warnings
2. `ctest --test-dir build` — all tests pass
3. `./scripts/check_runtime_completeness.sh` — passes
4. `viper build demo.zia -o /tmp/demo` — native compilation works
5. All existing 9+ demos still work (no regression)
6. Documentation updated

### Known Cross-Cutting Issues to Resolve Before Implementation
1. **`vgfx3d_transform_aabb` type mismatch**: Takes `double[16]` but Phase D needs `float[16]`. Add a `vgfx3d_transform_aabb_f` float variant in `vgfx3d_frustum.c`.
2. **Raycast layer filtering**: Phase A's `rt_ray3d_intersect_mesh` should accept an optional layer mask parameter to filter which bodies/meshes are tested.
3. **Performance overlay**: Add `Canvas3D.GetFrameStats()` returning draw call count + triangle count (simple, can go in any phase).
