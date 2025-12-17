//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCsvTests.cpp
// Purpose: Tests for Viper.Text.Csv parsing and formatting.
//
//===----------------------------------------------------------------------===//

#include "rt_csv.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

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
// ParseLine Tests
// ============================================================================

static void test_parse_line_simple()
{
    void *fields = rt_csv_parse_line(make_str("a,b,c"));

    assert(rt_seq_len(fields) == 3);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "b");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "c");

    printf("test_parse_line_simple: PASSED\n");
}

static void test_parse_line_quoted()
{
    void *fields = rt_csv_parse_line(make_str("\"hello\",world,\"test\""));

    assert(rt_seq_len(fields) == 3);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "hello");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "world");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "test");

    printf("test_parse_line_quoted: PASSED\n");
}

static void test_parse_line_escaped_quotes()
{
    // CSV escapes quotes by doubling them: ""
    void *fields = rt_csv_parse_line(make_str("\"He said \"\"Hello\"\"\""));

    assert(rt_seq_len(fields) == 1);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "He said \"Hello\"");

    printf("test_parse_line_escaped_quotes: PASSED\n");
}

static void test_parse_line_embedded_comma()
{
    void *fields = rt_csv_parse_line(make_str("\"a,b\",c"));

    assert(rt_seq_len(fields) == 2);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a,b");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "c");

    printf("test_parse_line_embedded_comma: PASSED\n");
}

static void test_parse_line_empty_fields()
{
    void *fields = rt_csv_parse_line(make_str("a,,c,"));

    assert(rt_seq_len(fields) == 4);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "c");
    assert_str_eq((rt_string)rt_seq_get(fields, 3), "");

    printf("test_parse_line_empty_fields: PASSED\n");
}

static void test_parse_line_custom_delimiter()
{
    void *fields = rt_csv_parse_line_with(make_str("a;b;c"), make_str(";"));

    assert(rt_seq_len(fields) == 3);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "b");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "c");

    printf("test_parse_line_custom_delimiter: PASSED\n");
}

// ============================================================================
// Parse (multi-line) Tests
// ============================================================================

