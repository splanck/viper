// test_font.c - Font engine unit tests
#include "vg_font.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

//=============================================================================
// Test Utilities
//=============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing %s... ", #name); \
    fflush(stdout)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { FAIL(#cond " is false"); return; } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { FAIL(#a " != " #b); return; } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { FAIL(#ptr " is NULL"); return; } \
} while(0)

//=============================================================================
// UTF-8 Tests
//=============================================================================

static void test_utf8_decode_ascii(void) {
    TEST(utf8_decode_ascii);

    const char* str = "Hello";
    uint32_t cp = vg_utf8_decode(&str);
    ASSERT_EQ(cp, 'H');

    cp = vg_utf8_decode(&str);
    ASSERT_EQ(cp, 'e');

    PASS();
}

static void test_utf8_decode_2byte(void) {
    TEST(utf8_decode_2byte);

    const char* str = "\xC3\xA9";  // é (U+00E9)
    uint32_t cp = vg_utf8_decode(&str);
    ASSERT_EQ(cp, 0xE9);

    PASS();
}

static void test_utf8_decode_3byte(void) {
    TEST(utf8_decode_3byte);

    const char* str = "\xE4\xB8\xAD";  // 中 (U+4E2D)
    uint32_t cp = vg_utf8_decode(&str);
    ASSERT_EQ(cp, 0x4E2D);

    PASS();
}

static void test_utf8_decode_4byte(void) {
    TEST(utf8_decode_4byte);

    const char* str = "\xF0\x9F\x98\x80";  // 😀 (U+1F600)
    uint32_t cp = vg_utf8_decode(&str);
    ASSERT_EQ(cp, 0x1F600);

    PASS();
}

static void test_utf8_strlen(void) {
    TEST(utf8_strlen);

    ASSERT_EQ(vg_utf8_strlen("Hello"), 5);
    ASSERT_EQ(vg_utf8_strlen("Héllo"), 5);
    ASSERT_EQ(vg_utf8_strlen("中文"), 2);
    ASSERT_EQ(vg_utf8_strlen(""), 0);

    PASS();
}

static void test_utf8_offset(void) {
    TEST(utf8_offset);

    ASSERT_EQ(vg_utf8_offset("Hello", 0), 0);
    ASSERT_EQ(vg_utf8_offset("Hello", 1), 1);
    ASSERT_EQ(vg_utf8_offset("Hello", 5), 5);

    // Multi-byte characters
    ASSERT_EQ(vg_utf8_offset("Héllo", 0), 0);
    ASSERT_EQ(vg_utf8_offset("Héllo", 1), 1);
    ASSERT_EQ(vg_utf8_offset("Héllo", 2), 3);  // After 'é' (2 bytes)

    PASS();
}

//=============================================================================
// Font Loading Tests (require actual font file)
//=============================================================================

static void test_font_load_null(void) {
    TEST(font_load_null);

    vg_font_t* font = vg_font_load(NULL, 0);
    ASSERT_TRUE(font == NULL);

    PASS();
}

static void test_font_load_empty(void) {
    TEST(font_load_empty);

    uint8_t empty[1] = {0};
    vg_font_t* font = vg_font_load(empty, 1);
    ASSERT_TRUE(font == NULL);

    PASS();
}

static void test_font_destroy_null(void) {
    TEST(font_destroy_null);

    // Should not crash
    vg_font_destroy(NULL);

    PASS();
}

//=============================================================================
// Integration Test (if font available)
//=============================================================================

#ifdef TEST_FONT_PATH
static void test_font_load_file(void) {
    TEST(font_load_file);

    vg_font_t* font = vg_font_load_file(TEST_FONT_PATH);
    ASSERT_NOT_NULL(font);

    // Check family name
    const char* family = vg_font_get_family(font);
    ASSERT_NOT_NULL(family);
    printf("(Family: %s) ", family);

    // Check metrics
    vg_font_metrics_t metrics;
    vg_font_get_metrics(font, 16.0f, &metrics);
    ASSERT_TRUE(metrics.ascent > 0);
    ASSERT_TRUE(metrics.line_height > 0);

    // Check glyph lookup
    ASSERT_TRUE(vg_font_has_glyph(font, 'A'));
    ASSERT_TRUE(vg_font_has_glyph(font, 'Z'));

    // Get glyph
    const vg_glyph_t* glyph = vg_font_get_glyph(font, 16.0f, 'A');
    ASSERT_NOT_NULL(glyph);
    ASSERT_TRUE(glyph->advance > 0);

    // Measure text
    vg_text_metrics_t text_metrics;
    vg_font_measure_text(font, 16.0f, "Hello", &text_metrics);
    ASSERT_TRUE(text_metrics.width > 0);
    ASSERT_EQ(text_metrics.glyph_count, 5);

    // Hit test
    int idx = vg_font_hit_test(font, 16.0f, "Hello", text_metrics.width / 2);
    ASSERT_TRUE(idx >= 0 && idx <= 5);

    // Cursor position
    float cursor_x = vg_font_get_cursor_x(font, 16.0f, "Hello", 2);
    ASSERT_TRUE(cursor_x > 0);

    vg_font_destroy(font);

    PASS();
}
#endif

//=============================================================================
// Main
//=============================================================================

int main(int argc, char** argv) {
    printf("Viper GUI Font Engine Tests\n");
    printf("============================\n\n");

    printf("UTF-8 Tests:\n");
    test_utf8_decode_ascii();
    test_utf8_decode_2byte();
    test_utf8_decode_3byte();
    test_utf8_decode_4byte();
    test_utf8_strlen();
    test_utf8_offset();

    printf("\nFont Loading Tests:\n");
    test_font_load_null();
    test_font_load_empty();
    test_font_destroy_null();

#ifdef TEST_FONT_PATH
    printf("\nIntegration Tests:\n");
    test_font_load_file();
#else
    printf("\nNote: Define TEST_FONT_PATH to enable font file tests\n");
#endif

    printf("\n============================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
