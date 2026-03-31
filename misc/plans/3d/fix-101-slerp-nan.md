# Fix 101: SLERP `acosf` Domain Error → NaN Animation Corruption

## Severity: P0 — Critical

## Problem

`quat_slerp_float()` in `rt_skeleton3d.c:256` calls `acosf(dot)` where `dot` is the
quaternion dot product. Floating-point rounding can produce `dot = 1.0000001f`, which
is outside `acosf`'s valid domain [-1, 1], causing it to return `NaN`. This propagates
through the entire bone transform chain, corrupting all animations.

## Prerequisites

None — `<math.h>` is already included, `fmaxf`/`fminf` are standard C99.

## Current Code (`rt_skeleton3d.c:241-256`)

```c
float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
// ... flip if negative ...
if (dot > 0.9995f) {
    // linear fallback
} else {
    float theta = acosf(dot);  // BUG: dot could be > 1.0
```

## Fix

Add one line before the `acosf` call:

```c
if (dot > 1.0f) dot = 1.0f;  // clamp for acosf domain safety
float theta = acosf(dot);
```

Or the more defensive full clamp: `dot = fmaxf(-1.0f, fminf(1.0f, dot));`

Note: `dot` is already forced positive by the flip at line 245, so only the upper
bound is needed. But clamping both sides is safer for future refactors.

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_skeleton3d.c` | Add clamp before `acosf` (1 line) |

## Documentation Update

None — internal fix, no API change.

## Test

Add to existing skeleton test or create `TestSkeletonSlerp`:
- Construct two nearly-identical quaternions whose dot product rounds to >1.0
  (e.g., `q1 = {0, 0, 0, 1}`, `q2 = {1e-7, 0, 0, 1}` — normalized dot ≈ 1.0)
- Call `quat_slerp_float(q1, q2, 0.5, out)` — verify `out` contains valid values (not NaN)
- Verify `isnan(out[0]) == false`
- Existing animation tests pass (regression)
