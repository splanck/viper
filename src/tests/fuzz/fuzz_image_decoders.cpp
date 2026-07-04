//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_image_decoders.cpp
// Purpose: libFuzzer harness for the from-scratch PNG, JPEG, and GIF buffer
//   decoders — the most attacker-controlled byte parsers in the 2D runtime.
//   The first input byte selects the decoder so one corpus covers all three.
//
// Key invariants:
//   - Input size is capped before decoding.
//   - Malformed images must fail cleanly; decoded buffers are freed.
//
// Ownership/Lifetime:
//   - Pixel buffers returned by the decoders are freed before returning.
//
// Links: src/runtime/graphics/2d/rt_pixels_png.c, rt_pixels_jpeg.c,
//   src/runtime/graphics/media/rt_gif.c
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <cstdlib>

extern "C" {
int rt_png_decode_buffer_rgba32(
    const uint8_t *data, size_t len, uint32_t **out_pixels, int64_t *out_width, int64_t *out_height);
void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);
int rt_gif_decode_memory_first_rgba32(
    const uint8_t *data, size_t len, uint32_t **out_pixels, int *out_width, int *out_height);
void rt_obj_free(void *obj);
int rt_obj_release_check0(void *obj);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > (512u << 10))
        return 0;

    const uint8_t selector = data[0];
    const uint8_t *payload = data + 1;
    const size_t payload_len = size - 1;

    switch (selector % 3) {
    case 0: {
        uint32_t *pixels = nullptr;
        int64_t w = 0;
        int64_t h = 0;
        if (rt_png_decode_buffer_rgba32(payload, payload_len, &pixels, &w, &h))
            std::free(pixels);
        break;
    }
    case 1: {
        void *pixels = rt_jpeg_decode_buffer(payload, payload_len);
        if (pixels && rt_obj_release_check0(pixels))
            rt_obj_free(pixels);
        break;
    }
    case 2: {
        uint32_t *pixels = nullptr;
        int w = 0;
        int h = 0;
        if (rt_gif_decode_memory_first_rgba32(payload, payload_len, &pixels, &w, &h))
            std::free(pixels);
        break;
    }
    }
    return 0;
}
