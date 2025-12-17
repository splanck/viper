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
#include "rt_parse.h"
#include "rt_string.h"

#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>

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

// ============================================================================
// TryInt Tests
// ============================================================================

static void test_try_int_valid()
{
    int64_t result = 0;
    assert(rt_parse_try_int(make_str("42"), &result) == true);
    assert(result == 42);

    assert(rt_parse_try_int(make_str("-123"), &result) == true);
    assert(result == -123);

    assert(rt_parse_try_int(make_str("  100  "), &result) == true);
    assert(result == 100);

    assert(rt_parse_try_int(make_str("0"), &result) == true);
    assert(result == 0);

    printf("test_try_int_valid: PASSED\n");
}

static void test_try_int_invalid()
{
    int64_t result = 999;
    assert(rt_parse_try_int(make_str(""), &result) == false);
    assert(rt_parse_try_int(make_str("abc"), &result) == false);
    assert(rt_parse_try_int(make_str("12.34"), &result) == false);
    assert(rt_parse_try_int(make_str("12abc"), &result) == false);
    assert(rt_parse_try_int(make_str("   "), &result) == false);

    printf("test_try_int_invalid: PASSED\n");
}

// ============================================================================
// TryNum Tests
// ============================================================================

static void test_try_num_valid()
{
    double result = 0.0;
    assert(rt_parse_try_num(make_str("3.14"), &result) == true);
    assert(fabs(result - 3.14) < 0.001);

    assert(rt_parse_try_num(make_str("-2.5"), &result) == true);
    assert(fabs(result - (-2.5)) < 0.001);

    assert(rt_parse_try_num(make_str("42"), &result) == true);
    assert(fabs(result - 42.0) < 0.001);

    assert(rt_parse_try_num(make_str("1e10"), &result) == true);
    assert(fabs(result - 1e10) < 1.0);

    assert(rt_parse_try_num(make_str("  .5  "), &result) == true);
    assert(fabs(result - 0.5) < 0.001);

    printf("test_try_num_valid: PASSED\n");
}

static void test_try_num_invalid()
{
    double result = 999.0;
    assert(rt_parse_try_num(make_str(""), &result) == false);
    assert(rt_parse_try_num(make_str("abc"), &result) == false);
    assert(rt_parse_try_num(make_str("12.34.56"), &result) == false);
    assert(rt_parse_try_num(make_str("   "), &result) == false);

    printf("test_try_num_invalid: PASSED\n");
}

// ============================================================================
// TryBool Tests
// ============================================================================

static void test_try_bool_true_values()
{
    bool result = false;
    assert(rt_parse_try_bool(make_str("true"), &result) == true);
    assert(result == true);

    assert(rt_parse_try_bool(make_str("TRUE"), &result) == true);
    assert(result == true);

    assert(rt_parse_try_bool(make_str("True"), &result) == true);
    assert(result == true);

    assert(rt_parse_try_bool(make_str("yes"), &result) == true);
    assert(result == true);

    assert(rt_parse_try_bool(make_str("YES"), &result) == true);
    assert(result == true);

    assert(rt_parse_try_bool(make_str("1"), &result) == true);
    assert(result == true);

    assert(rt_parse_try_bool(make_str("on"), &result) == true);
    assert(result == true);

    printf("test_try_bool_true_values: PASSED\n");
}

static void test_try_bool_false_values()
{
    bool result = true;
    assert(rt_parse_try_bool(make_str("false"), &result) == true);
    assert(result == false);

    assert(rt_parse_try_bool(make_str("FALSE"), &result) == true);
    assert(result == false);

    assert(rt_parse_try_bool(make_str("no"), &result) == true);
    assert(result == false);

    assert(rt_parse_try_bool(make_str("NO"), &result) == true);
    assert(result == false);

    assert(rt_parse_try_bool(make_str("0"), &result) == true);
    assert(result == false);

    assert(rt_parse_try_bool(make_str("off"), &result) == true);
    assert(result == false);

    printf("test_try_bool_false_values: PASSED\n");
}

static void test_try_bool_invalid()
{
    bool result = true;
    assert(rt_parse_try_bool(make_str(""), &result) == false);
    assert(rt_parse_try_bool(make_str("abc"), &result) == false);
    assert(rt_parse_try_bool(make_str("maybe"), &result) == false);
    assert(rt_parse_try_bool(make_str("2"), &result) == false);
    assert(rt_parse_try_bool(make_str("   "), &result) == false);

    printf("test_try_bool_invalid: PASSED\n");
}

// ============================================================================
// IntOr Tests
// ============================================================================

static void test_int_or()
{
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

static void test_num_or()
{
    assert(fabs(rt_parse_num_or(make_str("3.14"), -1.0) - 3.14) < 0.001);
    assert(fabs(rt_parse_num_or(make_str("-2.5"), 0.0) - (-2.5)) < 0.001);
    assert(fabs(rt_parse_num_or(make_str("abc"), -1.0) - (-1.0)) < 0.001);
    assert(fabs(rt_parse_num_or(make_str(""), 99.9) - 99.9) < 0.001);

    printf("test_num_or: PASSED\n");
}

// ============================================================================
// BoolOr Tests
// ============================================================================

static void test_bool_or()
{
    assert(rt_parse_bool_or(make_str("true"), false) == true);
    assert(rt_parse_bool_or(make_str("false"), true) == false);
    assert(rt_parse_bool_or(make_str("abc"), true) == true);
    assert(rt_parse_bool_or(make_str("abc"), false) == false);
    assert(rt_parse_bool_or(make_str("yes"), false) == true);
    assert(rt_parse_bool_or(make_str("no"), true) == false);

    printf("test_bool_or: PASSED\n");
}

// ============================================================================
// IsInt Tests
// ============================================================================

static void test_is_int()
{
    assert(rt_parse_is_int(make_str("42")) == true);
    assert(rt_parse_is_int(make_str("-123")) == true);
    assert(rt_parse_is_int(make_str("  100  ")) == true);
    assert(rt_parse_is_int(make_str("0")) == true);
    assert(rt_parse_is_int(make_str("abc")) == false);
    assert(rt_parse_is_int(make_str("12.34")) == false);
    assert(rt_parse_is_int(make_str("")) == false);

    printf("test_is_int: PASSED\n");
}

// ============================================================================
// IsNum Tests
// ============================================================================

static void test_is_num()
{
    assert(rt_parse_is_num(make_str("3.14")) == true);
    assert(rt_parse_is_num(make_str("-2.5")) == true);
    assert(rt_parse_is_num(make_str("42")) == true);
    assert(rt_parse_is_num(make_str("1e10")) == true);
    assert(rt_parse_is_num(make_str("abc")) == false);
    assert(rt_parse_is_num(make_str("")) == false);

    printf("test_is_num: PASSED\n");
}

// ============================================================================
// IntRadix Tests
// ============================================================================

static void test_int_radix()
{
    // Binary
    assert(rt_parse_int_radix(make_str("1010"), 2, -1) == 10);
    assert(rt_parse_int_radix(make_str("11111111"), 2, -1) == 255);

    // Octal
    assert(rt_parse_int_radix(make_str("77"), 8, -1) == 63);
    assert(rt_parse_int_radix(make_str("10"), 8, -1) == 8);

    // Decimal
    assert(rt_parse_int_radix(make_str("42"), 10, -1) == 42);

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

    printf("test_int_radix: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Parse Tests ===\n\n");

    // TryInt
    test_try_int_valid();
    test_try_int_invalid();

    // TryNum
    test_try_num_valid();
    test_try_num_invalid();

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

    printf("\nAll RTParseTests passed!\n");
    return 0;
}
