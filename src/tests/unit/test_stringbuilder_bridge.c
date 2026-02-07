//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_stringbuilder_bridge.c
// Purpose: C test harness for StringBuilder OOP bridge and runtime integration.
// Key invariants: Tests are self-contained; do not require global runtime init;
//                 exercise append/clear/ToString and capacity behavior.
// Ownership/Lifetime: Test binary manages any allocations created via bridge APIs.
// Links: docs/runtime-stringbuilder.md
//
//===----------------------------------------------------------------------===//
// Test harness for StringBuilder bridge functions
// Tests the OOP StringBuilder <-> internal rt_string_builder bridge

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rt_ns_bridge.h"
#include "rt_string_builder.h"

// Runtime initialization not needed for these tests

// Helper to create string from C string
/// What: Create a runtime string from a C string.
/// Why:  Simplify tests by avoiding repetitive conversions.
/// How:  Uses rt_string_from_bytes when non-null, otherwise returns rt_str_empty().
static rt_string make_string(const char *s)
{
    return s ? rt_string_from_bytes(s, strlen(s)) : rt_str_empty();
}

// Test helpers
static bool test_passed = false;
static int tests_run = 0;
static int tests_failed = 0;

#define TEST_START(name)                                                                           \
    printf("  Testing %s... ", name);                                                              \
    fflush(stdout);                                                                                \
    test_passed = true;                                                                            \
    tests_run++;

#define TEST_END()                                                                                 \
    if (test_passed)                                                                               \
    {                                                                                              \
        printf("✓\n");                                                                             \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
        printf("✗\n");                                                                             \
        tests_failed++;                                                                            \
    }

#define ASSERT_EQ(expected, actual)                                                                \
    do                                                                                             \
    {                                                                                              \
        if ((expected) != (actual))                                                                \
        {                                                                                          \
            printf("\n    FAILED: Expected %lld, got %lld at line %d\n",                           \
                   (long long)(expected),                                                          \
                   (long long)(actual),                                                            \
                   __LINE__);                                                                      \
            test_passed = false;                                                                   \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ(expected, actual)                                                            \
    do                                                                                             \
    {                                                                                              \
        if (strcmp((expected), (actual)) != 0)                                                     \
        {                                                                                          \
            printf("\n    FAILED: Expected '%s', got '%s' at line %d\n",                           \
                   (expected),                                                                     \
                   (actual),                                                                       \
                   __LINE__);                                                                      \
            test_passed = false;                                                                   \
        }                                                                                          \
    } while (0)

#define ASSERT_TRUE(cond)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            printf("\n    FAILED: Condition false at line %d\n", __LINE__);                        \
            test_passed = false;                                                                   \
        }                                                                                          \
    } while (0)

// Tests
void test_new_and_initial_state(void)
{
    TEST_START("new and initial state");

    void *sb = rt_ns_stringbuilder_new();
    ASSERT_TRUE(sb != NULL);
    ASSERT_EQ(0, rt_text_sb_get_length(sb));

    // Capacity should be non-zero (inline buffer)
    int64_t cap = rt_text_sb_get_capacity(sb);
    ASSERT_TRUE(cap > 0);

    // ToString on empty should give empty string
    rt_string result = rt_text_sb_to_string(sb);
    ASSERT_EQ(0, rt_str_len(result));

    TEST_END();
}

void test_append_single(void)
{
    TEST_START("append single string");

    void *sb = rt_ns_stringbuilder_new();
    rt_string hello = make_string("Hello");

    void *ret = rt_text_sb_append(sb, hello);
    ASSERT_TRUE(ret == sb); // Should return self for chaining
    ASSERT_EQ(5, rt_text_sb_get_length(sb));

    rt_string result = rt_text_sb_to_string(sb);
    ASSERT_EQ(5, rt_str_len(result));
    ASSERT_STR_EQ("Hello", rt_string_cstr(result));

    TEST_END();
}

void test_append_multiple(void)
{
    TEST_START("append multiple strings");

    void *sb = rt_ns_stringbuilder_new();

    rt_text_sb_append(sb, make_string("Hello"));
    rt_text_sb_append(sb, make_string(", "));
    rt_text_sb_append(sb, make_string("World"));
    rt_text_sb_append(sb, make_string("!"));

    ASSERT_EQ(13, rt_text_sb_get_length(sb));

    rt_string result = rt_text_sb_to_string(sb);
    ASSERT_EQ(13, rt_str_len(result));
    ASSERT_STR_EQ("Hello, World!", rt_string_cstr(result));

    TEST_END();
}

