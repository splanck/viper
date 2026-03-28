# Plan: Memory Leak Fixes — COMPLETE

## Status: All items resolved or verified already fixed.

### ~~1. Water3D Mesh~~ — ALREADY FIXED (verified)
`rt_water3d.c:142-150` resets vertex/index counts in-place. No per-frame allocation.

### ~~2. Particles3D Material~~ — ALREADY FIXED (verified)
`rt_particles3d.c:674-680` caches material via null-check. Created once.

### 3. Mesh3D.Clear API — DONE (Plan 01)
Added `rt_mesh3d_clear()` to reset vertex/index counts without freeing backing arrays. Registered in runtime.def as `Mesh3D.Clear`. 4 tests added.

### 4. Sprite3D Per-Frame Allocation — DONE (Plan 01)
Cached mesh + material in sprite struct. Registered temp buffers with canvas. 6 tests added.

### ~~5. Skeleton Bone Palette Workspace~~ — ALREADY FIXED (verified)
`rt_skeleton3d.c:693` has `if (!p->globals_buf)` null-check — allocates once, reuses across frames.
`compute_inverse_bind` at line 334 is one-time setup, not per-frame.

### 6. Canvas3D Software Framebuffer Clear — OPTIMIZED (2026-03-28)
**Was:** Per-pixel loop writing 4 bytes per pixel (cr, cg, cb, 0xFF) in nested for-loops.
**Now:** uint32 writes — packs RGBA into single uint32 and writes one value per pixel. Same semantics, fewer memory stores. The clear is still needed (2D overlay functions use the software framebuffer) but is now ~4x faster at 1080p.

### Tests Added
- `test_mesh_clear_stress`: 100 clear-rebuild cycles simulating Water3D pattern (1 test)
- Previous Plan 01 tests: Mesh3D.Clear (3) + Sprite3D (6)

### Total: 62/62 canvas3d, 1358/1358 full suite.
