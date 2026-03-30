# Plan 02: PNG Indexed Color and Grayscale Support

## Goal

Extend the existing PNG decoder in `rt_pixels.c` to support all standard PNG color types,
not just RGB (type 2) and RGBA (type 6).  This fixes the most common real-world PNG loading
failures.

## Scope

**In scope:**
- Color type 0: Grayscale (1/2/4/8/16-bit)
- Color type 3: Indexed color (palette-based, 1/2/4/8-bit)
- Color type 4: Grayscale + alpha (8/16-bit)
- 16-bit depth support for types 0, 2, 4, 6 (downscale to 8-bit)
- tRNS chunk for palette transparency and grayscale key color

**Out of scope:**
- Interlaced PNG (Adam7) — rare in game assets, significant complexity
- Animated PNG (APNG)
- Color space chunks (sRGB, iCCP, cHRM, gAMA) — correct rendering but not critical

## Background

The current PNG decoder (`rt_pixels.c:606-837`) only accepts `color_type == 2` (RGB) or
`color_type == 6` (RGBA) at `bit_depth == 8`.  This rejects many real-world PNGs:

- **Indexed PNGs** (type 3) are extremely common — they're the default export from tools
  like Aseprite, GraphicsGale, and many web optimizers.
- **Grayscale PNGs** (type 0) are common for masks, heightmaps, and UI elements.
- **16-bit PNGs** (types 0/2/4/6 at depth 16) are used in scientific imaging and some
  game pipelines.

## Technical Design

### 1. Modify Existing Decoder (~200 LOC net addition)

The existing decoder already handles IHDR parsing, IDAT accumulation, DEFLATE
decompression, and filter reconstruction.  The changes are localized to two areas:

#### A. Relax the color_type/bit_depth check (line 668)

Current:
```c
if (bit_depth != 8 || (color_type != 2 && color_type != 6)) {
    // reject
}
```

New: Accept all valid combinations per PNG spec.

#### B. Add PLTE chunk parsing (~30 LOC)

In the chunk loop (line 654), add handling for `PLTE` chunk:
```c
} else if (memcmp(chunk_type, "PLTE", 4) == 0) {
    // Read palette entries (3 bytes each: R, G, B)
    palette_count = chunk_len / 3;
    memcpy(palette, chunk_data, chunk_len);
}
```

#### C. Add tRNS chunk parsing (~20 LOC)

```c
} else if (memcmp(chunk_type, "tRNS", 4) == 0) {
    // For indexed: per-entry alpha values
    // For grayscale: single transparent key color
    // For RGB: single transparent key color (R, G, B)
}
```

#### D. Expand scanline-to-RGBA conversion (~120 LOC)

After filter reconstruction, replace the current RGB/RGBA copy with a switch on
color_type:

| Type | Bytes/pixel | Conversion |
|------|-------------|------------|
| 0 (gray) | 1 (or sub-byte) | `R=G=B=gray, A=0xFF` (or tRNS key) |
| 2 (RGB) | 3 | Existing code (add tRNS key check) |
| 3 (indexed) | 1 (or sub-byte) | `RGBA = palette[index]` with tRNS alpha |
| 4 (gray+A) | 2 | `R=G=B=gray, A=alpha` |
| 6 (RGBA) | 4 | Existing code |

#### E. Handle sub-byte depths (1/2/4-bit for types 0 and 3) (~30 LOC)

For bit depths < 8, each byte contains multiple pixels packed left-to-right.
Unpack with bit shifting before the color conversion step.

#### F. Handle 16-bit depth (~20 LOC)

For bit depths of 16, each sample is 2 bytes (big-endian).  Downscale to 8-bit
by taking the high byte: `sample8 = sample16 >> 8`.

### 2. Filter Stride Adjustment

The existing filter reconstruction uses `bpp` (bytes per pixel) for the Sub/Average/Paeth
filters.  This already works correctly — just need to compute `bpp` based on the new
color types:

| Type | Depth 8 bpp | Depth 16 bpp | Depth <8 bpp |
|------|-------------|-------------|-------------|
| 0 | 1 | 2 | 1 |
| 2 | 3 | 6 | N/A |
| 3 | 1 | N/A | 1 |
| 4 | 2 | 4 | N/A |
| 6 | 4 | 8 | N/A |

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels.c` | Extend `rt_pixels_load_png()` (~200 LOC) |
| `src/tests/CMakeLists.txt` | Add PNG format variant tests |

## Test Plan

Test images (generate programmatically in test harness):
- 8-bit grayscale (type 0)
- 1-bit, 2-bit, 4-bit indexed (type 3) with palette
- 8-bit indexed with tRNS transparency
- Grayscale + alpha (type 4)
- 16-bit RGB (type 2, depth 16)
- 16-bit RGBA (type 6, depth 16)

Golden test: decode and verify pixel checksum matches expected RGBA output.

## Risks

- **Sub-byte unpacking** for 1/2/4-bit depths requires careful bit arithmetic and
  correct row padding (rows are byte-aligned after the filter byte).
- **tRNS interaction** with indexed color requires matching each palette index against
  the tRNS array length (indices beyond tRNS length have alpha=255).
