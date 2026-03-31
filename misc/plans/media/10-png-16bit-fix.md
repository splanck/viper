# Plan 10: PNG 16-bit Sample Downscaling Fix

## Context

The PNG decoder handles 16-bit samples by taking only the high byte (`row[x * 2]`),
discarding the low byte entirely. This loses precision — a 16-bit value of `0x80FF` maps
to `0x80` instead of the more accurate `0x81` (with rounding).

## Problem

Six locations in `rt_pixels_io.c` read 16-bit big-endian samples and use only the MSB:
- Line 571: Grayscale 16-bit → `gray = row[x * 2]`
- Line 595-597: RGB 16-bit → `r = row[x * 6]`, `g = row[x * 6 + 2]`, `b = row[x * 6 + 4]`
- Line 626-627: Grayscale+Alpha 16-bit → `gray = row[x * 4]`, `alpha = row[x * 4 + 2]`
- Line 636-640: RGBA 16-bit → channels at x*8, x*8+2, x*8+4, x*8+6

## Fix

Replace each raw MSB read with a rounding downscale:

```c
// Before:
gray = row[x * 2];

// After:
uint16_t sample16 = ((uint16_t)row[x * 2] << 8) | row[x * 2 + 1];
gray = (uint8_t)((sample16 + 128) >> 8);  // round-to-nearest
```

The `+ 128` before the shift implements round-half-up, which is the standard approach for
16→8 bit downscaling (used by libpng, Photoshop, etc.).

### Helper Function

Add a static inline helper to avoid repeating the pattern:

```c
/// @brief Downscale a 16-bit big-endian sample to 8-bit with rounding.
static inline uint8_t png_down16(const uint8_t *p) {
    uint16_t v = ((uint16_t)p[0] << 8) | p[1];
    return (uint8_t)((v + 128) >> 8);
}
```

Then each site becomes a one-word replacement:
```c
gray = png_down16(row + x * 2);
r = png_down16(row + x * 6);
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels_io.c` | Add `png_down16()` helper, replace 6 sites |

## Estimated LOC

~10 lines net (add helper, shorten 6 call sites).

## Verification

- Create a 16-bit PNG with known values (e.g., 0x8080 → should map to 0x81, not 0x80)
- Round-trip: create 16-bit PNG externally, load via `rt_pixels_load_png`, verify pixel
  values are within ±1 of expected 8-bit values
- Existing `test_png_formats` test continues to pass
