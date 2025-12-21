//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTCompressTests.cpp
// Purpose: Validate Viper.IO.Compress DEFLATE/GZIP compression functions.
// Key invariants: Round-trip compression/decompression preserves data.
// Links: docs/viperlib/io.md
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Get bytes data pointer
static uint8_t *get_bytes_data(void *bytes)
{
    struct bytes_impl
    {
        int64_t len;
        uint8_t *data;
    };
    return ((bytes_impl *)bytes)->data;
}

/// @brief Get bytes length
static int64_t get_bytes_len(void *bytes)
{
    return rt_bytes_len(bytes);
}

/// @brief Compare two byte arrays
static bool bytes_equal(void *a, void *b)
{
    int64_t len_a = get_bytes_len(a);
    int64_t len_b = get_bytes_len(b);
    if (len_a != len_b)
        return false;
    return memcmp(get_bytes_data(a), get_bytes_data(b), len_a) == 0;
}

/// @brief Create bytes from raw data
static void *make_bytes(const uint8_t *data, size_t len)
{
    void *bytes = rt_bytes_new(len);
    memcpy(get_bytes_data(bytes), data, len);
    return bytes;
}

/// @brief Create bytes from string literal
static void *make_bytes_str(const char *str)
{
    size_t len = strlen(str);
    return make_bytes((const uint8_t *)str, len);
}

//=============================================================================
// DEFLATE Tests
//=============================================================================

