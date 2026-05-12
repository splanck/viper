//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTParseTests.cpp
// Purpose: Tests for Viper.Parse safe parsing functions.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_fmt.h"
#include "rt_numeric.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_parse.h"
#include "rt_string.h"

#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s) {
    return rt_const_cstr(s);
}

static rt_string make_bytes(const char *s, size_t len) {
    return rt_string_from_bytes(s, len);
}

// ============================================================================
// TryInt Tests
// ============================================================================

static void test_try_int_valid() {
    int64_t result = 0;
    assert(rt_parse_try_int(make_str("42"), &result) == 1);
    assert(result == 42);

    assert(rt_parse_try_int(make_str("-123"), &result) == 1);
    assert(result == -123);

    assert(rt_parse_try_int(make_str("  100  "), &result) == 1);
    assert(result == 100);

    assert(rt_parse_try_int(make_str("0"), &result) == 1);
    assert(result == 0);

    printf("test_try_int_valid: PASSED\n");
}

static void test_try_int_invalid() {
    int64_t result = 999;
    assert(rt_parse_try_int(make_str(""), &result) == 0);
    assert(rt_parse_try_int(make_str("abc"), &result) == 0);
    assert(rt_parse_try_int(make_str("12.34"), &result) == 0);
    assert(rt_parse_try_int(make_str("12abc"), &result) == 0);
    assert(rt_parse_try_int(make_str("   "), &result) == 0);

    printf("test_try_int_invalid: PASSED\n");
}

// ============================================================================
// TryNum Tests
// ============================================================================

static void test_try_num_valid() {
    double result = 0.0;
    assert(rt_parse_try_num(make_str("3.14"), &result) == 1);
    assert(fabs(result - 3.14) < 0.001);

    assert(rt_parse_try_num(make_str("-2.5"), &result) == 1);
    assert(fabs(result - (-2.5)) < 0.001);

    assert(rt_parse_try_num(make_str("42"), &result) == 1);
    assert(fabs(result - 42.0) < 0.001);

    assert(rt_parse_try_num(make_str("1e10"), &result) == 1);
    assert(fabs(result - 1e10) < 1.0);

    assert(rt_parse_try_num(make_str("  .5  "), &result) == 1);
    assert(fabs(result - 0.5) < 0.001);

    printf("test_try_num_valid: PASSED\n");
}

static void test_try_num_invalid() {
    double result = 999.0;
    assert(rt_parse_try_num(make_str(""), &result) == 0);
    assert(rt_parse_try_num(make_str("abc"), &result) == 0);
    assert(rt_parse_try_num(make_str("12.34.56"), &result) == 0);
    assert(rt_parse_try_num(make_str("   "), &result) == 0);
    assert(rt_parse_try_num(make_str("0x1p4"), &result) == 0);
    assert(rt_parse_try_num(make_str("1e"), &result) == 0);

    printf("test_try_num_invalid: PASSED\n");
}

static void test_low_level_double_rejects_hex_float() {
    double result = 123.0;
    assert(rt_parse_double("0x1p4", &result) == (int32_t)Err_InvalidCast);
    assert(result == 0.0);
    assert(rt_parse_double("16.0", &result) == (int32_t)Err_None);
    assert(result == 16.0);

    printf("test_low_level_double_rejects_hex_float: PASSED\n");
}

static void test_low_level_parse_failures_zero_output() {
    int64_t ivalue = 123;
    double dvalue = 123.0;
    assert(rt_parse_int64("12x", &ivalue) == (int32_t)Err_InvalidCast);
    assert(ivalue == 0);
    assert(rt_parse_int64("999999999999999999999999", &ivalue) == (int32_t)Err_Overflow);
    assert(ivalue == 0);
    assert(rt_parse_double("1e9999", &dvalue) == (int32_t)Err_Overflow);
    assert(dvalue == 0.0);
    assert(rt_parse_double("", &dvalue) == (int32_t)Err_InvalidCast);
    assert(dvalue == 0.0);

    printf("test_low_level_parse_failures_zero_output: PASSED\n");
}

