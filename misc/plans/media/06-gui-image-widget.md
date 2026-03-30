# Plan 06: GUI Image Widget File Loading

## Goal

Implement `vg_image_load_file()` in the GUI image widget so that it can load image files
directly from disk using Viper's existing decoders.

## Scope

**In scope:**
- Wire `vg_image_load_file()` to `rt_pixels_load_png()` / `rt_pixels_load_bmp()`
- Auto-detect format via magic bytes
- Convert from rt_pixels RGBA format to the widget's expected pixel layout
- JPEG support (if Plan 01 is completed)

**Out of scope:**
- Image scaling/resizing within the widget (already handled by rendering)
- Animated image support
- Network image loading (fetch-from-URL)

## Background

`vg_image_load_file()` at `src/lib/gui/src/widgets/vg_image.c:62-70` is currently a stub
that returns `false`.  The comment says "Image file loading requires a decode library
(e.g. stb_image)."  But Viper already has working PNG and BMP decoders in `rt_pixels.c` —
they just need to be connected.

The widget accepts pre-decoded RGBA data via `vg_image_set_pixels()` (line 38), which
expects `uint8_t*` in RGBA byte order.  The rt_pixels system uses `uint32_t` packed as
`0xRRGGBBAA`.  A simple byte-order conversion bridges the two.

## Technical Design

### 1. Add Format Detection (~10 LOC)

Reuse the same magic-byte detection approach from Plan 03:

```c
static int detect_format(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t hdr[8];
    size_t n = fread(hdr, 1, 8, f);
    fclose(f);
    if (n >= 2 && hdr[0] == 'B' && hdr[1] == 'M') return 1;
    if (n >= 4 && hdr[0] == 137 && hdr[1] == 80 &&
        hdr[2] == 78 && hdr[3] == 71) return 2;
    if (n >= 2 && hdr[0] == 0xFF && hdr[1] == 0xD8) return 3;
    return 0;
}
```

### 2. Implement `vg_image_load_file()` (~25 LOC)

```c
bool vg_image_load_file(vg_image_t *image, const char *path) {
    if (!image || !path) return false;

    // Create a runtime string for the rt_pixels API
    rt_string rts = rt_str_from_cstr(path);
    if (!rts) return false;

    int fmt = detect_format(path);
    void *pixels = NULL;
    switch (fmt) {
        case 1: pixels = rt_pixels_load_bmp(rts); break;
        case 2: pixels = rt_pixels_load_png(rts); break;
        // case 3: pixels = rt_pixels_load_jpeg(rts); break;
        default: break;
    }
    rt_str_release_maybe(rts);
    if (!pixels) return false;

    int w = (int)rt_pixels_width(pixels);
    int h = (int)rt_pixels_height(pixels);

    // Convert from packed 0xRRGGBBAA (uint32_t) to byte RGBA (uint8_t*)
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) { /* free pixels */ return false; }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t px = (uint32_t)rt_pixels_get(pixels, x, y);
            size_t off = ((size_t)y * w + x) * 4;
            rgba[off + 0] = (px >> 24) & 0xFF; // R
            rgba[off + 1] = (px >> 16) & 0xFF; // G
            rgba[off + 2] = (px >> 8)  & 0xFF; // B
            rgba[off + 3] = (px)       & 0xFF; // A
        }
    }

    vg_image_set_pixels(image, rgba, w, h);
    free(rgba);
    // Free the rt_pixels object (via heap release)
    rt_pixels_destroy(pixels);

    return true;
}
```

### 3. Include Dependencies

The `vg_image.c` file needs access to the rt_pixels API.  Add:
```c
#include "rt_pixels.h"
#include "rt_string.h"
```

If the GUI library doesn't currently link against the runtime graphics module, the
CMakeLists.txt may need an additional link dependency.

### 4. Optimization Note

The pixel-by-pixel `rt_pixels_get()` loop is O(w*h) with function-call overhead per pixel.
For a faster path, access the internal pixel buffer directly:

```c
const uint32_t *buf = rt_pixels_buffer(pixels); // if such an accessor exists
```

If no direct buffer accessor exists, add one to `rt_pixels.h`:
```c
const uint32_t *rt_pixels_raw_buffer(void *pixels);
```

This avoids per-pixel bounds checking and function call overhead.

## Files to Modify

| File | Change |
|------|--------|
| `src/lib/gui/src/widgets/vg_image.c` | Implement `vg_image_load_file()` (~25 LOC) |
| `src/runtime/graphics/rt_pixels.h` | Add `rt_pixels_raw_buffer()` accessor (optional) |
| `src/runtime/graphics/rt_pixels.c` | Implement `rt_pixels_raw_buffer()` (~5 LOC, optional) |
| `src/lib/gui/CMakeLists.txt` | Add link dependency on rt_pixels (if needed) |

## Test Plan

- Load a PNG file into a GUI image widget, verify dimensions
- Load a BMP file into a GUI image widget
- Load a non-existent file (returns false, widget unchanged)
- Load an invalid/corrupt file (returns false)
- Verify pixel data matches: load same file via `rt_pixels_load_png`, compare RGBA values

## Risks

- **Link dependency cycle:** The GUI library (`vg_*`) may not currently depend on the
  runtime graphics module (`rt_pixels`).  If a circular dependency exists, extract the
  image decoding into a shared utility or use a callback/function-pointer approach.
- **rt_string bridging:** The GUI uses C strings (`const char*`) while rt_pixels uses
  runtime strings (`rt_string`).  Need `rt_str_from_cstr()` for conversion — verify this
  function exists and is accessible from the GUI library.
