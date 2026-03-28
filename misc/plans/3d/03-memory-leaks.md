# Plan: Memory Leak Fixes

## Overview
After code verification: Water3D and Particles3D already cache properly. The remaining confirmed items are smaller than originally scoped.

## ~~1. Water3D Mesh~~ — ALREADY FIXED
**Verified:** `rt_water3d.c:142-150` resets `vertex_count = 0; index_count = 0;` and rebuilds in-place. No per-frame allocation.

## ~~2. Particles3D Material~~ — ALREADY FIXED
**Verified:** `rt_particles3d.c:674-680` checks `if (!ps->cached_material)` and creates once. Properly cached.

## 3. Add rt_mesh3d_clear() API (CONFIRMED MISSING)
**File:** `src/runtime/graphics/rt_mesh3d.c`
**Issue:** No way to reset a mesh's vertex/index data without creating a new mesh. Water3D works around this by directly setting counts to 0, but this isn't exposed to Zia programs.
**Fix:** Add:
```c
void rt_mesh3d_clear(void *mesh) {
    rt_mesh3d_impl *m = (rt_mesh3d_impl *)mesh;
    if (!m) return;
    m->vertex_count = 0;
    m->index_count = 0;
}
```
Register in runtime.def as `Mesh3D.Clear()`.

## 4. Sprite3D Per-Frame Allocation (NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_sprite3d.c`
**Status:** The original report flagged per-frame mesh/material creation. Needs closer inspection of the actual draw path to confirm whether caching exists.
**Fix if confirmed:** Add `cached_mesh` and `cached_material` fields to sprite struct, rebuild only on frame/texture change.

## 5. Skeleton Bone Palette Workspace (NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_skeleton3d.c:759`
**Status:** The original report flagged per-frame malloc in `compute_bone_palette`. Needs verification — may already use stack allocation or pre-allocated buffer.
**Fix if confirmed:** Pre-allocate workspace in AnimPlayer3D struct at creation time.

## 6. Canvas3D Double Clear (NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_canvas3d.c:232-262`
**Status:** Needs verification — GPU backends may have a valid reason for the software buffer clear (e.g., screenshot support).
**Fix if confirmed:** Guard software buffer clear with `if (backend == software)`.

## Verdict
3 of 5 original items already fixed. Remaining items need targeted verification before implementing.
