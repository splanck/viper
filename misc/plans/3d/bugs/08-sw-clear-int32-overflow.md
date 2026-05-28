# Plan 08 — Use size_t for software-backend clear/depth-reset loop counts

- **Severity:** Low (correctness / robustness — only at impractically large render targets)
- **Type:** Defensive correctness + allocation/iteration consistency
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c`
- **Status:** Planned (do not code yet)

## Problem

The depth-buffer clear/reset loops compute the pixel count as a 32-bit `int`:

```c
int32_t total = rt->width * rt->height;     // sw_clear (RTT)          : 2627
int32_t total = ctx->width * ctx->height;   // sw_clear (window)       : 2645
int32_t total = rt->width * rt->height;     // sw_begin_frame (RTT)    : 2678
int32_t total = ctx->width * ctx->height;   // sw_begin_frame (window) : 2683
for (int32_t i = 0; i < total; i++) depth_buf[i] = FLT_MAX;
```

Meanwhile the buffers themselves are allocated with **overflow-checked `size_t`** math
(`sw_ensure_zbuf_capacity`, `vgfx3d_backend_sw.c:219-223`, and the render-target allocator). So there
is an **inconsistency**: a width×height that exceeds `INT32_MAX` allocates a correctly-sized buffer
but the clear computes a wrapped/negative `total`, leaving the depth buffer **partially or entirely
uncleared** → stale depth → rendering artifacts (not a crash, not OOB write).

## Investigation notes / nuances

- Overflow requires ~`46341 × 46341` pixels (a ~8.5 GB depth buffer), so this is **not practically
  reachable** today — it's a robustness/consistency fix, not an exploitable bug. Document it as such;
  don't oversell severity.
- The nested **color**-clear loops (`for y<height { for x<width }`) use `int32_t` `x`/`y` counters,
  which are individually fine, but the index expression `y * rt->stride + x * 4` is also `int` math
  and would overflow at the same scale. Switching the index to `size_t` arithmetic there is the
  consistent companion change (optional, same root cause).
- The allocation path is the source of truth: it already rejects `pixel_count > SIZE_MAX/sizeof(float)`
  and (for RTT) is bounded; the clear should simply iterate the same `size_t` count.

## Proposed fix

In all four sites, compute the count as `size_t` and iterate with a `size_t` index:

```c
size_t total = (size_t)rt->width * (size_t)rt->height;
for (size_t i = 0; i < total; i++) rt->depth_buf[i] = FLT_MAX;
```

Optionally, make the color-clear pixel-address arithmetic `size_t` for the same reason. Keep the
existing `width/height <= 0` guards (the RTT/window paths already ensure positive dimensions before
reaching these loops via `ensure_*`/`vgfx_get_framebuffer`).

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c` — only.

## Tests

- Functional behavior at normal sizes is unchanged; existing graphics3d probes cover that.
- Optional targeted unit test for `sw_clear` semantics at a normal RTT size (depth all `FLT_MAX`,
  color all the clear color) to lock in behavior. A genuine >INT32_MAX test is impractical (memory),
  so rely on code review + the consistency argument.
- Build + `ctest --test-dir build -L graphics3d`.

## Risk

Very low. Pure type widening to match the allocation; no behavior change at any realistic resolution.
