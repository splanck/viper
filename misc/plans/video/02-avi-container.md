# Plan 02: AVI RIFF Container Parser

## Problem

No video container parser exists in Viper. AVI is the simplest video
container format (flat RIFF chunk structure, well-documented Microsoft spec).

## Goal

Parse AVI files to extract interleaved video frames and audio chunks.
Support MJPEG video (`MJPG` FOURCC) and PCM audio (uncompressed WAV).

## Zero External Dependencies

RIFF is a binary format with 4-byte tags and 32-bit sizes. Pure C parsing.

---

## AVI File Structure

```
RIFF('AVI '
  LIST('hdrl'
    'avih'                 — main AVI header (frame rate, dimensions, stream count)
    LIST('strl'            — stream 0 (video)
      'strh'               — stream header (codec FOURCC, frame count, rate)
      'strf'               — stream format (BITMAPINFOHEADER for video)
    )
    LIST('strl'            — stream 1 (audio)
      'strh'               — stream header
      'strf'               — stream format (WAVEFORMATEX for audio)
    )
  )
  LIST('movi'              — interleaved A/V data
    '00dc' [JPEG data]     — video frame (stream 0, dc = compressed video)
    '01wb' [PCM data]      — audio chunk (stream 1, wb = wave bytes)
    '00dc' [JPEG data]
    '01wb' [PCM data]
    ...
  )
  'idx1'                   — optional index (offsets into movi)
)
```

Key: `00dc` = stream 0, data type `dc` (compressed video). `01wb` = stream 1,
data type `wb` (wave bytes). Stream numbers are 2-digit ASCII.

## Implementation

### Data Structures

```c
// rt_avi.h

typedef struct {
    uint32_t fourcc;       // codec FOURCC (e.g., 'MJPG')
    int32_t width, height;
    int32_t frame_count;
    double fps;            // from avih.dwMicroSecPerFrame
    double duration;       // frame_count / fps
} avi_video_info_t;

typedef struct {
    int32_t sample_rate;
    int32_t channels;
    int32_t bits_per_sample;
    int32_t block_align;
} avi_audio_info_t;

typedef struct {
    const uint8_t *data;   // pointer into movi data
    uint32_t size;         // chunk payload size
    int32_t stream_index;  // 0=video, 1=audio
    int8_t is_video;
} avi_chunk_t;

typedef struct {
    uint8_t *file_data;    // entire file in memory
    size_t file_len;
    avi_video_info_t video;
    avi_audio_info_t audio;
    int8_t has_audio;
    // Index: array of chunk descriptors in playback order
    avi_chunk_t *chunks;
    int32_t chunk_count;
    int32_t chunk_capacity;
} avi_context_t;
```

### Parser Functions

```c
/// @brief Parse an AVI file from a memory buffer.
/// Returns 0 on success, -1 on error.
int avi_parse(avi_context_t *ctx, uint8_t *data, size_t len);

/// @brief Free resources (does NOT free file_data — caller owns it).
void avi_free(avi_context_t *ctx);

/// @brief Get video frame data at given index.
/// Returns pointer to JPEG data and sets *out_size.
const uint8_t *avi_get_video_frame(const avi_context_t *ctx,
                                    int32_t frame_index,
                                    uint32_t *out_size);

/// @brief Get audio chunk at given index.
const uint8_t *avi_get_audio_chunk(const avi_context_t *ctx,
                                    int32_t chunk_index,
                                    uint32_t *out_size);
```

### Parsing Steps

1. Validate RIFF header: first 4 bytes = `RIFF`, bytes 8-11 = `AVI `
2. Walk top-level chunks:
   - `LIST('hdrl')`: parse `avih` (main header), then each `LIST('strl')` for stream headers
   - `LIST('movi')`: walk sub-chunks, record each `XXdc`/`XXdb`/`XXwb` with pointer + size
   - `idx1`: optional index — if present, use for seeking; if absent, use movi walk order
3. Parse `avih` header:
   - `dwMicroSecPerFrame` → fps = 1000000.0 / dwMicroSecPerFrame
   - `dwTotalFrames`, `dwWidth`, `dwHeight`
