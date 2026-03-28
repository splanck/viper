# Plan: Spot Lights + Orthographic Camera — COMPLETE

## 1. Spot Lights — DONE (2026-03-28)
- Added `rt_light3d_new_spot(position, direction, r, g, b, attenuation, innerAngle, outerAngle)` constructor
- Angles in degrees, converted to cosines internally for shader comparison
- Added `inner_cos` and `outer_cos` fields to both `rt_light3d` struct and `vgfx3d_light_params_t` backend params
- Software backend: smoothstep cone attenuation between outer and inner cosines
- GPU backends (Metal, D3D11, OpenGL): receive spot params via light_params struct; shader update needed for full GPU spot rendering (currently uses point light fallback on GPU)
- Light param copy in `build_light_params()` updated to pass cone angles

## 2. Orthographic Camera — DONE (2026-03-28)
- Added `rt_camera3d_new_ortho(size, aspect, near, far)` — size is half-height in world units
- Builds orthographic projection matrix: no perspective foreshortening
- Added `is_ortho` flag and `ortho_size` fields to camera struct
- Added `rt_camera3d_is_ortho()` query
- `LookAt`, `Orbit`, `Shake`, `SmoothFollow` all work unchanged (only projection differs)

## Files Changed
- `src/runtime/graphics/rt_light3d.c` — NewSpot constructor
- `src/runtime/graphics/rt_camera3d.c` — NewOrtho constructor, build_ortho helper, is_ortho query
- `src/runtime/graphics/rt_canvas3d_internal.h` — Spot fields on rt_light3d, ortho fields on rt_camera3d
- `src/runtime/graphics/vgfx3d_backend.h` — Spot fields on vgfx3d_light_params_t
- `src/runtime/graphics/vgfx3d_backend_sw.c` — Spot cone attenuation in software renderer
- `src/runtime/graphics/rt_canvas3d.c` — Copy spot params in build_light_params
- `src/runtime/graphics/rt_canvas3d.h` — Declarations
- `src/il/runtime/runtime.def` — RT_FUNC entries + class updates

## Tests Added
5 new tests in `test_rt_canvas3d.cpp`:
- `test_light_spot`: Creates spot light, verifies non-null
- `test_light_spot_intensity`: Set intensity on spot light
- `test_camera_ortho`: Creates ortho camera, verifies IsOrtho=true
- `test_camera_ortho_look_at`: LookAt on ortho camera preserves position
- `test_camera_perspective_not_ortho`: Perspective camera IsOrtho=false

Total: 67/67 canvas3d, 1358/1358 full suite.