static void test_public_parse_string_wrappers() {
    int64_t ivalue = 0;
    double dvalue = 0.0;
    rt_string int_text = make_str(" +42 ");
    rt_string dbl_text = make_str(" 6.5 ");
    assert(rt_parse_int64_str(int_text, &ivalue) == (int32_t)Err_None);
    assert(ivalue == 42);
    assert(rt_parse_double_str(dbl_text, &dvalue) == (int32_t)Err_None);
    assert(fabs(dvalue - 6.5) < 0.001);
    rt_string_unref(int_text);
    rt_string_unref(dbl_text);

    const char raw[] = {'1', '\0', '2'};
    rt_string embedded = rt_string_from_bytes(raw, sizeof(raw));
    ivalue = 123;
    dvalue = 123.0;
    assert(rt_parse_int64_str(embedded, &ivalue) == (int32_t)Err_InvalidCast);
    assert(ivalue == 0);
    assert(rt_parse_double_str(embedded, &dvalue) == (int32_t)Err_InvalidCast);
    assert(dvalue == 0.0);
    rt_string_unref(embedded);

    printf("test_public_parse_string_wrappers: PASSED\n");
}

static void test_parse_option_wrappers() {
    void *num = rt_parse_double_option(make_str("6.25"));
    assert(rt_obj_class_id(num) == RT_OPTION_CLASS_ID);
    assert(rt_option_is_some(num) == 1);
    assert(fabs(rt_option_unwrap_f64(num) - 6.25) < 0.001);

    void *bad_num = rt_parse_double_option(make_str("nan"));
    assert(rt_obj_class_id(bad_num) == RT_OPTION_CLASS_ID);
    assert(rt_option_is_some(bad_num) == 0);

    void *ival = rt_parse_int64_option(make_str("64"));
    assert(rt_obj_class_id(ival) == RT_OPTION_CLASS_ID);
    assert(rt_option_is_some(ival) == 1);
    assert(rt_option_unwrap_i64(ival) == 64);

    void *bad_int = rt_parse_int64_option(make_str("64x"));
    assert(rt_obj_class_id(bad_int) == RT_OPTION_CLASS_ID);
    assert(rt_option_is_some(bad_int) == 0);

    if (rt_obj_release_check0(num))
        rt_obj_free(num);
    if (rt_obj_release_check0(bad_num))
        rt_obj_free(bad_num);
    if (rt_obj_release_check0(ival))
        rt_obj_free(ival);
    if (rt_obj_release_check0(bad_int))
        rt_obj_free(bad_int);

    printf("test_parse_option_wrappers: PASSED\n");
}

// ============================================================================
// TryBool Tests
// ============================================================================

static void test_try_bool_true_values() {
    int8_t result = 0;
    assert(rt_parse_try_bool(make_str("true"), &result) == 1);
    assert(result == 1);

    assert(rt_parse_try_bool(make_str("TRUE"), &result) == 1);
    assert(result == 1);

    assert(rt_parse_try_bool(make_str("True"), &result) == 1);
    assert(result == 1);

    assert(rt_parse_try_bool(make_str("yes"), &result) == 1);
    assert(result == 1);

    assert(rt_parse_try_bool(make_str("YES"), &result) == 1);
    assert(result == 1);

    assert(rt_parse_try_bool(make_str("1"), &result) == 1);
    assert(result == 1);

    assert(rt_parse_try_bool(make_str("on"), &result) == 1);
    assert(result == 1);

    printf("test_try_bool_true_values: PASSED\n");
}

static void test_try_bool_false_values() {
    int8_t result = 1;
    assert(rt_parse_try_bool(make_str("false"), &result) == 1);
    assert(result == 0);

    assert(rt_parse_try_bool(make_str("FALSE"), &result) == 1);
    assert(result == 0);

    assert(rt_parse_try_bool(make_str("no"), &result) == 1);
    assert(result == 0);

    assert(rt_parse_try_bool(make_str("NO"), &result) == 1);
    assert(result == 0);

    assert(rt_parse_try_bool(make_str("0"), &result) == 1);
    assert(result == 0);

    assert(rt_parse_try_bool(make_str("off"), &result) == 1);
    assert(result == 0);

    printf("test_try_bool_false_values: PASSED\n");
}

static void test_try_bool_invalid() {
    int8_t result = 1;
    assert(rt_parse_try_bool(make_str(""), &result) == 0);
    assert(rt_parse_try_bool(make_str("abc"), &result) == 0);
    assert(rt_parse_try_bool(make_str("maybe"), &result) == 0);
    assert(rt_parse_try_bool(make_str("2"), &result) == 0);
    assert(rt_parse_try_bool(make_str("   "), &result) == 0);

    printf("test_try_bool_invalid: PASSED\n");
}

// ============================================================================
// IntOr Tests
// ============================================================================

static void test_int_or() {
    assert(rt_parse_int_or(make_str("42"), -1) == 42);
    assert(rt_parse_int_or(make_str("-100"), 0) == -100);
    assert(rt_parse_int_or(make_str("abc"), -1) == -1);
    assert(rt_parse_int_or(make_str(""), 99) == 99);
    assert(rt_parse_int_or(make_str("12.34"), 50) == 50);

    printf("test_int_or: PASSED\n");
}