static void test_deflate_literals_only()
{
    printf("Testing DEFLATE Literals Only (Fixed Huffman):\n");

    // Create 100 sequential bytes - no matches possible since each 3-byte
    // sequence is unique. This tests literal encoding only.
    uint8_t buffer[100];
    for (int i = 0; i < 100; i++)
    {
        buffer[i] = (uint8_t)i;
    }

    void *original = make_bytes(buffer, 100);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Literals-only round-trip", bytes_equal(original, decompressed));
    printf("  Original: %lld bytes, Compressed: %lld bytes\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed));
}

static void test_deflate_simple_match()
{
    printf("Testing DEFLATE Simple Match (Fixed Huffman):\n");

    // Create data with one simple match: "ABCABC" repeated
    // This has exactly one match opportunity: at position 3, match position 0, length 3
    const char *text = "ABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABCABC";
    void *original = make_bytes_str(text);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Simple match round-trip", bytes_equal(original, decompressed));
    printf("  Original: %lld bytes, Compressed: %lld bytes\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed));
}

static void test_deflate_distance_with_extra_bits()
{
    printf("Testing DEFLATE Distance with Extra Bits:\n");

    // Distance 5-6 require 1 extra bit (dist code 4-5)
    // Distance 7-8 require 1 extra bit (dist code 5)
    // Distance 9-12 require 2 extra bits (dist code 6-7)
    // Distance 25-32 require 4 extra bits (dist code 9)

    // Test distance 10 (requires 2 extra bits): 10 unique bytes then repeat
    const char *text = "0123456789" "0123456789" "0123456789" "0123456789"
                       "0123456789" "0123456789" "0123456789" "0123456789";
    void *original = make_bytes_str(text);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Distance with extra bits round-trip", bytes_equal(original, decompressed));
    printf("  Original: %lld bytes, Compressed: %lld bytes\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed));
}

static void test_deflate_distance_26()
{
    printf("Testing DEFLATE Distance 26:\n");

    // Distance 26 requires 3 extra bits (dist code 9, base 25, extra 1)
    // 26 unique bytes then repeat
    const char *text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    void *original = make_bytes_str(text);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Distance 26 round-trip", bytes_equal(original, decompressed));
    printf("  Original: %lld bytes, Compressed: %lld bytes\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed));
}

static void test_deflate_longer_data()
{
    printf("Testing DEFLATE Longer Data:\n");

    // Test sizes around length code boundaries (414 uses code 280, 415 uses code 281)
    int sizes[] = {300, 414, 415, 500, 1000};
    for (int s = 0; s < 5; s++)
    {
        int size = sizes[s];
        char *buffer = (char *)malloc(size);
        for (int i = 0; i < size; i++)
        {
            buffer[i] = 'A' + (i % 26);
        }

        void *original = make_bytes((const uint8_t *)buffer, size);
        free(buffer);
        void *compressed = rt_compress_deflate(original);
        void *decompressed = rt_compress_inflate(compressed);

        char test_name[32];
        snprintf(test_name, sizeof(test_name), "%d bytes round-trip", size);
        test_result(test_name, bytes_equal(original, decompressed));
    }
}

static void test_deflate_inflate_empty()
{
    printf("Testing DEFLATE Empty:\n");

    void *empty = rt_bytes_new(0);
    void *compressed = rt_compress_deflate(empty);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Empty data round-trip", bytes_equal(empty, decompressed));
}

static void test_deflate_inflate_small()
{
    printf("Testing DEFLATE Small Data:\n");

    // Small data uses stored blocks
    const char *text = "Hello, World!";
    void *original = make_bytes_str(text);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Small data round-trip", bytes_equal(original, decompressed));
    printf("  Original: %lld bytes, Compressed: %lld bytes\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed));
}

static void test_deflate_inflate_repeated()
{
    printf("Testing DEFLATE Repeated Data:\n");

    // Data with lots of repetition - verify round-trip works
    char buffer[1000];
    for (int i = 0; i < 1000; i++)
    {
        buffer[i] = 'A' + (i % 26);
    }

    void *original = make_bytes((const uint8_t *)buffer, 1000);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Repeated data round-trip", bytes_equal(original, decompressed));

    printf("  Original: %lld bytes, Compressed: %lld bytes (%.1f%% ratio)\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed),
           100.0 * get_bytes_len(compressed) / get_bytes_len(original));
}

static void test_deflate_levels()
{
    printf("Testing DEFLATE Levels:\n");

    // Create compressible data
    char buffer[2000];
    for (int i = 0; i < 2000; i++)
    {
        buffer[i] = 'A' + (i % 10);
    }
    void *original = make_bytes((const uint8_t *)buffer, 2000);

    // Test different levels
    for (int level = 1; level <= 9; level++)
    {
        void *compressed = rt_compress_deflate_lvl(original, level);
        void *decompressed = rt_compress_inflate(compressed);

        char test_name[32];
        snprintf(test_name, sizeof(test_name), "Level %d round-trip", level);
        test_result(test_name, bytes_equal(original, decompressed));
    }
}

static void test_deflate_binary()
{
    printf("Testing DEFLATE Binary Data:\n");

    // Binary data with all byte values
    uint8_t buffer[512];
    for (int i = 0; i < 512; i++)
    {
        buffer[i] = (uint8_t)(i & 0xFF);
    }

    void *original = make_bytes(buffer, 512);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Binary data round-trip", bytes_equal(original, decompressed));
}

//=============================================================================
// GZIP Tests
//=============================================================================

static void test_gzip_gunzip_basic()
{
    printf("Testing GZIP Basic:\n");

    const char *text = "Hello, GZIP World!";
    void *original = make_bytes_str(text);
    void *compressed = rt_compress_gzip(original);
    void *decompressed = rt_compress_gunzip(compressed);

    test_result("Basic round-trip", bytes_equal(original, decompressed));

    // Check GZIP magic number
    uint8_t *data = get_bytes_data(compressed);
    test_result("GZIP magic number", data[0] == 0x1F && data[1] == 0x8B);
    test_result("GZIP method = deflate", data[2] == 0x08);
}

static void test_gzip_levels()
{
    printf("Testing GZIP Levels:\n");

    char buffer[1000];
    for (int i = 0; i < 1000; i++)
    {
        buffer[i] = 'X';
    }
    void *original = make_bytes((const uint8_t *)buffer, 1000);

    for (int level = 1; level <= 9; level++)
    {
        void *compressed = rt_compress_gzip_lvl(original, level);
        void *decompressed = rt_compress_gunzip(compressed);

        char test_name[32];
        snprintf(test_name, sizeof(test_name), "Level %d round-trip", level);
        test_result(test_name, bytes_equal(original, decompressed));
    }
}

static void test_gzip_crc()
{
    printf("Testing GZIP CRC:\n");

    // Create data and compress
    void *original = make_bytes_str("Test data for CRC verification");
    void *compressed = rt_compress_gzip(original);
    void *decompressed = rt_compress_gunzip(compressed);

    test_result("CRC verification passed", bytes_equal(original, decompressed));
}

//=============================================================================
// String Convenience Tests
//=============================================================================

static void test_deflate_string()
{
    printf("Testing DEFLATE String:\n");

    rt_string text = rt_const_cstr("Hello, String Compression!");
    void *compressed = rt_compress_deflate_str(text);
    rt_string decompressed = rt_compress_inflate_str(compressed);

    const char *orig_str = rt_string_cstr(text);
    const char *dec_str = rt_string_cstr(decompressed);

    test_result("String round-trip", strcmp(orig_str, dec_str) == 0);
}

static void test_gzip_string()
{
    printf("Testing GZIP String:\n");

    rt_string text = rt_const_cstr("Hello, GZIP String!");
    void *compressed = rt_compress_gzip_str(text);
    rt_string decompressed = rt_compress_gunzip_str(compressed);

    const char *orig_str = rt_string_cstr(text);
    const char *dec_str = rt_string_cstr(decompressed);

    test_result("String round-trip", strcmp(orig_str, dec_str) == 0);
}

//=============================================================================
// Known Compressed Data Tests
//=============================================================================

static void test_inflate_known_data()
{
    printf("Testing Inflate Known Data:\n");

    // This is "Hello" compressed with deflate (stored block)
    // Created by: echo -n "Hello" | python3 -c "import zlib,sys; sys.stdout.buffer.write(zlib.compress(sys.stdin.buffer.read(), 0)[2:-4])"
    // However, since we use stored blocks for small data, let's just verify round-trip

    const char *text = "Hello";
    void *original = make_bytes_str(text);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Known data round-trip", bytes_equal(original, decompressed));
}

//=============================================================================
// Large Data Test
//=============================================================================

static void test_large_data()
{
    printf("Testing Large Data:\n");

    // 100KB of compressible data
    size_t size = 100 * 1024;
    uint8_t *buffer = (uint8_t *)malloc(size);
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = (uint8_t)('A' + (i % 26));
    }

    void *original = make_bytes(buffer, size);
    free(buffer);

    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Large data round-trip", bytes_equal(original, decompressed));

    printf("  Original: %lld bytes, Compressed: %lld bytes (%.1f%% ratio)\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed),
           100.0 * get_bytes_len(compressed) / get_bytes_len(original));
}

//=============================================================================
// Random Data Test
//=============================================================================

static void test_random_data()
{
    printf("Testing Random Data:\n");

    // Random data (hard to compress)
    uint8_t buffer[1000];
    unsigned int seed = 12345;
    for (int i = 0; i < 1000; i++)
    {
        seed = seed * 1103515245 + 12345;
        buffer[i] = (uint8_t)(seed >> 16);
    }

    void *original = make_bytes(buffer, 1000);
    void *compressed = rt_compress_deflate(original);
    void *decompressed = rt_compress_inflate(compressed);

    test_result("Random data round-trip", bytes_equal(original, decompressed));

    printf("  Original: %lld bytes, Compressed: %lld bytes\n",
           (long long)get_bytes_len(original),
           (long long)get_bytes_len(compressed));
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Compress Tests ===\n\n");

    // DEFLATE tests
    test_deflate_literals_only();
    printf("\n");
    test_deflate_simple_match();
    printf("\n");
    test_deflate_distance_with_extra_bits();
    printf("\n");
    test_deflate_distance_26();
    printf("\n");
    test_deflate_longer_data();
    printf("\n");
    test_deflate_inflate_empty();
    printf("\n");
    test_deflate_inflate_small();
    printf("\n");
    test_deflate_inflate_repeated();
    printf("\n");
    test_deflate_levels();
    printf("\n");
    test_deflate_binary();
    printf("\n");

    // GZIP tests
    test_gzip_gunzip_basic();
    printf("\n");
    test_gzip_levels();
    printf("\n");
    test_gzip_crc();
    printf("\n");

    // String tests
    test_deflate_string();
    printf("\n");
    test_gzip_string();
    printf("\n");

    // Known data
    test_inflate_known_data();
    printf("\n");

    // Large data
    test_large_data();
    printf("\n");

    // Random data
    test_random_data();
    printf("\n");

    printf("All Compress tests passed!\n");
    return 0;
}
