# Plan 01: JPEG Image Loading

## Goal

Add baseline JPEG decoding to `rt_pixels.c` so that `rt_pixels_load_jpeg(path)` returns
an RGBA Pixels object.  This enables loading photographs and web-sourced images without
requiring an external library.

## Scope

**In scope:**
- Baseline JPEG decoding (sequential, Huffman-coded DCT)
- 8-bit YCbCr 4:4:4 and 4:2:0 subsampling (covers ~95% of real-world JPEGs)
- Conversion from YCbCr to RGBA (alpha = 0xFF)
- Grayscale JPEG (single component)

**Out of scope:**
- Progressive JPEG (rare in game assets; can add later)
- Arithmetic coding (patent-encumbered, extremely rare)
- CMYK / YCCK color spaces
- EXIF orientation metadata (can add as a follow-up)
- Writing/saving JPEG files

## Background

Viper's image pipeline is entirely from-scratch (zero dependencies).  PNG and BMP are
already supported via custom decoders in `rt_pixels.c`.  JPEG is the most-requested
missing format — it's needed to load photographs, downloaded images, and assets exported
from tools that default to JPEG.

## Technical Design

### 1. File Structure

Add a new section in `rt_pixels.c` after the PNG decoder (~line 1096), following the same
pattern as `rt_pixels_load_png()`.

### 2. JPEG Decoding Pipeline

```
File bytes
  -> Marker parsing (SOI, APP0, DQT, SOF0, DHT, SOS, EOI)
  -> Huffman decode (DC/AC coefficients per 8x8 block)
  -> Inverse DCT (8x8 blocks)
  -> Dequantization
  -> YCbCr -> RGB conversion
  -> Chroma upsampling (for 4:2:0)
  -> Pack into 0xRRGGBBAA pixel format
```

### 3. Key Components (~1500 LOC total)

| Component | Est. LOC | Notes |
|-----------|----------|-------|
| Marker parser | ~200 | SOI/SOF0/DHT/DQT/SOS/EOI, skip APPn/COM |
| Huffman table builder | ~150 | Build decode trees from DHT segments |
| Huffman bitstream decoder | ~200 | Read variable-length codes, DC/AC zigzag |
| Inverse DCT (8x8) | ~150 | AAN fast IDCT algorithm (integer-only) |
| Dequantization + zigzag | ~80 | Apply quantization tables, zigzag reorder |
| Color conversion | ~100 | YCbCr->RGB with clamping, chroma upsample |
| Block assembly | ~150 | MCU iteration, interleaved scan handling |
| Integration / error handling | ~200 | File I/O, validation, cleanup on error |
| Header + public API | ~50 | `rt_pixels_load_jpeg()` declaration |

### 4. IDCT Strategy

Use the AAN (Arai, Agui, Nakajima) fast IDCT — it's the standard for software JPEG decoders.
All-integer implementation using 16-bit fixed-point arithmetic.  No floating-point required.

### 5. API

```c
// rt_pixels.h
void *rt_pixels_load_jpeg(void *path);
```

Follows the same signature as `rt_pixels_load_png()` and `rt_pixels_load_bmp()`.

### 6. Integration Points

- **rt_pixels.h**: Add declaration after `rt_pixels_load_png`
- **rt_pixels.c**: Add implementation after PNG section
- **rt_sprite.c:140**: Update `rt_sprite_from_file()` format dispatch (see Plan 03)
- **runtime.def**: Register `Pixels.LoadJpeg` if exposing to BASIC/Zia

### 7. Validation Approach

Standard JPEG test images:
- Grayscale 8-bit
- YCbCr 4:4:4 (no subsampling)
- YCbCr 4:2:0 (most common)
- Various sizes (1x1, odd dimensions, large)
- Restart markers (RST0-RST7)

Decode-then-compare against known pixel checksums (golden test).

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels.h` | Add `rt_pixels_load_jpeg` declaration |
| `src/runtime/graphics/rt_pixels.c` | Add ~1500 LOC JPEG decoder |
| `src/tests/CMakeLists.txt` | Add JPEG decode tests |

## Risks

- **Restart markers** add complexity to the bitstream reader (~50 extra LOC).
- **Non-standard JPEGs** (Photoshop, camera firmware) may have unusual marker ordering.
  Mitigate by being lenient in marker parsing (skip unknown markers gracefully).
- **Performance:** AAN integer IDCT is fast enough for asset loading.  Not optimized for
  real-time video decoding, but that's out of scope.
