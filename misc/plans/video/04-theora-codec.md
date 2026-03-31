# Plan 04: Theora Video Codec (Decode Only)

## Problem

MJPEG provides video playback but with poor compression (1:20 ratio).
Real video content needs inter-frame compression for practical file sizes.
Theora is the simplest royalty-free codec with real compression (~1:100+).

## Goal

From-scratch Theora decoder conforming to the Theora I specification
(https://www.theora.org/doc/Theora.pdf). Decode-only — no encoder.
Integrates with VideoPlayer via OGG container (reuses `rt_ogg.c`).

## Zero External Dependencies

Clean-room implementation from the public Theora specification PDF.
No reference to libtheora source code.

## Why Theora

- **Godot Engine precedent** — Godot uses OGG Theora as its ONLY built-in
  video format, for the same licensing reasons
- **Royalty-free** — BSD license, no patent pools
- **Reuses OGG + Vorbis** — container parser and audio decoder already exist
- **Simpler than H.264** — no CABAC, no B-frames, no multiple reference
  frames, no complex deblocking filter

---

## Theora Codec Overview

Theora is a block-based DCT codec descended from VP3:

- **Frame types:** Intra (I-frame) and Inter (P-frame) only — no B-frames
- **Block size:** 8x8 pixels (same as JPEG)
- **Color space:** YCbCr 4:2:0 (half-resolution chroma)
- **Entropy coding:** Huffman (not arithmetic) — simpler than H.264's CABAC
- **Motion compensation:** Block-level, half-pixel precision
- **Loop filter:** Simple in-loop deblocking (4-tap filter)
- **Superblocks:** 4x4 groups of 8x8 blocks (32x32 pixel regions)

## Implementation Components

### 1. Header Parsing (~200 LOC)

Three mandatory headers in OGG packets (identified by first byte):
- `0x80` — Identification header: version, frame size, pixel aspect, FPS,
  color space, quality hint
- `0x81` — Comment header: vendor string + key=value metadata (reuse
  Vorbis comment parser pattern)
- `0x82` — Setup header: loop filter limits, quantization matrices (64
  entries × 3 planes × 64 quality levels), Huffman tables (80 tables)

### 2. Frame Header Decode (~100 LOC)

Per-frame: frame type (I/P), quality index, block coding modes.
For I-frames: all blocks are intra-coded.
For P-frames: coded/uncoded block map, motion vectors.

### 3. Block Coding Modes (~200 LOC)

8 coding modes for inter blocks:
- Mode 0: inter, no motion (copy from reference)
- Mode 1: intra (code block independently, like JPEG)
- Mode 2-3: inter with motion vector
- Mode 4-7: inter with motion vector from neighbors

### 4. DCT Coefficient Decode (~500 LOC)

Similar to JPEG:
- Huffman decode of DC/AC coefficients (32 DC + 48 AC Huffman tables)
- Zigzag scan order (same as JPEG)
- Dequantization using per-quality-level matrices
- Inverse DCT (8x8 block, same math as JPEG)

### 5. Motion Compensation (~400 LOC)

For inter (P) frames:
- Block-level motion vectors (integer + half-pixel)
- Half-pixel interpolation: bilinear filter on reference frame
- Apply prediction + residual

### 6. Loop Filter (~150 LOC)

In-loop deblocking at block boundaries:
- 4-tap horizontal and vertical filter
- Strength varies by quality level (from setup header tables)

### 7. YCbCr 4:2:0 → RGB Conversion (~100 LOC)

```c
// BT.601 conversion (standard for SD/Theora)
static void ycbcr420_to_rgba(const uint8_t *y_plane, const uint8_t *cb_plane,
                              const uint8_t *cr_plane, int width, int height,
                              int y_stride, int c_stride, uint32_t *rgba_out) {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int y = y_plane[row * y_stride + col] - 16;
            int cb = cb_plane[(row/2) * c_stride + (col/2)] - 128;
            int cr = cr_plane[(row/2) * c_stride + (col/2)] - 128;
            int r = (298 * y + 409 * cr + 128) >> 8;
            int g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
            int b = (298 * y + 516 * cb + 128) >> 8;
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            rgba_out[row * width + col] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) |
                ((uint32_t)b << 8) | 0xFF;
        }
    }
}
```

### 8. OGG Multi-Stream Demux (~100 LOC)

The existing `rt_ogg.c` parses OGG pages. For Theora+Vorbis files, we need
to demux by stream serial number:
- Read first 3 BOS (Beginning of Stream) pages
- Identify Theora stream (first packet starts with `0x80` + "theora")
- Identify Vorbis stream (first packet starts with `0x01` + "vorbis")
- Route subsequent pages to the correct decoder by serial number

### 9. VideoPlayer Integration (~200 LOC)

Extend `rt_videoplayer.c` to support `container_type == 1` (OGG):
- Parse Theora + Vorbis headers from OGG stream
- On `Update(dt)`: read next OGG page, decode Theora packet → YCbCr →
  RGB → Pixels frame, feed Vorbis packets to audio
- Sync via OGG granule position timestamps

## Files

| File | Description |
|------|-------------|
| `src/runtime/graphics/rt_theora.c` | Theora decoder (~2500 LOC) |
| `src/runtime/graphics/rt_theora.h` | Public API |
| `src/runtime/graphics/rt_ycbcr.c` | YCbCr→RGB conversion (~100 LOC) |
| `src/runtime/graphics/rt_ycbcr.h` | Conversion API |
| `src/runtime/graphics/rt_videoplayer.c` | Extend for OGG/Theora (~200 LOC) |
| `src/runtime/graphics/rt_avi.c` | (no changes) |
| `src/runtime/graphics/rt_graphics_stubs.c` | Add stubs |
| `src/runtime/CMakeLists.txt` | Add new source files |

## LOC Estimate

~3500 LOC total:
- Theora decoder: ~2500 LOC
- YCbCr conversion: ~100 LOC
- OGG demux extension: ~100 LOC
- VideoPlayer OGG integration: ~200 LOC
- Stubs + registration: ~100 LOC

## Testing

### Unit Test: `TestTheoraDecoder.cpp`

```cpp
TEST(TheoraDecoder, DecodesIdentificationHeader) {
    // Parse a known Theora identification header packet
    theora_decoder_t dec = {};
    uint8_t id_header[] = { 0x80, 't','h','e','o','r','a', /* ... */ };
    ASSERT_EQ(theora_decode_header(&dec, id_header, sizeof(id_header)), 0);
    EXPECT_GT(dec.width, 0);
    EXPECT_GT(dec.height, 0);
    EXPECT_GT(dec.fps_num, 0u);
}

TEST(TheoraDecoder, DecodesIntraFrame) {
    // Load a minimal OGG Theora file (1 I-frame, no audio)
    theora_decoder_t dec = {};
    // ... parse headers ...
    // Decode first data packet
    uint8_t *y, *cb, *cr;
    ASSERT_EQ(theora_decode_frame(&dec, packet_data, packet_len,
                                   &y, &cb, &cr), 0);
    EXPECT_NE(y, nullptr);
}

TEST(TheoraDecoder, YCbCrToRgbaConversion) {
    // Test known color values
    uint8_t y[] = {235}; // white in BT.601
    uint8_t cb[] = {128};
    uint8_t cr[] = {128};
    uint32_t rgba;
    ycbcr420_to_rgba(y, cb, cr, 1, 1, 1, 1, &rgba);
    // Should be close to white (255,255,255)
    EXPECT_GE((rgba >> 24) & 0xFF, 250u); // R
    EXPECT_GE((rgba >> 16) & 0xFF, 250u); // G
    EXPECT_GE((rgba >> 8) & 0xFF, 250u);  // B
}
```

### Integration Test: `test_theora_playback.zia`

```zia
module TestTheoraPlayback;
bind Viper.Graphics;
bind Viper.Terminal;

func start() {
    var player = VideoPlayer.Open("tests/runtime/assets/test_theora.ogv");
    if (player == null) {
        Say("FAIL: could not open OGV file");
        return;
    }
    if (VideoPlayer.get_Width(player) <= 0) {
        Say("FAIL: width <= 0");
        return;
    }
    VideoPlayer.Play(player);
    VideoPlayer.Update(player, 0.1);
    var frame = VideoPlayer.get_Frame(player);
    if (frame == null) {
        Say("FAIL: no frame");
        return;
    }
    Say("PASS");
}
```

### Test Assets

- `tests/runtime/assets/test_theora.ogv` — Short OGG Theora file (5 frames,
  160x120, no audio). Generate with: `ffmpeg -f lavfi -i testsrc=s=160x120:d=0.2
  -c:v libtheora -q:v 5 test_theora.ogv`
- `tests/runtime/assets/test_theora_av.ogv` — With Vorbis audio (1 second,
  160x120). Generate with: `ffmpeg -f lavfi -i testsrc=s=160x120:d=1 -f lavfi
  -i sine=f=440:d=1 -c:v libtheora -q:v 5 -c:a libvorbis test_theora_av.ogv`

### CMake Registration

```cmake
viper_add_test(test_theora_decoder
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestTheoraDecoder.cpp)
target_link_libraries(test_theora_decoder PRIVATE viper_test_common)
viper_add_ctest(test_theora_decoder test_theora_decoder)
set_tests_properties(test_theora_decoder PROPERTIES LABELS "unit;runtime")
```

## Verification

1. `./scripts/build_viper.sh`
2. `ctest --test-dir build -R test_theora --output-on-failure`
3. Run OGV demo: `viper run examples/apiaudit/graphics/theora_demo.zia`

## Reference

- Theora Specification: https://www.theora.org/doc/Theora.pdf
- VP3 format notes: https://github.com/xiph/theora/blob/master/doc/vp3-format.txt
- OGG format: https://xiph.org/ogg/doc/
