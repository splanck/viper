# Plan 11: Pixels.Load() Auto-Detect Function

## Context

Users must currently call format-specific loaders (`Pixels.LoadBmp`, `Pixels.LoadPng`,
`Pixels.LoadJpeg`, `Pixels.LoadGif`). The sprite loader (`Sprite.FromFile`) already
auto-detects format via magic bytes, but the `Pixels` API doesn't offer this convenience.

## Goal

Add `Pixels.Load(path)` that detects format from magic bytes and dispatches to the correct
loader. Falls back gracefully — tries each format in order, returns NULL if none match.

## Implementation

### New Function in `rt_pixels_io.c`

```c
void *rt_pixels_load(void *path) {
    if (!path) return NULL;
    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath) return NULL;

    // Read magic bytes
    FILE *f = fopen(filepath, "rb");
    if (!f) return NULL;
    uint8_t hdr[8];
    size_t n = fread(hdr, 1, 8, f);
    fclose(f);

    // Dispatch by magic
    if (n >= 8 && hdr[0] == 137 && hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G')
        return rt_pixels_load_png(path);
    if (n >= 2 && hdr[0] == 0xFF && hdr[1] == 0xD8)
        return rt_pixels_load_jpeg(path);
    if (n >= 2 && hdr[0] == 'B' && hdr[1] == 'M')
        return rt_pixels_load_bmp(path);
    if (n >= 3 && hdr[0] == 'G' && hdr[1] == 'I' && hdr[2] == 'F')
        return rt_pixels_load_gif(path);
    return NULL;
}
```

### Header Declaration in `rt_pixels.h`

```c
/// @brief Load an image from a file path, auto-detecting format from magic bytes.
/// @details Supports BMP, PNG, JPEG, and GIF. Returns first frame for animated GIFs.
/// @param path File path (runtime string).
/// @return New Pixels object, or NULL on failure.
void *rt_pixels_load(void *path);
```

### Runtime Registration in `runtime.def`

```
RT_FUNC(PixelsLoad, rt_pixels_load, "Viper.Graphics.Pixels.Load", "obj(str)")
```

And add to the Pixels class methods:
```
RT_METHOD("Load", "obj(str)", PixelsLoad)
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels_io.c` | Add `rt_pixels_load()` (~20 LOC) |
| `src/runtime/graphics/rt_pixels.h` | Add declaration |
| `src/il/runtime/runtime.def` | Add RT_FUNC + RT_METHOD entries |
| `docs/viperlib/graphics/pixels.md` | Add `Load(path)` to method table |

## Verification

- `Pixels.Load("test.png")` returns same result as `Pixels.LoadPng("test.png")`
- `Pixels.Load("test.bmp")` returns same result as `Pixels.LoadBmp("test.bmp")`
- `Pixels.Load("nonexistent")` returns NULL
- `Pixels.Load("garbage_file")` returns NULL