void test_append_line(void)
{
    TEST_START("append line");

    void *sb = rt_ns_stringbuilder_new();

    void *ret = rt_text_sb_append_line(sb, make_string("a"));
    ASSERT_TRUE(ret == sb);
    ret = rt_text_sb_append_line(sb, make_string("b"));
    ASSERT_TRUE(ret == sb);

    ASSERT_EQ(4, rt_text_sb_get_length(sb));

    rt_string result = rt_text_sb_to_string(sb);
    ASSERT_EQ(4, rt_str_len(result));
    ASSERT_STR_EQ("a\nb\n", rt_string_cstr(result));

    TEST_END();
}

void test_clear(void)
{
    TEST_START("clear operation");

    void *sb = rt_ns_stringbuilder_new();
    rt_text_sb_append(sb, make_string("Test content"));

    ASSERT_EQ(12, rt_text_sb_get_length(sb));
    int64_t cap_before = rt_text_sb_get_capacity(sb);

    rt_text_sb_clear(sb);

    ASSERT_EQ(0, rt_text_sb_get_length(sb));
    // Capacity should remain unchanged after clear
    ASSERT_EQ(cap_before, rt_text_sb_get_capacity(sb));

    // Should be able to append after clear
    rt_text_sb_append(sb, make_string("New"));
    ASSERT_EQ(3, rt_text_sb_get_length(sb));

    rt_string result = rt_text_sb_to_string(sb);
    ASSERT_STR_EQ("New", rt_string_cstr(result));

    TEST_END();
}

void test_capacity_growth(void)
{
    TEST_START("capacity growth");

    void *sb = rt_ns_stringbuilder_new();
    int64_t initial_cap = rt_text_sb_get_capacity(sb);

    // Append enough to force growth
    char large_text[1024];
    memset(large_text, 'A', sizeof(large_text) - 1);
    large_text[sizeof(large_text) - 1] = '\0';

    rt_string large = make_string(large_text);
    rt_text_sb_append(sb, large);

    int64_t new_cap = rt_text_sb_get_capacity(sb);
    ASSERT_TRUE(new_cap > initial_cap);
    ASSERT_TRUE(new_cap >= 1023); // Must fit the content
    ASSERT_EQ(1023, rt_text_sb_get_length(sb));

    TEST_END();
}

void test_empty_append(void)
{
    TEST_START("append empty string");

    void *sb = rt_ns_stringbuilder_new();
    rt_text_sb_append(sb, make_string("Start"));

    rt_string empty = rt_str_empty();
    rt_text_sb_append(sb, empty);

    ASSERT_EQ(5, rt_text_sb_get_length(sb));

    rt_string result = rt_text_sb_to_string(sb);
    ASSERT_STR_EQ("Start", rt_string_cstr(result));

    TEST_END();
}

void test_method_chaining(void)
{
    TEST_START("method chaining");

    void *sb = rt_ns_stringbuilder_new();

    // Chain multiple appends
    void *result = rt_text_sb_append(
        rt_text_sb_append(rt_text_sb_append(sb, make_string("A")), make_string("B")),
        make_string("C"));

    ASSERT_TRUE(result == sb);
    ASSERT_EQ(3, rt_text_sb_get_length(sb));

    rt_string str = rt_text_sb_to_string(sb);
    ASSERT_STR_EQ("ABC", rt_string_cstr(str));

    TEST_END();
}

void test_toString_preserves_state(void)
{
    TEST_START("ToString preserves builder state");

    void *sb = rt_ns_stringbuilder_new();
    rt_text_sb_append(sb, make_string("Test"));

    // First ToString
    rt_string result1 = rt_text_sb_to_string(sb);
    ASSERT_STR_EQ("Test", rt_string_cstr(result1));
    ASSERT_EQ(4, rt_text_sb_get_length(sb)); // Length unchanged

    // Can still append
    rt_text_sb_append(sb, make_string("123"));

    // Second ToString shows accumulated content
    rt_string result2 = rt_text_sb_to_string(sb);
    ASSERT_STR_EQ("Test123", rt_string_cstr(result2));
    ASSERT_EQ(7, rt_text_sb_get_length(sb));

    TEST_END();
}

int main(void)
{
    printf("\n=== StringBuilder Bridge Tests ===\n\n");

    // Run tests
    test_new_and_initial_state();
    test_append_single();
    test_append_multiple();
    test_append_line();
    test_clear();
    test_capacity_growth();
    test_empty_append();
    test_method_chaining();
    test_toString_preserves_state();

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_run - tests_failed);
    printf("Tests failed: %d\n", tests_failed);

    if (tests_failed == 0)
    {
        printf("\nAll tests PASSED! ✓\n");
        return 0;
    }
    else
    {
        printf("\nSome tests FAILED! ✗\n");
        return 1;
    }
}
