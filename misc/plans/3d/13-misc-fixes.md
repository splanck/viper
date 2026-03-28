# Plan: Miscellaneous 3D Fixes

## Overview
After verification, several originally reported issues are already correct. Remaining confirmed items listed below.

## ~~1. Perspective Matrix Inconsistency~~ — ALREADY CONSISTENT
**Verified:** Both `rt_mat4_perspective()` and Camera3D's `build_perspective()` use identical formula and index placement.

## ~~2. Friction Not Applied~~ — ALREADY IMPLEMENTED
**Verified:** Lines 192-218 of rt_physics3d.c contain full Coulomb friction.

## ~~3. set_static(false) Bug~~ — ALREADY CORRECT
**Verified:** Lines 540-551 properly restore `inv_mass = 1.0/mass`.

## Confirmed Remaining Fixes

### 4. OBJ Vertex Deduplication (GFX-047)
**File:** `src/runtime/graphics/rt_mesh3d.c` OBJ loader section
**Issue:** Each face vertex creates a new mesh vertex even if an identical (position, normal, UV) combination already exists.
**Fix:** Build hash map of `(pos_idx, norm_idx, uv_idx) → vertex_idx`. Before adding vertex, check map. Expected 2-4x vertex reduction for typical models.

### 5. Normal Transform (GFX-037)
**File:** `src/runtime/graphics/rt_mesh3d.c:257`
**Issue:** `rt_mesh3d_transform` applies matrix directly to normals. Non-uniform scaling produces wrong normals.
**Fix:** Compute inverse-transpose of upper-left 3x3:
```c
mat3 normal_mat = transpose(inverse(extract_mat3(transform)));
for each vertex: normal = normalize(normal_mat * normal);
```

### 6. Particle PRNG Isolation (GFX-051 — NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_particles3d.c`
**Status:** Original report says global `prng_state`. Needs verification — may have been fixed with the material caching fix.
**Fix if confirmed:** Move PRNG state into emitter struct. Seed from emitter pointer or user-provided seed.

### 7. Canvas3D Font Duplication (GFX-043 — NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_canvas3d.c`
**Status:** Original report says font table duplicated in two functions. Low priority code quality fix.
**Fix if confirmed:** Extract to single `static const` array at file scope.

### 8. Shadow Resource Leak (GFX-074 — NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_canvas3d.c` finalizer
**Status:** Original report says shadow render target not freed in finalizer.
**Fix if confirmed:** Add `if (c->shadow_rt) { ... free ... }` in Canvas3D finalizer.

### 9. InstanceBatch3D Light Parameter Bug (MISC-3 — NEEDS VERIFICATION)
**File:** `src/runtime/graphics/rt_instbatch3d.c:180`
**Status:** Original report says pointer array passed as struct array for lighting. Needs verification.
**Fix if confirmed:** Convert `rt_light3d*` array to `vgfx3d_light_params_t` array before passing to backend.

## Verdict
2 confirmed fixes (OBJ dedup, normal transform). 4 items need targeted verification before implementing. All are P2 code quality, not crash-level.
