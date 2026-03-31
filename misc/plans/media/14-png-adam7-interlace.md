# Plan 14: PNG Adam7 Interlace Support

## Context

The PNG decoder currently ignores the interlace flag in the IHDR chunk (`chunk_data[12]`).
Adam7-interlaced PNGs fail to decode because the scanline layout doesn't match the expected
sequential row format. Interlaced PNGs are less common but still produced by some tools.

## Problem

Adam7 interlacing stores pixels in 7 passes, each covering a different subset of the image:

| Pass | Start Col | Col Step | Start Row | Row Step | Sub-image Size |
|------|-----------|----------|-----------|----------|----------------|
| 1 | 0 | 8 | 0 | 8 | ceil(w/8) × ceil(h/8) |
| 2 | 4 | 8 | 0 | 8 | ceil((w-4)/8) × ceil(h/8) |
| 3 | 0 | 4 | 4 | 8 | ceil(w/4) × ceil((h-4)/8) |
| 4 | 2 | 4 | 0 | 4 | ceil((w-2)/4) × ceil(h/4) |
| 5 | 0 | 2 | 2 | 4 | ceil(w/2) × ceil((h-2)/4) |
| 6 | 1 | 2 | 0 | 2 | ceil((w-1)/2) × ceil(h/2) |
| 7 | 0 | 1 | 1 | 2 | w × ceil((h-1)/2) |

The decompressed IDAT data contains 7 sub-images concatenated sequentially, each with its
own filter bytes (one per row of the sub-image).

## Approach

### Step 1: Read interlace flag from IHDR (~2 LOC)

In the IHDR parsing section of `rt_pixels_io.c` (around line 366):
```c
uint8_t interlace = chunk_data[12]; // 0=none, 1=Adam7
```

### Step 2: Handle interlaced decode (~80 LOC)

After DEFLATE decompression, instead of the single-pass scanline reconstruction, process
7 passes. Each pass produces a sub-image that is scattered into the full-size output.

```c
if (interlace == 1) {
    // Adam7 interlaced
    static const int adam7_x0[7] = {0, 4, 0, 2, 0, 1, 0};
    static const int adam7_dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const int adam7_y0[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int adam7_dy[7] = {8, 8, 8, 4, 4, 2, 2};

    size_t src_offset = 0;
    for (int pass = 0; pass < 7; pass++) {
        int sub_w = (width - adam7_x0[pass] + adam7_dx[pass] - 1) / adam7_dx[pass];
        int sub_h = (height - adam7_y0[pass] + adam7_dy[pass] - 1) / adam7_dy[pass];
        if (sub_w <= 0 || sub_h <= 0) continue;

        size_t sub_stride = sub_w * bpp_bytes;
        // Each sub-image row has a filter byte
        for (int y = 0; y < sub_h; y++) {
            uint8_t filter = raw_data[src_offset++];
            // Reconstruct filtered row (same 5 filter types)
            // ... apply filter using sub_stride as the row width ...
            // Scatter pixels into full image at (adam7_x0[pass] + x*adam7_dx[pass],
            //                                    adam7_y0[pass] + y*adam7_dy[pass])
        }
    }
} else {
    // Existing sequential decode (unchanged)
}
```

### Step 3: Refactor filter reconstruction

The existing filter reconstruction code (lines 536-555) needs to be callable for both
the full-image and sub-image cases. Extract it into a helper:

```c
static void png_reconstruct_row(uint8_t *dst, const uint8_t *src,
                                 const uint8_t *prev, size_t stride, int bpp);
```

This helper applies the filter byte and produces a reconstructed row. Called once per row
in the sequential case, and once per sub-image row in the interlaced case.

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels_io.c` | Read interlace flag, add Adam7 decode path, extract filter helper |

## Estimated LOC

~100 lines (Adam7 pass loop ~60, refactor filter into helper ~20, interlace flag reading ~2,
sub-image stride computation ~15).

## Verification

- Create a small interlaced PNG (e.g., 8×8 with Adam7) using an external tool
- Load it and verify pixel values match expected output
- Non-interlaced PNGs continue to work (regression)
- Existing `test_png_io` and `test_png_formats` tests pass