4. Parse `strh` headers:
   - `fccType` = `vids` (video) or `auds` (audio)
   - `fccHandler` = codec FOURCC (e.g., `MJPG`)
5. Parse `strf` format:
   - Video: BITMAPINFOHEADER (biWidth, biHeight, biCompression)
   - Audio: WAVEFORMATEX (nSamplesPerSec, nChannels, wBitsPerSample)

### RIFF Chunk Walking

```c
// Generic RIFF chunk walker
static int walk_riff(const uint8_t *data, size_t len, size_t pos,
                      void (*callback)(uint32_t fourcc, const uint8_t *payload,
                                       uint32_t size, void *userdata),
                      void *userdata) {
    while (pos + 8 <= len) {
        uint32_t fourcc = read_le32(data + pos);
        uint32_t size = read_le32(data + pos + 4);
        if (pos + 8 + size > len) break;
        callback(fourcc, data + pos + 8, size, userdata);
        pos += 8 + size;
        if (size & 1) pos++; // RIFF chunks are 2-byte aligned
    }
    return 0;
}
```

## Files

| File | Description |
|------|-------------|
| `src/runtime/graphics/rt_avi.c` | AVI RIFF parser (~300 LOC) |
| `src/runtime/graphics/rt_avi.h` | Public API + data structures |
| `src/runtime/CMakeLists.txt` | Add `graphics/rt_avi.c` |

## LOC Estimate

~400 LOC (parser + chunk walking + header decode + index building).

## Testing

### Unit Test: `TestAviParser.cpp`

```cpp
TEST(AviParser, ParsesValidMjpegAvi) {
    // Load test AVI file (small, ~10 frames, MJPEG + PCM)
    auto data = loadFile("tests/runtime/assets/test_video.avi");
    avi_context_t ctx = {};
    ASSERT_EQ(avi_parse(&ctx, data.data(), data.size()), 0);

    EXPECT_EQ(ctx.video.fourcc, FOURCC('M','J','P','G'));
    EXPECT_GT(ctx.video.width, 0);
    EXPECT_GT(ctx.video.height, 0);
    EXPECT_GT(ctx.video.frame_count, 0);
    EXPECT_GT(ctx.video.fps, 0.0);

    // Verify first video frame is valid JPEG (starts with 0xFFD8)
    uint32_t frame_size = 0;
    const uint8_t *frame = avi_get_video_frame(&ctx, 0, &frame_size);
    ASSERT_NE(frame, nullptr);
    EXPECT_GE(frame_size, 2u);
    EXPECT_EQ(frame[0], 0xFF);
    EXPECT_EQ(frame[1], 0xD8);

    avi_free(&ctx);
}

TEST(AviParser, RejectsNonAviFile) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    avi_context_t ctx = {};
    EXPECT_NE(avi_parse(&ctx, garbage, sizeof(garbage)), 0);
}

TEST(AviParser, HandlesAudioStream) {
    auto data = loadFile("tests/runtime/assets/test_video.avi");
    avi_context_t ctx = {};
    ASSERT_EQ(avi_parse(&ctx, data.data(), data.size()), 0);

    if (ctx.has_audio) {
        EXPECT_GT(ctx.audio.sample_rate, 0);
        EXPECT_GT(ctx.audio.channels, 0);
        uint32_t audio_size = 0;
        const uint8_t *audio = avi_get_audio_chunk(&ctx, 0, &audio_size);
        EXPECT_NE(audio, nullptr);
    }
    avi_free(&ctx);
}
```

### Test Asset

Generate a small test AVI file (10 frames, 160x120, MJPEG + 8kHz PCM mono)
using a script or by encoding manually. Store at
`tests/runtime/assets/test_video.avi`. Keep < 100 KB.

### CMake Registration

```cmake
viper_add_test(test_avi_parser
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestAviParser.cpp)
target_link_libraries(test_avi_parser PRIVATE viper_test_common)
viper_add_ctest(test_avi_parser test_avi_parser)
set_tests_properties(test_avi_parser PROPERTIES LABELS "unit;runtime")
```

## Verification

1. `./scripts/build_viper.sh`
2. `ctest --test-dir build -R test_avi_parser --output-on-failure`
