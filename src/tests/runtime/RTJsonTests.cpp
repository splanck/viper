//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTJsonTests.cpp
// Purpose: Tests for Viper.Text.Json parsing and formatting.
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace
{
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

static const char *str_cstr(rt_string s)
{
    return rt_string_cstr(s);
}

static void assert_str_eq(rt_string s, const char *expected)
{
    const char *actual = str_cstr(s);
    assert(strcmp(actual, expected) == 0);
}

// ============================================================================
// Validation Tests
// ============================================================================

static void test_is_valid_basic()
{
    // Valid JSON - basic structures
    assert(rt_json_is_valid(make_str("null")) == 1);
    assert(rt_json_is_valid(make_str("true")) == 1);
    assert(rt_json_is_valid(make_str("false")) == 1);
    assert(rt_json_is_valid(make_str("123")) == 1);
    assert(rt_json_is_valid(make_str("-45.67")) == 1);
    assert(rt_json_is_valid(make_str("\"hello\"")) == 1);
    assert(rt_json_is_valid(make_str("[]")) == 1);
    assert(rt_json_is_valid(make_str("{}")) == 1);

    // Invalid JSON - empty string
    assert(rt_json_is_valid(make_str("")) == 0);

    // Invalid JSON - wrong first character (not { [ " digit - t f n)
    assert(rt_json_is_valid(make_str("'single'")) == 0);  // single quotes
    assert(rt_json_is_valid(make_str("abc")) == 0);       // random word
    assert(rt_json_is_valid(make_str("@invalid")) == 0);  // special char

    // Note: rt_json_is_valid does basic first-character check only.
    // Full validation happens during parsing.

    printf("test_is_valid_basic: PASSED\n");
}

static void test_is_valid_complex()
{
    // Valid complex structures
    assert(rt_json_is_valid(make_str("[1, 2, 3]")) == 1);
    assert(rt_json_is_valid(make_str("{\"key\": \"value\"}")) == 1);
    assert(rt_json_is_valid(make_str("{\"a\": 1, \"b\": 2}")) == 1);
    assert(rt_json_is_valid(make_str("[{\"x\": 1}, {\"y\": 2}]")) == 1);
    assert(rt_json_is_valid(make_str("{\"nested\": {\"deep\": [1,2,3]}}")) == 1);

    printf("test_is_valid_complex: PASSED\n");
}

// ============================================================================
// Parse Tests
// ============================================================================

static void test_parse_null()
{
    void *result = rt_json_parse(make_str("null"));
    assert(result == nullptr);

    printf("test_parse_null: PASSED\n");
}

static void test_parse_bool()
{
    void *t = rt_json_parse(make_str("true"));
    assert(t != nullptr);
    assert(rt_unbox_i1(t) == 1);

    void *f = rt_json_parse(make_str("false"));
    assert(f != nullptr);
    assert(rt_unbox_i1(f) == 0);

    printf("test_parse_bool: PASSED\n");
}

static void test_parse_number()
{
    void *n1 = rt_json_parse(make_str("42"));
    assert(n1 != nullptr);
    assert(rt_unbox_f64(n1) == 42.0);

    void *n2 = rt_json_parse(make_str("-3.14"));
    assert(n2 != nullptr);
    double v2 = rt_unbox_f64(n2);
    assert(fabs(v2 - (-3.14)) < 0.0001);

    void *n3 = rt_json_parse(make_str("1.5e2"));
    assert(n3 != nullptr);
    assert(rt_unbox_f64(n3) == 150.0);

    printf("test_parse_number: PASSED\n");
}

static void test_parse_string()
{
    void *s1 = rt_json_parse(make_str("\"hello\""));
    assert(s1 != nullptr);
    assert_str_eq((rt_string)s1, "hello");

    void *s2 = rt_json_parse(make_str("\"with\\nescapes\\t\""));
    assert(s2 != nullptr);
    assert_str_eq((rt_string)s2, "with\nescapes\t");

    void *s3 = rt_json_parse(make_str("\"unicode: \\u0041\""));
    assert(s3 != nullptr);
    assert_str_eq((rt_string)s3, "unicode: A");

    printf("test_parse_string: PASSED\n");
}

static void test_parse_array()
{
    void *arr = rt_json_parse(make_str("[1, 2, 3]"));
    assert(arr != nullptr);
    assert(rt_seq_len(arr) == 3);
    assert(rt_unbox_f64(rt_seq_get(arr, 0)) == 1.0);
    assert(rt_unbox_f64(rt_seq_get(arr, 1)) == 2.0);
    assert(rt_unbox_f64(rt_seq_get(arr, 2)) == 3.0);

    // Empty array
    void *empty = rt_json_parse(make_str("[]"));
    assert(empty != nullptr);
    assert(rt_seq_len(empty) == 0);

    // Nested array
    void *nested = rt_json_parse(make_str("[[1, 2], [3, 4]]"));
    assert(nested != nullptr);
    assert(rt_seq_len(nested) == 2);
    void *inner = rt_seq_get(nested, 0);
    assert(rt_seq_len(inner) == 2);

    printf("test_parse_array: PASSED\n");
}

static void test_parse_object()
{
    void *obj = rt_json_parse(make_str("{\"name\": \"Alice\", \"age\": 30}"));
    assert(obj != nullptr);

    void *name = rt_map_get(obj, make_str("name"));
    assert_str_eq((rt_string)name, "Alice");

    void *age = rt_map_get(obj, make_str("age"));
    assert(rt_unbox_f64(age) == 30.0);

    // Empty object
    void *empty = rt_json_parse(make_str("{}"));
    assert(empty != nullptr);

    printf("test_parse_object: PASSED\n");
}

static void test_parse_array_only()
{
    void *arr = rt_json_parse_array(make_str("[1, 2]"));
    assert(arr != nullptr);
    assert(rt_seq_len(arr) == 2);

    // Should trap on non-array
    EXPECT_TRAP(rt_json_parse_array(make_str("{}")));
    EXPECT_TRAP(rt_json_parse_array(make_str("123")));

    printf("test_parse_array_only: PASSED\n");
}

static void test_parse_object_only()
{
    void *obj = rt_json_parse_object(make_str("{\"a\": 1}"));
    assert(obj != nullptr);

    // Should trap on non-object
    EXPECT_TRAP(rt_json_parse_object(make_str("[]")));
    EXPECT_TRAP(rt_json_parse_object(make_str("123")));

    printf("test_parse_object_only: PASSED\n");
}

// ============================================================================
// Format Tests
// ============================================================================

static void test_format_null()
{
    rt_string result = rt_json_format(nullptr);
    assert_str_eq(result, "null");

    printf("test_format_null: PASSED\n");
}

static void test_format_bool()
{
    rt_string t = rt_json_format(rt_box_i1(1));
    assert_str_eq(t, "true");

    rt_string f = rt_json_format(rt_box_i1(0));
    assert_str_eq(f, "false");

    printf("test_format_bool: PASSED\n");
}

static void test_format_number()
{
    rt_string n1 = rt_json_format(rt_box_f64(42.0));
    // Should contain "42" somewhere in the output
    assert(strstr(str_cstr(n1), "42") != nullptr);

    rt_string n2 = rt_json_format(rt_box_f64(3.14));
    // Should contain "3.14" or similar
    assert(strstr(str_cstr(n2), "3.14") != nullptr);

    printf("test_format_number: PASSED\n");
}

static void test_format_string()
{
    rt_string s = rt_json_format((void *)make_str("hello"));
    assert_str_eq(s, "\"hello\"");

    // String with escapes
    rt_string s2 = rt_json_format((void *)make_str("line\nbreak"));
    assert(strstr(str_cstr(s2), "\\n") != nullptr);

    printf("test_format_string: PASSED\n");
}

static void test_format_array()
{
    void *arr = rt_seq_new();
    rt_seq_push(arr, rt_box_f64(1.0));
    rt_seq_push(arr, rt_box_f64(2.0));
    rt_seq_push(arr, rt_box_f64(3.0));

    rt_string result = rt_json_format(arr);
    assert_str_eq(result, "[1,2,3]");

    printf("test_format_array: PASSED\n");
}

static void test_format_object()
{
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("x"), rt_box_f64(10.0));

    rt_string result = rt_json_format(obj);
    // Should contain "x" and "10"
    const char *s = str_cstr(result);
    assert(strstr(s, "\"x\"") != nullptr);
    assert(strstr(s, "10") != nullptr);

    printf("test_format_object: PASSED\n");
}

