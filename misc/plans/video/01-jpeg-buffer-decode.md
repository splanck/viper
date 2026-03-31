# Plan 01: JPEG Buffer Decode Refactor

## Problem

`rt_pixels_load_jpeg()` in `rt_pixels_io.c:1340` only accepts a file path.
Internally it reads the file into a `uint8_t *` buffer, then decodes from
`jpeg_ctx_t.data`. MJPEG needs to decode JPEG frames directly from memory
buffers extracted from the AVI container — no temporary file.

## Goal

Extract the core JPEG decode logic into a public `rt_jpeg_decode_buffer()`
function that accepts a raw byte buffer. Keep `rt_pixels_load_jpeg()` as a
thin file-I/O wrapper around it.

## Zero External Dependencies

Pure refactor of existing code. No new algorithms or libraries.

---

## Implementation

### Step 1: Extract buffer decode function

In `rt_pixels_io.c`, after the existing `rt_pixels_load_jpeg`:

```c
/// @brief Decode a JPEG image from a memory buffer.
/// @param data Pointer to JPEG data (must start with 0xFFD8 SOI marker).
/// @param len Length of data in bytes.
/// @return New Pixels object, or NULL on failure.
void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);
```

Implementation: move the decode logic from `rt_pixels_load_jpeg` (lines
1373-1819) into this function. The existing function becomes:

```c
void *rt_pixels_load_jpeg(void *path) {
    // ... file open, fread into buffer ...
    void *result = rt_jpeg_decode_buffer(file_data, (size_t)file_len);
    free(file_data);
    return result;
}
```

### Step 2: Declare in header

In `rt_pixels.h`:
```c
void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);
```

### Step 3: Add stub

In `rt_graphics_stubs.c`:
```c
void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len) {
    (void)data; (void)len; return NULL;
}
```

## Files Modified

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_pixels_io.c` | Extract decode logic into `rt_jpeg_decode_buffer` |
| `src/runtime/graphics/rt_pixels.h` | Declare new function |
| `src/runtime/graphics/rt_graphics_stubs.c` | Add stub |

## LOC Estimate

~100 LOC (mostly moving existing code, ~20 new lines of glue).

## Testing

### Unit Test: `TestJpegBufferDecode.cpp`

```cpp
// In src/tests/unit/runtime/TestJpegBufferDecode.cpp
TEST(JpegBufferDecode, DecodesValidJpeg) {
    // Load a known JPEG file into memory
    FILE *f = fopen("tests/runtime/assets/test_image.jpg", "rb");
    ASSERT_NE(f, nullptr);
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(len);
    fread(buf, 1, len, f);
    fclose(f);

    void *pixels = rt_jpeg_decode_buffer(buf, len);
    ASSERT_NE(pixels, nullptr);
    EXPECT_GT(rt_pixels_width(pixels), 0);
    EXPECT_GT(rt_pixels_height(pixels), 0);
    free(buf);
}

TEST(JpegBufferDecode, RejectsInvalidData) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    void *pixels = rt_jpeg_decode_buffer(garbage, sizeof(garbage));
    EXPECT_EQ(pixels, nullptr);
}

TEST(JpegBufferDecode, RejectsNullAndZeroLen) {
    EXPECT_EQ(rt_jpeg_decode_buffer(NULL, 100), nullptr);
    uint8_t data[] = {0xFF, 0xD8};
    EXPECT_EQ(rt_jpeg_decode_buffer(data, 0), nullptr);
}
```

### CMake Registration

```cmake
viper_add_test(test_jpeg_buffer_decode
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestJpegBufferDecode.cpp)
target_link_libraries(test_jpeg_buffer_decode PRIVATE viper_test_common)
viper_add_ctest(test_jpeg_buffer_decode test_jpeg_buffer_decode)
set_tests_properties(test_jpeg_buffer_decode PROPERTIES LABELS "unit;runtime")
```

## Verification

1. `./scripts/build_viper.sh`
2. `ctest --test-dir build -R test_jpeg_buffer_decode --output-on-failure`
3. Verify `rt_pixels_load_jpeg` still works (existing JPEG tests pass)
