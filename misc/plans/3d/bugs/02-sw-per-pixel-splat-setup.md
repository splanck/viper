# Plan 02 — Hoist terrain-splat view setup out of the per-pixel loop

- **Severity:** High (performance)
- **Type:** Optimization (behavior-preserving)
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c`
- **Status:** Planned (do not code yet)

## Problem

In the software rasterizer's per-pixel inner loop, the terrain-splat branch re-resolves all five
splat texture views **for every covered pixel**:

```c
for (int y …) for (int x …) {
  ...
  if (cmd && cmd->has_splat && cmd->splat_map) {
    ...
    sw_pixels_view splat_view;
    sw_pixels_view layer_views[4];
    if (sw_setup_complete_splat(cmd, &splat_view, layer_views)) {   // per pixel!
      ...
    }
  }
}
```
(`vgfx3d_backend_sw.c:1489-1498`; `sw_setup_complete_splat` at `626-640`)

`sw_setup_complete_splat` validates `cmd->has_splat`, all five Pixels pointers, and calls
`setup_pixels_view` five times (each copies a struct + bounds-checks). These views are **identical for
every pixel of the draw** — they depend only on `cmd`, not on the pixel.

**Impact:** a full-screen terrain triangle covering ~1–2M pixels performs ~5–10M redundant
view-setup/validation operations per frame, on the CPU, in the hottest loop in the renderer.

## Investigation notes / nuances

- `raster_triangle` is a big static function (`vgfx3d_backend_sw.c:1322`) called once per (clipped)
  triangle from `sw_submit_draw`. The splat data is per **draw command** (`cmd`), so it is constant
  across all triangles of the draw too — setup could even live in `sw_submit_draw`.
- The cleanest minimal change keeps the resolved views local to `raster_triangle`: compute them once
  before the `for(y)` loop, then reference them per pixel. This avoids touching the function
  signature and other call sites.
- Edge case: when `sw_setup_complete_splat` returns 0 (incomplete splat), today the per-pixel branch
  simply does nothing and falls through to the normal diffuse. Hoisting must preserve that: resolve
  once into a `int have_splat` flag + the views; in the pixel loop, gate on `have_splat` instead of
  re-calling setup. Behavior (and output pixels) stay identical.
- The per-pixel splat math that *does* legitimately vary (sampling `splat_view`/`layer_views` at the
  per-pixel UV, weight normalization, layer blend) stays in the loop unchanged.

## Proposed fix

In `raster_triangle`, before the rasterization `for (y …)` loop:

1. Add `sw_pixels_view splat_view; sw_pixels_view layer_views[4]; int have_splat = 0;`
2. `if (cmd && cmd->has_splat && cmd->splat_map) have_splat = sw_setup_complete_splat(cmd, &splat_view, layer_views);`
3. In the per-pixel branch, replace the `cmd->has_splat && cmd->splat_map` test + inner
   `sw_setup_complete_splat` call with `if (have_splat) { … }`, using the pre-resolved views.

No signature change; the resolved views are read-only during rasterization.

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c` — only.

## Tests

- A terrain-splat visual probe (software backend) frame must match its baseline byte-for-pixel —
  this is a pure hoist. If no dedicated splat probe exists, add a minimal one that draws a splatted
  quad and compares a handful of sampled pixels.
- Build + `ctest --test-dir build -L graphics3d`.

## Risk

Very low. Behavior-preserving hoist of loop-invariant setup; the only state added is stack-local.
