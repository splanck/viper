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

#include "rt_box.h"
#include "rt_csv.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s) {
    return rt_const_cstr(s);
}

static const char *str_cstr(rt_string s) {
    return rt_string_cstr(s);
}

static void assert_str_eq(rt_string s, const char *expected) {
    const char *actual = str_cstr(s);
    assert(strcmp(actual, expected) == 0);
}

static void assert_bytes_eq(rt_string s, const char *expected, size_t expected_len) {
    assert(rt_str_len(s) == (int64_t)expected_len);
    assert(memcmp(rt_string_cstr(s), expected, expected_len) == 0);
}

// ============================================================================
// ParseLine Tests
// ============================================================================

static void test_parse_line_simple() {
    void *fields = rt_csv_parse_line(make_str("a,b,c"));

    assert(rt_seq_len(fields) == 3);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "b");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "c");

    printf("test_parse_line_simple: PASSED\n");
}

static void test_parse_line_quoted() {
    void *fields = rt_csv_parse_line(make_str("\"hello\",world,\"test\""));

    assert(rt_seq_len(fields) == 3);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "hello");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "world");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "test");

    printf("test_parse_line_quoted: PASSED\n");
}

static void test_parse_line_escaped_quotes() {
    // CSV escapes quotes by doubling them: ""
    void *fields = rt_csv_parse_line(make_str("\"He said \"\"Hello\"\"\""));

    assert(rt_seq_len(fields) == 1);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "He said \"Hello\"");

    printf("test_parse_line_escaped_quotes: PASSED\n");
}

static void test_parse_line_embedded_comma() {
    void *fields = rt_csv_parse_line(make_str("\"a,b\",c"));

    assert(rt_seq_len(fields) == 2);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a,b");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "c");

    printf("test_parse_line_embedded_comma: PASSED\n");
}

static void test_parse_line_empty_fields() {
    void *fields = rt_csv_parse_line(make_str("a,,c,"));

    assert(rt_seq_len(fields) == 4);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "c");
    assert_str_eq((rt_string)rt_seq_get(fields, 3), "");

    printf("test_parse_line_empty_fields: PASSED\n");
}

static void test_parse_line_custom_delimiter() {
    void *fields = rt_csv_parse_line_with(make_str("a;b;c"), make_str(";"));

    assert(rt_seq_len(fields) == 3);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "b");
    assert_str_eq((rt_string)rt_seq_get(fields, 2), "c");

    printf("test_parse_line_custom_delimiter: PASSED\n");
}

static void test_parse_line_null_or_empty_delimiter_defaults() {
    void *fields = rt_csv_parse_line_with(make_str("a,b"), NULL);

    assert(rt_seq_len(fields) == 2);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "a");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "b");

    fields = rt_csv_parse_line_with(make_str("x,y"), make_str(""));
    assert(rt_seq_len(fields) == 2);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "x");
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "y");

    printf("test_parse_line_null_or_empty_delimiter_defaults: PASSED\n");
}

static void test_parse_line_embedded_nul() {
    const char input[] = {'a', '\0', ',', 'b'};
    void *fields = rt_csv_parse_line(rt_string_from_bytes(input, sizeof(input)));

    assert(rt_seq_len(fields) == 2);
    const char first[] = {'a', '\0'};
    assert_bytes_eq((rt_string)rt_seq_get(fields, 0), first, sizeof(first));
    assert_str_eq((rt_string)rt_seq_get(fields, 1), "b");

    printf("test_parse_line_embedded_nul: PASSED\n");
}

static void test_parse_line_rejects_extra_records() {
    EXPECT_TRAP(rt_csv_parse_line(make_str("a,b\nc,d")));

    printf("test_parse_line_rejects_extra_records: PASSED\n");
}

// ============================================================================
// Parse (multi-line) Tests
// ============================================================================

static void test_parse_multiline() {
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

static void test_parse_newline_in_quotes() {
    void *rows = rt_csv_parse(make_str("\"line1\nline2\",b"));

    assert(rt_seq_len(rows) == 1);

    void *row0 = rt_seq_get(rows, 0);
    assert(rt_seq_len(row0) == 2);
    assert_str_eq((rt_string)rt_seq_get(row0, 0), "line1\nline2");
    assert_str_eq((rt_string)rt_seq_get(row0, 1), "b");

    printf("test_parse_newline_in_quotes: PASSED\n");
}

static void test_parse_crlf() {
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

static void test_format_line_simple() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a"));
    rt_seq_push(fields, (void *)make_str("b"));
    rt_seq_push(fields, (void *)make_str("c"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "a,b,c");

    printf("test_format_line_simple: PASSED\n");
}

static void test_format_line_needs_quoting() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a,b"));
    rt_seq_push(fields, (void *)make_str("c"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "\"a,b\",c");

    printf("test_format_line_needs_quoting: PASSED\n");
}

static void test_format_line_escape_quotes() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("He said \"Hello\""));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "\"He said \"\"Hello\"\"\"");

    printf("test_format_line_escape_quotes: PASSED\n");
}

static void test_format_line_newline() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("line1\nline2"));
    rt_seq_push(fields, (void *)make_str("b"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "\"line1\nline2\",b");

    printf("test_format_line_newline: PASSED\n");
}

static void test_format_line_custom_delimiter() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a"));
    rt_seq_push(fields, (void *)make_str("b"));
    rt_seq_push(fields, (void *)make_str("c"));

    rt_string result = rt_csv_format_line_with(fields, make_str(";"));
    assert_str_eq(result, "a;b;c");

    printf("test_format_line_custom_delimiter: PASSED\n");
}