static void test_parse_multiline()
{
    void *rows = rt_csv_parse(make_str("a,b,c\n1,2,3\nx,y,z"));

    assert(rt_seq_len(rows) == 3);

    void *row0 = rt_seq_get(rows, 0);
    void *row1 = rt_seq_get(rows, 1);
    void *row2 = rt_seq_get(rows, 2);

    assert(rt_seq_len(row0) == 3);
    assert(rt_seq_len(row1) == 3);
    assert(rt_seq_len(row2) == 3);

    assert_str_eq((rt_string)rt_seq_get(row0, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(row1, 1), "2");
    assert_str_eq((rt_string)rt_seq_get(row2, 2), "z");

    printf("test_parse_multiline: PASSED\n");
}

static void test_parse_newline_in_quotes()
{
    void *rows = rt_csv_parse(make_str("\"line1\nline2\",b"));

    assert(rt_seq_len(rows) == 1);

    void *row0 = rt_seq_get(rows, 0);
    assert(rt_seq_len(row0) == 2);
    assert_str_eq((rt_string)rt_seq_get(row0, 0), "line1\nline2");
    assert_str_eq((rt_string)rt_seq_get(row0, 1), "b");

    printf("test_parse_newline_in_quotes: PASSED\n");
}

static void test_parse_crlf()
{
    void *rows = rt_csv_parse(make_str("a,b\r\nc,d"));

    assert(rt_seq_len(rows) == 2);

    void *row0 = rt_seq_get(rows, 0);
    void *row1 = rt_seq_get(rows, 1);

    assert_str_eq((rt_string)rt_seq_get(row0, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(row0, 1), "b");
    assert_str_eq((rt_string)rt_seq_get(row1, 0), "c");
    assert_str_eq((rt_string)rt_seq_get(row1, 1), "d");

    printf("test_parse_crlf: PASSED\n");
}

// ============================================================================
// FormatLine Tests
// ============================================================================

static void test_format_line_simple()
{
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a"));
    rt_seq_push(fields, (void *)make_str("b"));
    rt_seq_push(fields, (void *)make_str("c"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "a,b,c");

    printf("test_format_line_simple: PASSED\n");
}

static void test_format_line_needs_quoting()
{
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a,b"));
    rt_seq_push(fields, (void *)make_str("c"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "\"a,b\",c");

    printf("test_format_line_needs_quoting: PASSED\n");
}

static void test_format_line_escape_quotes()
{
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("He said \"Hello\""));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "\"He said \"\"Hello\"\"\"");

    printf("test_format_line_escape_quotes: PASSED\n");
}

static void test_format_line_newline()
{
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("line1\nline2"));
    rt_seq_push(fields, (void *)make_str("b"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "\"line1\nline2\",b");

    printf("test_format_line_newline: PASSED\n");
}

static void test_format_line_custom_delimiter()
{
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a"));
    rt_seq_push(fields, (void *)make_str("b"));
    rt_seq_push(fields, (void *)make_str("c"));

    rt_string result = rt_csv_format_line_with(fields, make_str(";"));
    assert_str_eq(result, "a;b;c");

    printf("test_format_line_custom_delimiter: PASSED\n");
}

// ============================================================================
// Format (multi-line) Tests
// ============================================================================

static void test_format_multiline()
{
    void *rows = rt_seq_new();

    void *row1 = rt_seq_new();
    rt_seq_push(row1, (void *)make_str("a"));
    rt_seq_push(row1, (void *)make_str("b"));

    void *row2 = rt_seq_new();
    rt_seq_push(row2, (void *)make_str("c"));
    rt_seq_push(row2, (void *)make_str("d"));

    rt_seq_push(rows, row1);
    rt_seq_push(rows, row2);

    rt_string result = rt_csv_format(rows);
    assert_str_eq(result, "a,b\nc,d\n");

    printf("test_format_multiline: PASSED\n");
}

// ============================================================================
// Roundtrip Tests
// ============================================================================

static void test_roundtrip_simple()
{
    const char *original = "a,b,c";
    void *parsed = rt_csv_parse_line(make_str(original));
    rt_string formatted = rt_csv_format_line(parsed);
    assert_str_eq(formatted, original);

    printf("test_roundtrip_simple: PASSED\n");
}

static void test_roundtrip_complex()
{
    const char *original = "\"quoted,field\",normal,\"with \"\"escaped\"\"\"";
    void *parsed = rt_csv_parse_line(make_str(original));
    rt_string formatted = rt_csv_format_line(parsed);
    assert_str_eq(formatted, original);

    printf("test_roundtrip_complex: PASSED\n");
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_empty_input()
{
    void *fields = rt_csv_parse_line(make_str(""));
    assert(rt_seq_len(fields) == 1);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "");

    void *rows = rt_csv_parse(make_str(""));
    assert(rt_seq_len(rows) == 0);

    printf("test_empty_input: PASSED\n");
}

static void test_single_field()
{
    void *fields = rt_csv_parse_line(make_str("hello"));
    assert(rt_seq_len(fields) == 1);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "hello");

    printf("test_single_field: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Text.Csv Tests ===\n\n");

    // ParseLine tests
    test_parse_line_simple();
    test_parse_line_quoted();
    test_parse_line_escaped_quotes();
    test_parse_line_embedded_comma();
    test_parse_line_empty_fields();
    test_parse_line_custom_delimiter();

    // Parse (multi-line) tests
    test_parse_multiline();
    test_parse_newline_in_quotes();
    test_parse_crlf();

    // FormatLine tests
    test_format_line_simple();
    test_format_line_needs_quoting();
    test_format_line_escape_quotes();
    test_format_line_newline();
    test_format_line_custom_delimiter();

    // Format (multi-line) tests
    test_format_multiline();

    // Roundtrip tests
    test_roundtrip_simple();
    test_roundtrip_complex();

    // Edge cases
    test_empty_input();
    test_single_field();

    printf("\nAll RTCsvTests passed!\n");
    return 0;
}
