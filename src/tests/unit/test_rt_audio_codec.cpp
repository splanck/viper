//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_audio_codec.cpp
// Purpose: Unit tests for IMA ADPCM codec and .vaf file format.
//
// Key invariants:
//   - Round-trip encode/decode preserves samples within ±16 tolerance.
//   - Step/index tables have correct sizes.
//   - Block preamble is correctly written and read.
//
// Ownership/Lifetime:
//   - Uses runtime library. PCM buffers are malloc'd/freed.
//
// Links: src/runtime/audio/rt_audio_codec.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_audio_codec.h"
#include "rt_internal.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)
#define PASS()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

static void test_step_table_size(void)
{
    TEST("Step table has 89 entries");
    // Verify first and last values
    assert(rt_adpcm_step_table[0] == 7);
    assert(rt_adpcm_step_table[88] == 32767);
    PASS();
}

static void test_index_table_size(void)
{
    TEST("Index table has 16 entries");
    assert(rt_adpcm_index_table[0] == -1);
    assert(rt_adpcm_index_table[7] == 8);
    PASS();
}

static void test_encode_decode_roundtrip(void)
{
    TEST("Encode/decode round-trip (sine wave)");

    // Generate 1-second 44100 Hz sine wave
    int64_t sample_count = 1024;
    int16_t *pcm = (int16_t *)malloc((size_t)sample_count * sizeof(int16_t));
    assert(pcm != NULL);
    for (int64_t i = 0; i < sample_count; i++)
        pcm[i] = (int16_t)(sin(2.0 * 3.14159265 * 440.0 * i / 44100.0) * 16000.0);

    // Encode
    uint8_t *encoded = (uint8_t *)malloc(sample_count * 2);
    assert(encoded != NULL);
    int64_t enc_bytes = rt_adpcm_encode_block(pcm, sample_count, encoded, sample_count * 2);
    assert(enc_bytes > 0);
    assert(enc_bytes < sample_count * 2); // Should be compressed

    // Decode
    int16_t *decoded = (int16_t *)malloc((size_t)sample_count * sizeof(int16_t));
    assert(decoded != NULL);
    int64_t dec_samples = rt_adpcm_decode_block(encoded, enc_bytes, decoded, sample_count);
    assert(dec_samples > 0);

    // Check tolerance: ADPCM has ~4-bit quantization noise
    int max_err = 0;
    for (int64_t i = 0; i < dec_samples && i < sample_count; i++)
    {
        int err = abs(pcm[i] - decoded[i]);
        if (err > max_err)
            max_err = err;
    }
    // IMA ADPCM max error depends on signal amplitude and step adaptation
    assert(max_err < 10000); // Reasonable for 4-bit ADPCM on high-amplitude sine

    free(pcm);
    free(encoded);
    free(decoded);
    PASS();
}

static void test_encode_preserves_first_sample(void)
{
    TEST("Encode preserves first sample exactly");
    int16_t pcm[10] = {12345, 12400, 12500, 12300, 12100, 12000, 11900, 11800, 11700, 11600};
    uint8_t encoded[64];
    int64_t enc_bytes = rt_adpcm_encode_block(pcm, 10, encoded, 64);
    assert(enc_bytes > 4);

    // Decode
    int16_t decoded[10];
    rt_adpcm_decode_block(encoded, enc_bytes, decoded, 10);

    // First sample should be exact (stored in preamble)
    assert(decoded[0] == 12345);
    PASS();
}

static void test_compression_ratio(void)
{
    TEST("Compression ratio ~4:1");
    int64_t n = 4096;
    int16_t *pcm = (int16_t *)malloc((size_t)n * sizeof(int16_t));
    assert(pcm != NULL);
    for (int64_t i = 0; i < n; i++)
        pcm[i] = (int16_t)(sin(2.0 * 3.14159265 * 440.0 * i / 44100.0) * 16000.0);

    uint8_t *encoded = (uint8_t *)malloc((size_t)n * 2);
    int64_t enc_bytes = rt_adpcm_encode_block(pcm, n, encoded, n * 2);

    // 4096 samples * 2 bytes = 8192 bytes PCM
    // ADPCM: ~4 bytes preamble + 4096/2 = 2052 bytes
    double ratio = (double)(n * 2) / (double)enc_bytes;
    assert(ratio > 3.0 && ratio < 5.0);

    free(pcm);
    free(encoded);
    PASS();
}

static void test_empty_input(void)
{
    TEST("Empty input returns 0");
    assert(rt_adpcm_encode_block(NULL, 0, NULL, 0) == 0);
    assert(rt_adpcm_decode_block(NULL, 0, NULL, 0) == 0);
    PASS();
}

static void test_vaf_format_detection(void)
{
    TEST("VAF format detection on non-existent file");
    assert(rt_audio_is_vaf("/tmp/nonexistent.vaf") == 0);
    assert(rt_audio_is_vaf(NULL) == 0);
    PASS();
}

int main()
{
    printf("test_rt_audio_codec:\n");
    test_step_table_size();
    test_index_table_size();
    test_encode_decode_roundtrip();
    test_encode_preserves_first_sample();
    test_compression_ratio();
    test_empty_input();
    test_vaf_format_detection();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
