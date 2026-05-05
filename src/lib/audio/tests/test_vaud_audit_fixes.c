//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "vaud_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_failed = 0;
static vaud_error_t last_error_code = VAUD_OK;
static const char *last_error_msg = NULL;

void vaud_set_error(vaud_error_t code, const char *msg) {
    last_error_code = code;
    last_error_msg = msg;
}

#define EXPECT_TRUE(expr)                                                                            \
    do {                                                                                             \
        if (!(expr)) {                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                                   \
            tests_failed++;                                                                          \
            return;                                                                                  \
        }                                                                                            \
    } while (0)

static void test_resample_overflow_returns_sentinel(void) {
    int64_t frames = vaud_resample_output_frames(INT64_MAX / 2, 1, 3);
    EXPECT_TRUE(frames == INT64_MAX);
}

static void test_pcm_size_rejects_sentinel_frame_count(void) {
    size_t bytes = 0;
    EXPECT_TRUE(!vaud_pcm_s16_buffer_size(INT64_MAX, 2, &bytes));
}

static void test_pcm_size_rejects_channel_multiply_overflow(void) {
    size_t bytes = 0;
    EXPECT_TRUE(!vaud_pcm_s16_buffer_size((int64_t)(SIZE_MAX / 2), 3, &bytes));
}

static void test_pcm_size_accepts_normal_stereo_buffer(void) {
    size_t bytes = 0;
    EXPECT_TRUE(vaud_pcm_s16_buffer_size(44100, 2, &bytes));
    EXPECT_TRUE(bytes == 44100u * 2u * sizeof(int16_t));
}

static void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void write_f32_le(uint8_t *p, float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    write_u32_le(p, bits);
}

static size_t make_pcm32_wav(uint8_t *buf, size_t cap) {
    if (!buf || cap < 52)
        return 0;
    memset(buf, 0, cap);
    memcpy(buf + 0, "RIFF", 4);
    write_u32_le(buf + 4, 44);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    write_u32_le(buf + 16, 16);
    write_u16_le(buf + 20, 1);
    write_u16_le(buf + 22, 1);
    write_u32_le(buf + 24, 44100);
    write_u32_le(buf + 28, 44100u * 4u);
    write_u16_le(buf + 32, 4);
    write_u16_le(buf + 34, 32);
    memcpy(buf + 36, "data", 4);
    write_u32_le(buf + 40, 8);
    write_u32_le(buf + 44, 0x7FFF0000u);
    write_u32_le(buf + 48, 0x80000000u);
    return 52;
}

static size_t make_float32_wav(uint8_t *buf, size_t cap) {
    if (!buf || cap < 52)
        return 0;
    memset(buf, 0, cap);
    memcpy(buf + 0, "RIFF", 4);
    write_u32_le(buf + 4, 44);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    write_u32_le(buf + 16, 16);
    write_u16_le(buf + 20, 3);
    write_u16_le(buf + 22, 1);
    write_u32_le(buf + 24, 44100);
    write_u32_le(buf + 28, 44100u * 4u);
    write_u16_le(buf + 32, 4);
    write_u16_le(buf + 34, 32);
    memcpy(buf + 36, "data", 4);
    write_u32_le(buf + 40, 8);
    write_f32_le(buf + 44, -1.0f);
    write_f32_le(buf + 48, 1.0f);
    return 52;
}

static size_t make_pcm8_wav(uint8_t *buf, size_t cap) {
    if (!buf || cap < 47)
        return 0;
    memset(buf, 0, cap);
    memcpy(buf + 0, "RIFF", 4);
    write_u32_le(buf + 4, 39);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    write_u32_le(buf + 16, 16);
    write_u16_le(buf + 20, 1);
    write_u16_le(buf + 22, 1);
    write_u32_le(buf + 24, 44100);
    write_u32_le(buf + 28, 44100);
    write_u16_le(buf + 32, 1);
    write_u16_le(buf + 34, 8);
    memcpy(buf + 36, "data", 4);
    write_u32_le(buf + 40, 3);
    buf[44] = 0;
    buf[45] = 128;
    buf[46] = 255;
    return 47;
}

