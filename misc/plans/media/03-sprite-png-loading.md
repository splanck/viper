# Plan 03: Sprite PNG Loading

## Goal

Update `rt_sprite_from_file()` to support PNG files in addition to BMP, using the existing
`rt_pixels_load_png()` decoder.

## Scope

**In scope:**
- Detect file format by extension or magic bytes
- Load PNG files via `rt_pixels_load_png()`
- Load BMP files via `rt_pixels_load_bmp()` (existing behavior)
- Load JPEG files via `rt_pixels_load_jpeg()` (if Plan 01 is completed)

**Out of scope:**
- Animated sprites from GIF/APNG (separate feature)
- Sprite sheet parsing (already handled by `rt_spritesheet.h`)

## Background

`rt_sprite_from_file()` at `src/runtime/graphics/rt_sprite.c:136-153` currently only calls
`rt_pixels_load_bmp(path)`.  Since Viper already has a working PNG decoder, sprites should
be loadable from PNG — this is especially important because PNG supports transparency
(alpha channel), which is critical for game sprites.

## Technical Design

### 1. Format Detection (~20 LOC)

Add a helper that detects the image format from the file's magic bytes:

```c
/// @brief Detect image format from file magic bytes.
/// @return 1=BMP, 2=PNG, 3=JPEG, 0=unknown
static int detect_image_format(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return 0;
    uint8_t magic[8];
    size_t n = fread(magic, 1, 8, f);
    fclose(f);
    if (n < 2) return 0;
    if (magic[0] == 'B' && magic[1] == 'M') return 1;           // BMP
    if (n >= 8 && magic[0] == 137 && magic[1] == 80 &&
        magic[2] == 78 && magic[3] == 71) return 2;              // PNG
    if (magic[0] == 0xFF && magic[1] == 0xD8) return 3;          // JPEG
    return 0;
}
```

### 2. Update `rt_sprite_from_file()` (~10 LOC)

Replace the current direct `rt_pixels_load_bmp` call with format dispatch:

```c
void *rt_sprite_from_file(void *path) {
    if (!path) return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath) return NULL;

    int fmt = detect_image_format(filepath);
    void *pixels = NULL;
    switch (fmt) {
        case 1: pixels = rt_pixels_load_bmp(path); break;
        case 2: pixels = rt_pixels_load_png(path); break;
        // case 3: pixels = rt_pixels_load_jpeg(path); break;  // After Plan 01
        default: return NULL;
    }
    if (!pixels) return NULL;

    rt_sprite_impl *sprite = sprite_alloc();
    if (!sprite) return NULL;
    sprite->frames[0] = pixels;
    sprite->frame_count = 1;
    rt_heap_retain(pixels);
    return sprite;
}
```

### 3. Include Guards

Add `#include "rt_pixels.h"` to `rt_sprite.c` if not already present (for the
`rt_pixels_load_png` declaration).

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_sprite.c` | Add format detection + dispatch (~30 LOC) |

## Test Plan

- Load a PNG sprite and verify dimensions match
- Load a BMP sprite (regression — existing behavior preserved)
- Load a non-existent file (returns NULL)
- Load an unsupported format (returns NULL)
- Load a PNG with transparency and verify alpha channel is preserved

## Risks

- Minimal.  This is primarily a wiring change between two existing, tested subsystems.
- The `detect_image_format` helper opens the file briefly for magic-byte sniffing, then
  the loader re-opens it.  This double-open is acceptable for asset loading (not a hot path).