// ============================================================================
// NumOr Tests
// ============================================================================

static void test_num_or() {
    assert(fabs(rt_parse_num_or(make_str("3.14"), -1.0) - 3.14) < 0.001);
    assert(fabs(rt_parse_num_or(make_str("-2.5"), 0.0) - (-2.5)) < 0.001);
    assert(fabs(rt_parse_num_or(make_str("abc"), -1.0) - (-1.0)) < 0.001);
    assert(fabs(rt_parse_num_or(make_str(""), 99.9) - 99.9) < 0.001);

    printf("test_num_or: PASSED\n");
}

// ============================================================================
// BoolOr Tests
// ============================================================================

static void test_bool_or() {
    assert(rt_parse_bool_or(make_str("true"), 0) == 1);
    assert(rt_parse_bool_or(make_str("false"), 1) == 0);
    assert(rt_parse_bool_or(make_str("abc"), 1) == 1);
    assert(rt_parse_bool_or(make_str("abc"), 0) == 0);
    assert(rt_parse_bool_or(make_str("yes"), 0) == 1);
    assert(rt_parse_bool_or(make_str("no"), 1) == 0);

    printf("test_bool_or: PASSED\n");
}

// ============================================================================
// IsInt Tests
// ============================================================================

static void test_is_int() {
    assert(rt_parse_is_int(make_str("42")) == 1);
    assert(rt_parse_is_int(make_str("-123")) == 1);
    assert(rt_parse_is_int(make_str("  100  ")) == 1);
    assert(rt_parse_is_int(make_str("0")) == 1);
    assert(rt_parse_is_int(make_str("abc")) == 0);
    assert(rt_parse_is_int(make_str("12.34")) == 0);
    assert(rt_parse_is_int(make_str("")) == 0);

    printf("test_is_int: PASSED\n");
}

// ============================================================================
// IsNum Tests
// ============================================================================

static void test_is_num() {
    assert(rt_parse_is_num(make_str("3.14")) == 1);
    assert(rt_parse_is_num(make_str("-2.5")) == 1);
    assert(rt_parse_is_num(make_str("42")) == 1);
    assert(rt_parse_is_num(make_str("1e10")) == 1);
    assert(rt_parse_is_num(make_str("abc")) == 0);
    assert(rt_parse_is_num(make_str("")) == 0);

    printf("test_is_num: PASSED\n");
}

// ============================================================================
// IntRadix Tests
// ============================================================================

static void test_int_radix() {
    // Binary
    assert(rt_parse_int_radix(make_str("1010"), 2, -1) == 10);
    assert(rt_parse_int_radix(make_str("11111111"), 2, -1) == 255);

    // Octal
    assert(rt_parse_int_radix(make_str("77"), 8, -1) == 63);
    assert(rt_parse_int_radix(make_str("10"), 8, -1) == 8);

    // Decimal
    assert(rt_parse_int_radix(make_str("42"), 10, -1) == 42);
    assert(rt_parse_int_radix(make_str("+10"), 10, -1) == 10);
    assert(rt_parse_int_radix(make_str("-42"), 10, -1) == -42);
    assert(rt_parse_int_radix(make_str("-9223372036854775808"), 10, -1) == INT64_MIN);

    // Hexadecimal
    assert(rt_parse_int_radix(make_str("FF"), 16, -1) == 255);
    assert(rt_parse_int_radix(make_str("ff"), 16, -1) == 255);
    assert(rt_parse_int_radix(make_str("DEADBEEF"), 16, -1) == 0xDEADBEEF);

    // Base 36
    assert(rt_parse_int_radix(make_str("Z"), 36, -1) == 35);
    assert(rt_parse_int_radix(make_str("10"), 36, -1) == 36);

    // Invalid radix returns default
    assert(rt_parse_int_radix(make_str("42"), 1, -1) == -1);
    assert(rt_parse_int_radix(make_str("42"), 37, -1) == -1);
    assert(rt_parse_int_radix(make_str("42"), 0, -1) == -1);

    // Invalid string returns default
    assert(rt_parse_int_radix(make_str("GG"), 16, -1) == -1);
    assert(rt_parse_int_radix(make_str(""), 10, -1) == -1);
    assert(rt_parse_int_radix(make_str("0x10"), 16, -1) == -1);
    assert(rt_parse_int_radix(make_str("+FF"), 16, -1) == -1);
    assert(rt_parse_int_radix(make_str("-2a"), 16, -1) == -1);
    assert(rt_parse_int_radix(make_str("9223372036854775808"), 10, -1) == -1);

    // Non-decimal radices parse full 64-bit bit patterns so Fmt output round-trips.
    rt_string hex_minus_one = rt_fmt_hex(-1);
    assert(rt_parse_int_radix(hex_minus_one, 16, 0) == -1);
    rt_string_unref(hex_minus_one);

    rt_string bin_minus_one = rt_fmt_bin(-1);
    assert(rt_parse_int_radix(bin_minus_one, 2, 0) == -1);
    rt_string_unref(bin_minus_one);

    printf("test_int_radix: PASSED\n");
}

