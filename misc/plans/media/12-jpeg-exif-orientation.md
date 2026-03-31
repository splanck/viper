# Plan 12: JPEG EXIF Orientation

## Context

Camera phones and DSLRs embed an EXIF orientation tag in JPEG files that indicates how the
image should be rotated/flipped for correct display. The Viper JPEG decoder skips all APP
markers (`rt_pixels_io.c:1611-1614`), so photos may load sideways or upside-down.

## Problem

EXIF orientation is stored in APP1 marker (`0xFFE1`) as a TIFF IFD entry with tag `0x0112`.
8 possible values:

| Value | Transform |
|-------|-----------|
| 1 | None (identity) |
| 2 | Flip horizontal |
| 3 | Rotate 180° |
| 4 | Flip vertical |
| 5 | Transpose (rotate 90° CW + flip horizontal) |
| 6 | Rotate 90° CW |
| 7 | Transverse (rotate 90° CCW + flip horizontal) |
| 8 | Rotate 90° CCW |

Values 1-4 are common. Values 5-8 are rare (some Samsung phones use 6).

## Approach

### Step 1: Parse EXIF orientation during JPEG decode (~80 LOC)

In the JPEG marker switch (`rt_pixels_io.c:1352`), add a case for `0xFFE1`:

```c
case 0xFFE1: { // APP1 — EXIF
    if (data_len >= 14 && memcmp(ctx.data + seg_start, "Exif\0\0", 6) == 0) {
        exif_orientation = parse_exif_orientation(ctx.data + seg_start + 6, data_len - 6);
    }
    ctx.pos = seg_start + data_len;
    break;
}
```

### Step 2: EXIF parser (~60 LOC)

Parse the TIFF header (byte order marker, IFD offset), then scan IFD0 entries for tag
`0x0112` (Orientation). Return the uint16 value (1-8).

```c
static int parse_exif_orientation(const uint8_t *tiff, size_t len) {
    if (len < 8) return 1;
    int big_endian = (tiff[0] == 'M' && tiff[1] == 'M');
    // Read IFD offset (bytes 4-7)
    uint32_t ifd_offset = read_u32(tiff + 4, big_endian);
    if (ifd_offset + 2 > len) return 1;
    // Read IFD entry count
    uint16_t count = read_u16(tiff + ifd_offset, big_endian);
    // Scan entries for tag 0x0112
    for (int i = 0; i < count; i++) {
        size_t entry = ifd_offset + 2 + i * 12;
        if (entry + 12 > len) break;
        uint16_t tag = read_u16(tiff + entry, big_endian);
        if (tag == 0x0112) {
            return (int)read_u16(tiff + entry + 8, big_endian);
        }
    }
    return 1; // default: no rotation
}
```

### Step 3: Apply transform after decode (~30 LOC)

After the JPEG decode loop produces the Pixels object, apply the orientation transform
using the existing `rt_pixels_transform.c` functions:

```c
if (exif_orientation != 1 && pixels) {
    void *rotated = NULL;
    switch (exif_orientation) {
        case 2: rotated = rt_pixels_flip_h(pixels); break;
        case 3: rotated = rt_pixels_rotate_180(pixels); break;
        case 4: rotated = rt_pixels_flip_v(pixels); break;
        case 6: rotated = rt_pixels_rotate_cw(pixels); break;
        case 8: rotated = rt_pixels_rotate_ccw(pixels); break;
        // Cases 5, 7: transpose — rotate + flip (rare)
        case 5: { void *t = rt_pixels_rotate_cw(pixels); rotated = rt_pixels_flip_h(t); free(t); break; }
        case 7: { void *t = rt_pixels_rotate_ccw(pixels); rotated = rt_pixels_flip_h(t); free(t); break; }
    }
    if (rotated) {
        // Release original, return rotated
        rt_obj_release_check0(pixels); rt_obj_free(pixels);
        pixels = rotated;
    }
}
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels_io.c` | Add EXIF parser + APP1 case + post-decode transform |

## Estimated LOC

~150 lines total (parser ~60, marker case ~20, transform application ~30, helpers ~40).

## Verification

- JPEG without EXIF: no change in behavior (orientation defaults to 1)
- JPEG with orientation 6 (90° CW from camera): loads upright
- JPEG with orientation 3 (upside-down): loads correct way up
- Existing `test_jpeg_decode` tests continue to pass