static void test_wav_pcm32_decodes_little_endian_samples(void) {
    uint8_t wav[64];
    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t rate = 0;
    int32_t channels = 0;
    size_t size = make_pcm32_wav(wav, sizeof(wav));
    EXPECT_TRUE(size > 0);
    EXPECT_TRUE(vaud_wav_load_mem(wav, size, &samples, &frames, &rate, &channels));
    EXPECT_TRUE(frames == 2);
    EXPECT_TRUE(rate == 44100);
    EXPECT_TRUE(channels == 1);
    EXPECT_TRUE(samples[0] == 32767);
    EXPECT_TRUE(samples[1] == 32767);
    EXPECT_TRUE(samples[2] == -32768);
    EXPECT_TRUE(samples[3] == -32768);
    free(samples);
}

static void test_wav_float32_maps_full_scale_endpoints(void) {
    uint8_t wav[64];
    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t rate = 0;
    int32_t channels = 0;
    size_t size = make_float32_wav(wav, sizeof(wav));
    EXPECT_TRUE(size > 0);
    EXPECT_TRUE(vaud_wav_load_mem(wav, size, &samples, &frames, &rate, &channels));
    EXPECT_TRUE(frames == 2);
    EXPECT_TRUE(rate == 44100);
    EXPECT_TRUE(channels == 1);
    EXPECT_TRUE(samples[0] == INT16_MIN);
    EXPECT_TRUE(samples[1] == INT16_MIN);
    EXPECT_TRUE(samples[2] == INT16_MAX);
    EXPECT_TRUE(samples[3] == INT16_MAX);
    free(samples);
}

static void test_wav_pcm8_decodes_without_signed_shift_ub(void) {
    uint8_t wav[64];
    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t rate = 0;
    int32_t channels = 0;
    size_t size = make_pcm8_wav(wav, sizeof(wav));
    EXPECT_TRUE(size > 0);
    EXPECT_TRUE(vaud_wav_load_mem(wav, size, &samples, &frames, &rate, &channels));
    EXPECT_TRUE(frames == 3);
    EXPECT_TRUE(rate == 44100);
    EXPECT_TRUE(channels == 1);
    EXPECT_TRUE(samples[0] == -32768 && samples[1] == -32768);
    EXPECT_TRUE(samples[2] == 0 && samples[3] == 0);
    EXPECT_TRUE(samples[4] == 32512 && samples[5] == 32512);
    free(samples);
}

static void test_resample_rejects_invalid_rates_and_channels(void) {
    int16_t input[2] = {100, -100};
    int16_t output[2] = {1234, 5678};
    vaud_resample(input, 1, 0, output, 1, 44100, 1);
    EXPECT_TRUE(output[0] == 1234 && output[1] == 5678);
    vaud_resample(input, 1, 44100, output, 1, 0, 1);
    EXPECT_TRUE(output[0] == 1234 && output[1] == 5678);
    vaud_resample(input, 1, 44100, output, 1, 44100, 0);
    EXPECT_TRUE(output[0] == 1234 && output[1] == 5678);
}

int main(void) {
    test_resample_overflow_returns_sentinel();
    test_pcm_size_rejects_sentinel_frame_count();
    test_pcm_size_rejects_channel_multiply_overflow();
    test_pcm_size_accepts_normal_stereo_buffer();
    test_wav_pcm32_decodes_little_endian_samples();
    test_wav_float32_maps_full_scale_endpoints();
    test_wav_pcm8_decodes_without_signed_shift_ub();
    test_resample_rejects_invalid_rates_and_channels();

    if (tests_failed != 0)
        return 1;

    (void)last_error_code;
    (void)last_error_msg;
    printf("test_vaud_audit_fixes: PASS\n");
    return 0;
}
