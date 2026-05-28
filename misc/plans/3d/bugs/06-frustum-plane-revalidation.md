# Plan 06 — Validate frustum planes once at extraction, not per object test

- **Severity:** Medium (performance)
- **Type:** Optimization (behavior-preserving)
- **Primary files:** `vgfx3d_frustum.c`, `vgfx3d_frustum.h`
- **Status:** Planned (do not code yet)

## Problem

`vgfx3d_frustum_test_aabb` and `vgfx3d_frustum_test_sphere` call `vgfx3d_frustum_plane_is_valid` for
**all six planes on every object tested** (`vgfx3d_frustum.c:171` and `:210`). `plane_is_valid`
(`53-64`) does four `isfinite` checks plus a length²-vs-epsilon test per plane — i.e. 6 validity
checks + ~6 length computations per object, repeated for every culled object in the frame.

But the planes are produced once by `vgfx3d_frustum_extract`, which **already** normalizes them and
falls back to a conservative all-zero frustum on bad input. Their validity is therefore fixed at
extraction time and does not change between object tests.

**Impact:** the hot callers test *many* volumes against *one* extracted frustum per frame —
terrain chunks (`rt_terrain3d.c:1009`), instanced batches (`rt_instbatch3d.c:180`), and scene objects
(`rt_scene3d.c:2027`). For a few thousand objects that's tens of thousands of redundant per-plane
validity recomputations.

## Investigation notes / nuances

- `vgfx3d_frustum_t` is currently just `float planes[6][4]` (`vgfx3d_frustum.h:29-31`) — no room to
  cache validity without adding a field.
- The struct is **internal** (`#ifdef VIPER_ENABLE_GRAPHICS`, backend dir) and used **by value on the
  stack** by all callers; it is not serialized or ABI-frozen. Adding a field is safe as long as all
  TUs recompile (they will). Confirmed callers: `rt_canvas3d.c:3834/1936`, `rt_terrain3d.c:978/1009`,
  `rt_instbatch3d.c:387/180`, `rt_scene3d.c:2851/2027`, and `test_vgfx3d_backend_utils.c:525`. **All
  obtain the frustum via `vgfx3d_frustum_extract`** — none hand-construct one — so a flag set during
  extract is always initialized before any test.
- Behavior must be preserved exactly: today, an invalid/degenerate frustum (conservative all-zero, or
  any non-finite plane) makes the tests return `1` (intersecting) — the safe "don't cull" answer.
  `vgfx3d_frustum_extract` already routes all bad input through `vgfx3d_frustum_make_conservative`
  (`82-90, 138-141`), so "valid" is a single frustum-wide property, not per-plane in practice.

## Proposed fix

1. Add `int8_t planes_valid;` to `vgfx3d_frustum_t`.
2. In `vgfx3d_frustum_extract`: set `planes_valid = 1` on the successful normalized path; have
   `vgfx3d_frustum_make_conservative` set `planes_valid = 0` (covers every fallback: null vp,
   non-finite entries, degenerate plane length).
3. In `vgfx3d_frustum_test_aabb` / `vgfx3d_frustum_test_sphere`: replace the per-plane
   `vgfx3d_frustum_plane_is_valid` loop check with a single early `if (!f->planes_valid) return 1;`
   at the top, then run the existing p-vertex/n-vertex (or sphere-distance) loop without per-plane
   revalidation.
4. Keep `vgfx3d_frustum_plane_is_valid` (still used by `extract`'s own logic if desired) but it's no
   longer called per object.

Defensive nicety: `make_conservative` should also be the path that zero-inits `planes_valid`, so a
frustum that somehow reaches a test without extraction is treated as conservative (return 1), not as
garbage.

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_frustum.h` (struct field)
- `src/runtime/graphics/3d/backend/vgfx3d_frustum.c` (extract sets flag; tests consult it)

## Tests

- `test_vgfx3d_backend_utils.c` already asserts the invalid-VP frustum classifies as intersecting
  (`525-526`); keep/extend that — it directly exercises the `planes_valid == 0` path.
- Add cases: fully-inside (2), intersecting (1), fully-outside (0) AABB and sphere against a normal
  extracted frustum, asserting unchanged results vs. current behavior.
- Build + `ctest --test-dir build -L graphics3d` (and the frustum/culling unit suite).

## Risk

Low. Single internal struct field + a short-circuit; all callers initialize via `extract`. The only
care item is ensuring **every** conservative/fallback exit in `extract` clears the flag — centralizing
that in `make_conservative` covers it.