static void test_format_line_null_delimiter_defaults() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a"));
    rt_seq_push(fields, (void *)make_str("b"));

    rt_string result = rt_csv_format_line_with(fields, NULL);
    assert_str_eq(result, "a,b");

    result = rt_csv_format_line_with(fields, make_str(""));
    assert_str_eq(result, "a,b");

    printf("test_format_line_null_delimiter_defaults: PASSED\n");
}

static void test_format_line_null_field_is_empty() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, NULL);
    rt_seq_push(fields, (void *)make_str("b"));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, ",b");

    printf("test_format_line_null_field_is_empty: PASSED\n");
}

static void test_format_line_embedded_nul() {
    void *fields = rt_seq_new();
    const char field[] = {'a', '\0', ',', 'b'};
    rt_seq_push(fields, (void *)rt_string_from_bytes(field, sizeof(field)));

    rt_string result = rt_csv_format_line(fields);
    const char expected[] = {'"', 'a', '\0', ',', 'b', '"'};
    assert_bytes_eq(result, expected, sizeof(expected));

    printf("test_format_line_embedded_nul: PASSED\n");
}

static void test_format_line_boxed_non_strings() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, rt_box_i64(42));
    rt_seq_push(fields, rt_box_i1(1));

    rt_string result = rt_csv_format_line(fields);
    assert_str_eq(result, "42,True");

    printf("test_format_line_boxed_non_strings: PASSED\n");
}

static void test_invalid_delimiter_traps() {
    void *fields = rt_seq_new();
    rt_seq_push(fields, (void *)make_str("a"));

    EXPECT_TRAP(rt_csv_parse_line_with(make_str("a\"b"), make_str("\"")));
    EXPECT_TRAP(rt_csv_parse_line_with(make_str("a\nb"), make_str("\n")));
    EXPECT_TRAP(rt_csv_format_line_with(fields, make_str("\"")));

    printf("test_invalid_delimiter_traps: PASSED\n");
}

// ============================================================================
// Format (multi-line) Tests
// ============================================================================

static void test_format_multiline() {
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

static void test_roundtrip_simple() {
    const char *original = "a,b,c";
    void *parsed = rt_csv_parse_line(make_str(original));
    rt_string formatted = rt_csv_format_line(parsed);
    assert_str_eq(formatted, original);

    printf("test_roundtrip_simple: PASSED\n");
}

static void test_roundtrip_complex() {
    const char *original = "\"quoted,field\",normal,\"with \"\"escaped\"\"\"";
    void *parsed = rt_csv_parse_line(make_str(original));
    rt_string formatted = rt_csv_format_line(parsed);
    assert_str_eq(formatted, original);

    printf("test_roundtrip_complex: PASSED\n");
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_empty_input() {
    void *fields = rt_csv_parse_line(make_str(""));
    assert(rt_seq_len(fields) == 1);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "");

    void *rows = rt_csv_parse(make_str(""));
    assert(rt_seq_len(rows) == 0);

    printf("test_empty_input: PASSED\n");
}

static void test_single_field() {
    void *fields = rt_csv_parse_line(make_str("hello"));
    assert(rt_seq_len(fields) == 1);
    assert_str_eq((rt_string)rt_seq_get(fields, 0), "hello");

    printf("test_single_field: PASSED\n");
}

static void test_invalid_char_after_closing_quote_traps() {
    EXPECT_TRAP(rt_csv_parse_line(make_str("\"a\"b")));

    printf("test_invalid_char_after_closing_quote_traps: PASSED\n");
}

static void test_unterminated_quoted_field_traps() {
    EXPECT_TRAP(rt_csv_parse_line(make_str("\"unterminated")));
    EXPECT_TRAP(rt_csv_parse(make_str("a,\"unterminated\nnext,row")));

    printf("test_unterminated_quoted_field_traps: PASSED\n");
}

static void test_quote_in_unquoted_field_traps() {
    EXPECT_TRAP(rt_csv_parse_line(make_str("a,b\"c")));

    printf("test_quote_in_unquoted_field_traps: PASSED\n");
}

static void test_is_valid_reports_malformed_without_trapping() {
    assert(rt_csv_is_valid(make_str("a,b\nc,d\n")) == 1);
    assert(rt_csv_is_valid(make_str("\"line1\nline2\",b")) == 1);
    assert(rt_csv_is_valid(make_str("\"unterminated")) == 0);
    assert(rt_csv_is_valid(make_str("\"a\"b")) == 0);
    assert(rt_csv_is_valid(make_str("a,b\"c")) == 0);

    printf("test_is_valid_reports_malformed_without_trapping: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Viper.Text.Csv Tests ===\n\n");

    // ParseLine tests
    test_parse_line_simple();
    test_parse_line_quoted();
    test_parse_line_escaped_quotes();
    test_parse_line_embedded_comma();
    test_parse_line_empty_fields();
    test_parse_line_custom_delimiter();
    test_parse_line_null_or_empty_delimiter_defaults();
    test_parse_line_embedded_nul();
    test_parse_line_rejects_extra_records();

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
    test_format_line_null_delimiter_defaults();
    test_format_line_null_field_is_empty();
    test_format_line_embedded_nul();
    test_format_line_boxed_non_strings();
    test_invalid_delimiter_traps();

    // Format (multi-line) tests
    test_format_multiline();

    // Roundtrip tests
    test_roundtrip_simple();
    test_roundtrip_complex();

    // Edge cases
    test_empty_input();
    test_single_field();
    test_invalid_char_after_closing_quote_traps();
    test_unterminated_quoted_field_traps();
    test_quote_in_unquoted_field_traps();
    test_is_valid_reports_malformed_without_trapping();

    printf("\nAll RTCsvTests passed!\n");
    return 0;
}
