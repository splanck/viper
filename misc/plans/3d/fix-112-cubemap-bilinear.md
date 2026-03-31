# Fix 112: Cubemap Bilinear Filtering

## Severity: P2 — Medium

## Problem

Cubemap sampling (`rt_cubemap3d.c:153-162`) uses nearest-neighbor lookup — UV coordinates
are truncated to integer pixel positions. This produces blocky skyboxes and banded
environment reflections, especially visible at low cubemap resolutions.

```c
int px = (int)(u * (float)fw);
int py = (int)(v * (float)fh);
// ... clamp ...
int64_t pixel = rt_pixels_get(face_pixels, px, py);  // single texel
```

## Fix

Replace with bilinear interpolation between the 4 nearest texels:

```c
float fx = u * (float)fw - 0.5f;
float fy = v * (float)fh - 0.5f;
int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
int x1 = x0 + 1, y1 = y0 + 1;
float sx = fx - (float)x0, sy = fy - (float)y0;

// Clamp
x0 = x0 < 0 ? 0 : (x0 >= fw ? fw-1 : x0);
x1 = x1 < 0 ? 0 : (x1 >= fw ? fw-1 : x1);
y0 = y0 < 0 ? 0 : (y0 >= fh ? fh-1 : y0);
y1 = y1 < 0 ? 0 : (y1 >= fh ? fh-1 : y1);

// Sample 4 texels
uint32_t p00 = rt_pixels_get(face, x0, y0);
uint32_t p10 = rt_pixels_get(face, x1, y0);
uint32_t p01 = rt_pixels_get(face, x0, y1);
uint32_t p11 = rt_pixels_get(face, x1, y1);

// Bilinear blend per channel
uint8_t r = (uint8_t)(lerp(lerp(R(p00), R(p10), sx), lerp(R(p01), R(p11), sx), sy));
// ... same for G, B, A ...
```

This follows the same pattern used in the terrain splat texture sampler and the
bilinear resize in `rt_pixels_transform.c`.

## File to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_cubemap3d.c` | Replace nearest with bilinear sampling (~40 LOC) |

## Test

- Existing cubemap tests pass
- Visual: low-resolution cubemap (64x64) — verify smooth gradients instead of blocky pixels