static void test_format_pretty()
{
    void *arr = rt_seq_new();
    rt_seq_push(arr, rt_box_f64(1.0));
    rt_seq_push(arr, rt_box_f64(2.0));

    rt_string result = rt_json_format_pretty(arr, 2);
    const char *s = str_cstr(result);
    // Pretty format should contain newlines and indentation
    assert(strstr(s, "\n") != nullptr);

    printf("test_format_pretty: PASSED\n");
}

// ============================================================================
// Round-Trip Tests
// ============================================================================

static void test_roundtrip()
{
    // Parse then format should produce equivalent JSON
    const char *json = "{\"name\":\"test\",\"value\":42}";
    void *parsed = rt_json_parse(make_str(json));
    rt_string formatted = rt_json_format(parsed);

    // Parse the formatted version
    void *reparsed = rt_json_parse(formatted);

    // Check values match
    void *name1 = rt_map_get(parsed, make_str("name"));
    void *name2 = rt_map_get(reparsed, make_str("name"));
    assert_str_eq((rt_string)name1, str_cstr((rt_string)name2));

    printf("test_roundtrip: PASSED\n");
}

// ============================================================================
// Type Of Tests
// ============================================================================

static void test_type_of()
{
    assert_str_eq(rt_json_type_of(nullptr), "null");
    assert_str_eq(rt_json_type_of((void *)make_str("hi")), "string");
    assert_str_eq(rt_json_type_of(rt_box_f64(1.0)), "number");
    assert_str_eq(rt_json_type_of(rt_seq_new()), "array");
    assert_str_eq(rt_json_type_of(rt_map_new()), "object");

    printf("test_type_of: PASSED\n");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

static void test_parse_invalid_traps()
{
    EXPECT_TRAP(rt_json_parse(make_str("")));
    EXPECT_TRAP(rt_json_parse(make_str("invalid")));
    EXPECT_TRAP(rt_json_parse(make_str("[1,2,]")));
    EXPECT_TRAP(rt_json_parse(make_str("{\"a\":")));

    printf("test_parse_invalid_traps: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    // Validation
    test_is_valid_basic();
    test_is_valid_complex();

    // Parsing
    test_parse_null();
    test_parse_bool();
    test_parse_number();
    test_parse_string();
    test_parse_array();
    test_parse_object();
    test_parse_array_only();
    test_parse_object_only();

    // Formatting
    test_format_null();
    test_format_bool();
    test_format_number();
    test_format_string();
    test_format_array();
    test_format_object();
    test_format_pretty();

    // Round-trip
    test_roundtrip();

    // Type detection
    test_type_of();

    // Error handling
    test_parse_invalid_traps();

    printf("\nAll JSON tests passed!\n");
    return 0;
}
