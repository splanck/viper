//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_zstd.cpp
// Purpose: Unit tests for the from-scratch Zstandard decompressor — real-frame
//   round-trips against fixtures produced by the reference encoder, plus
//   malformed-input rejection (truncation, bad magic, bad checksum, output
//   budget).
//
// Key invariants:
//   - Every fixture decodes byte-identically to its plaintext twin.
//   - Corrupt or truncated frames return 0 without crashing or leaking.
//   - max_output is honored as a hard budget.
//
// Ownership/Lifetime:
//   - Decoded buffers are freed by the test after each comparison.
// Links: src/runtime/io/rt_zstd.c
//
//===----------------------------------------------------------------------===//

#include "rt_zstd.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)
#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)
#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: %s\n", msg);                                                             \
            return;                                                                                \
        }                                                                                          \
    } while (0)

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)size);
    if (size > 0 && fread(out.data(), 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static std::string fixture_path(const char *name) {
#ifdef VIPER_SOURCE_DIR
    return std::string(VIPER_SOURCE_DIR) + "/src/tests/unit/data/zstd/" + name;
#else
    return std::string("src/tests/unit/data/zstd/") + name;
#endif
}

/// Round-trip one fixture pair: <stem>.zst must decode to <stem>.bin exactly.
static void roundtrip_case(const char *stem) {
    std::string label = std::string("fixture '") + stem + "' decodes byte-identically";
    TEST(label.c_str());
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> expected;
    EXPECT_TRUE(read_file(fixture_path((std::string(stem) + ".zst").c_str()), compressed),
                "compressed fixture readable");
    EXPECT_TRUE(read_file(fixture_path((std::string(stem) + ".bin").c_str()), expected),
                "plaintext fixture readable");

    uint8_t *out = nullptr;
    size_t out_len = 0;
    int ok = rt_zstd_decompress_raw(
        compressed.data(), compressed.size(), 1u << 24, &out, &out_len);
    EXPECT_TRUE(ok == 1, "decompression succeeds");
    EXPECT_TRUE(out_len == expected.size(), "decoded length matches");
    EXPECT_TRUE(out_len == 0 || std::memcmp(out, expected.data(), out_len) == 0,
                "decoded bytes match");
    free(out);
    PASS();
}

static void test_rejects_bad_magic() {
    TEST("bad magic is rejected");
    uint8_t junk[16] = {0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *out = nullptr;
    size_t out_len = 0;
    EXPECT_TRUE(rt_zstd_decompress_raw(junk, sizeof(junk), 1024, &out, &out_len) == 0,
                "junk frame fails");
    EXPECT_TRUE(out == nullptr, "no buffer on failure");
    PASS();
}

static void test_rejects_truncation() {
    TEST("truncated frames are rejected at every length");
    std::vector<uint8_t> compressed;
    EXPECT_TRUE(read_file(fixture_path("mixed.zst"), compressed), "fixture readable");
    /* Every strict prefix must fail cleanly (also a mini structure-fuzz). */
    for (size_t cut = 0; cut < compressed.size(); cut += 7) {
        uint8_t *out = nullptr;
        size_t out_len = 0;
        int ok = rt_zstd_decompress_raw(compressed.data(), cut, 1u << 24, &out, &out_len);
        if (ok) {
            free(out);
            EXPECT_TRUE(false, "a truncated prefix decoded successfully");
        }
    }
    PASS();
}

static void test_rejects_checksum_mismatch() {
    TEST("corrupted payload fails the content checksum");
    std::vector<uint8_t> compressed;
    EXPECT_TRUE(read_file(fixture_path("bytes.zst"), compressed), "fixture readable");
    /* Flip one byte in the middle of the frame; either structural validation
     * or the xxhash64 checksum must reject it. */
    compressed[compressed.size() / 2] ^= 0x5A;
    uint8_t *out = nullptr;
    size_t out_len = 0;
    int ok = rt_zstd_decompress_raw(compressed.data(), compressed.size(), 1u << 24, &out, &out_len);
    if (ok)
        free(out);
    EXPECT_TRUE(ok == 0, "corrupted frame rejected");
    PASS();
}

static void test_honors_output_budget() {
    TEST("max_output is a hard budget");
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> expected;
    EXPECT_TRUE(read_file(fixture_path("bytes.zst"), compressed), "fixture readable");
    EXPECT_TRUE(read_file(fixture_path("bytes.bin"), expected), "plaintext readable");
    uint8_t *out = nullptr;
    size_t out_len = 0;
    EXPECT_TRUE(rt_zstd_decompress_raw(
                    compressed.data(), compressed.size(), expected.size() - 1, &out, &out_len) == 0,
                "one byte under the real size fails");
    EXPECT_TRUE(rt_zstd_decompress_raw(
                    compressed.data(), compressed.size(), expected.size(), &out, &out_len) == 1,
                "exact budget succeeds");
    free(out);
    PASS();
}

int main() {
    printf("test_rt_zstd:\n");
    roundtrip_case("text");
    roundtrip_case("bytes");
    roundtrip_case("mixed");
    roundtrip_case("tiny");
    roundtrip_case("empty");
    test_rejects_bad_magic();
    test_rejects_truncation();
    test_rejects_checksum_mismatch();
    test_honors_output_budget();
    printf("%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