// ============================================================================
// NULL Input Tests
// ============================================================================

static void test_null_inputs() {
    int64_t i = 123;
    double d = 4.5;
    int8_t b = 1;

    assert(rt_parse_try_int(NULL, &i) == 0);
    assert(i == 123);
    assert(rt_parse_try_num(NULL, &d) == 0);
    assert(fabs(d - 4.5) < 0.001);
    assert(rt_parse_try_bool(NULL, &b) == 0);
    assert(b == 1);

    assert(rt_parse_int_or(NULL, 7) == 7);
    assert(fabs(rt_parse_num_or(NULL, 2.5) - 2.5) < 0.001);
    assert(rt_parse_bool_or(NULL, 1) == 1);
    assert(rt_parse_is_int(NULL) == 0);
    assert(rt_parse_is_num(NULL) == 0);
    assert(rt_parse_int_radix(NULL, 10, 99) == 99);

    rt_string bogus = (rt_string)(uintptr_t)1;
    assert(rt_string_is_handle((const void *)bogus) == 0);
    assert(rt_parse_try_int(bogus, &i) == 0);
    assert(rt_parse_try_num(bogus, &d) == 0);
    assert(rt_parse_try_bool(bogus, &b) == 0);
    assert(rt_parse_is_int(bogus) == 0);
    assert(rt_parse_is_num(bogus) == 0);
    assert(rt_parse_int_radix(bogus, 10, 99) == 99);

    printf("test_null_inputs: PASSED\n");
}

// ============================================================================
// Embedded NUL Tests
// ============================================================================

static void test_embedded_nul_inputs() {
    const char int_bytes[] = {'1', '2', '3', '\0', 'j', 'u', 'n', 'k'};
    const char num_bytes[] = {'1', '.', '5', '\0', 'j', 'u', 'n', 'k'};
    const char bool_bytes[] = {'t', 'r', 'u', 'e', '\0', 'j', 'u', 'n', 'k'};

    rt_string int_s = make_bytes(int_bytes, sizeof(int_bytes));
    rt_string num_s = make_bytes(num_bytes, sizeof(num_bytes));
    rt_string bool_s = make_bytes(bool_bytes, sizeof(bool_bytes));

    int64_t i = 999;
    double d = 9.0;
    int8_t b = 0;

    assert(rt_parse_try_int(int_s, &i) == 0);
    assert(i == 999);
    assert(rt_parse_try_num(num_s, &d) == 0);
    assert(fabs(d - 9.0) < 0.001);
    assert(rt_parse_try_bool(bool_s, &b) == 0);
    assert(b == 0);

    assert(rt_parse_int_or(int_s, -7) == -7);
    assert(fabs(rt_parse_num_or(num_s, -7.5) - (-7.5)) < 0.001);
    assert(rt_parse_bool_or(bool_s, 1) == 1);
    assert(rt_parse_is_int(int_s) == 0);
    assert(rt_parse_is_num(num_s) == 0);
    assert(rt_parse_int_radix(int_s, 10, -8) == -8);

    rt_string_unref(int_s);
    rt_string_unref(num_s);
    rt_string_unref(bool_s);

    printf("test_embedded_nul_inputs: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Viper.Parse Tests ===\n\n");

    // TryInt
    test_try_int_valid();
    test_try_int_invalid();

    // TryNum
    test_try_num_valid();
    test_try_num_invalid();
    test_low_level_double_rejects_hex_float();
    test_low_level_parse_failures_zero_output();
    test_public_parse_string_wrappers();
    test_parse_option_wrappers();

    // TryBool
    test_try_bool_true_values();
    test_try_bool_false_values();
    test_try_bool_invalid();

    // XxxOr variants
    test_int_or();
    test_num_or();
    test_bool_or();

    // IsXxx variants
    test_is_int();
    test_is_num();

    // IntRadix
    test_int_radix();

    // NULL input handling
    test_null_inputs();
    test_embedded_nul_inputs();

    printf("\nAll RTParseTests passed!\n");
    return 0;
}
