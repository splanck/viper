# Plan: P0 Crash Fixes — COMPLETE

## Status: All items resolved.

## 1. Canvas3D Division by Zero — ALREADY FIXED (verified)
Code at `rt_canvas3d.c:1017-1019` clamps: `if (cs < 0.001f) cs = 1.0f;`

## 2. Camera3D asin NaN — ALREADY FIXED (verified)
Code at `rt_camera3d.c:370` clamps: `asin(fmax(-1.0, fmin(1.0, fy)))`

## 3. Sprite3D Use-After-Free — FIXED (2026-03-27)
**What was wrong:** `rt_sprite3d.c` created a new Mesh3D + Material3D every frame in `rt_canvas3d_draw_sprite3d()`, passing them to the deferred draw queue without GC protection. If GC ran between draw and End(), the mesh/material could be freed.

**What was fixed:**
- Added `cached_mesh`, `cached_material`, `cached_texture` fields to sprite struct
- Mesh3D is created once and reused via new `rt_mesh3d_clear()` (resets vertex/index counts without freeing backing arrays)
- Material3D is created once and reused until texture changes
- Both are registered with `rt_canvas3d_add_temp_buffer()` each frame so the canvas holds a GC reference

**Also added:**
- `rt_mesh3d_clear()` — new API to reset mesh geometry without reallocation. Declared in `rt_canvas3d.h`, registered in `runtime.def` as `Mesh3D.Clear`.

**Files changed:**
- `src/runtime/graphics/rt_sprite3d.c` — Cached mesh/material, register temp buffers
- `src/runtime/graphics/rt_mesh3d.c` — Added `rt_mesh3d_clear()`
- `src/runtime/graphics/rt_canvas3d.h` — Declared `rt_mesh3d_clear()`
- `src/il/runtime/runtime.def` — Registered `Mesh3D.Clear` as RT_FUNC + RT_METHOD

**Tests added:** 9 new tests in `test_rt_canvas3d.cpp`:
- Mesh3D.Clear: reset counts, rebuild after clear, null safety
- Sprite3D: creation, null texture, set position/scale/frame, null safety

**Test count:** 61/61 in test_rt_canvas3d, 1358/1358 total suite.
