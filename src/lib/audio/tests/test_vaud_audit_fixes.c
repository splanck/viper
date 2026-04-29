//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "vaud_internal.h"

#include <stdint.h>
#include <stdio.h>

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

int main(void) {
    test_resample_overflow_returns_sentinel();
    test_pcm_size_rejects_sentinel_frame_count();
    test_pcm_size_rejects_channel_multiply_overflow();
    test_pcm_size_accepts_normal_stereo_buffer();

    if (tests_failed != 0)
        return 1;

    (void)last_error_code;
    (void)last_error_msg;
    printf("test_vaud_audit_fixes: PASS\n");
    return 0;
}
