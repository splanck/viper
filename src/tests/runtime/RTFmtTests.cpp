//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTFmtTests.cpp
// Purpose: Tests for Viper.Fmt value formatting functions.
//
//===----------------------------------------------------------------------===//

#include "rt_fmt.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
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

static bool str_eq(rt_string s, const char *expected)
{
    const char *data = rt_string_cstr(s);
    return data != nullptr && strcmp(data, expected) == 0;
}

// ============================================================================
// Int Tests
// ============================================================================

static void test_fmt_int()
{
    assert(str_eq(rt_fmt_int(42), "42"));
    assert(str_eq(rt_fmt_int(-123), "-123"));
    assert(str_eq(rt_fmt_int(0), "0"));
    assert(str_eq(rt_fmt_int(1000000), "1000000"));
    assert(str_eq(rt_fmt_int(-9223372036854775807LL), "-9223372036854775807"));

    printf("test_fmt_int: PASSED\n");
}

static void test_fmt_int_radix()
{
    // Binary
    assert(str_eq(rt_fmt_int_radix(10, 2), "1010"));
    assert(str_eq(rt_fmt_int_radix(255, 2), "11111111"));

    // Octal
    assert(str_eq(rt_fmt_int_radix(63, 8), "77"));
    assert(str_eq(rt_fmt_int_radix(8, 8), "10"));

    // Decimal
    assert(str_eq(rt_fmt_int_radix(42, 10), "42"));
    assert(str_eq(rt_fmt_int_radix(-42, 10), "-42"));

    // Hexadecimal
    assert(str_eq(rt_fmt_int_radix(255, 16), "ff"));
    assert(str_eq(rt_fmt_int_radix(0xDEADBEEF, 16), "deadbeef"));

    // Base 36
    assert(str_eq(rt_fmt_int_radix(35, 36), "z"));
    assert(str_eq(rt_fmt_int_radix(36, 36), "10"));

    // Zero
    assert(str_eq(rt_fmt_int_radix(0, 2), "0"));
    assert(str_eq(rt_fmt_int_radix(0, 16), "0"));

    // Invalid radix returns empty
    assert(str_eq(rt_fmt_int_radix(42, 1), ""));
    assert(str_eq(rt_fmt_int_radix(42, 37), ""));

    printf("test_fmt_int_radix: PASSED\n");
}

static void test_fmt_int_pad()
{
    assert(str_eq(rt_fmt_int_pad(42, 5, make_str("0")), "00042"));
    assert(str_eq(rt_fmt_int_pad(42, 5, make_str(" ")), "   42"));
    assert(str_eq(rt_fmt_int_pad(-42, 5, make_str("0")), "-0042"));
    assert(str_eq(rt_fmt_int_pad(-42, 5, make_str(" ")), "  -42"));
    assert(str_eq(rt_fmt_int_pad(12345, 3, make_str("0")), "12345")); // No truncation
    assert(str_eq(rt_fmt_int_pad(7, 1, make_str("0")), "7"));

    printf("test_fmt_int_pad: PASSED\n");
}

// ============================================================================
// Num Tests
// ============================================================================

static void test_fmt_num()
{
    rt_string s = rt_fmt_num(3.14159);
    const char *data = rt_string_cstr(s);
    assert(data != nullptr);
    // %g format removes trailing zeros
    assert(strstr(data, "3.14") != nullptr);

    assert(str_eq(rt_fmt_num(42.0), "42"));
    assert(str_eq(rt_fmt_num(0.0), "0"));
    assert(str_eq(rt_fmt_num(NAN), "NaN"));
    assert(str_eq(rt_fmt_num(INFINITY), "Infinity"));
    assert(str_eq(rt_fmt_num(-INFINITY), "-Infinity"));

    printf("test_fmt_num: PASSED\n");
}

static void test_fmt_num_fixed()
{
    assert(str_eq(rt_fmt_num_fixed(3.14159, 2), "3.14"));
    assert(str_eq(rt_fmt_num_fixed(3.14159, 0), "3"));
    assert(str_eq(rt_fmt_num_fixed(3.14159, 4), "3.1416"));
    assert(str_eq(rt_fmt_num_fixed(42.0, 2), "42.00"));
    assert(str_eq(rt_fmt_num_fixed(NAN, 2), "NaN"));

    printf("test_fmt_num_fixed: PASSED\n");
}

static void test_fmt_num_sci()
{
    rt_string s = rt_fmt_num_sci(1234.5, 2);
    const char *data = rt_string_cstr(s);
    assert(data != nullptr);
    // Scientific notation: something like 1.23e+03
    assert(strstr(data, "1.23") != nullptr);
    assert(strstr(data, "e") != nullptr);

    assert(str_eq(rt_fmt_num_sci(NAN, 2), "NaN"));
    assert(str_eq(rt_fmt_num_sci(INFINITY, 2), "Infinity"));

    printf("test_fmt_num_sci: PASSED\n");
}

static void test_fmt_num_pct()
{
    assert(str_eq(rt_fmt_num_pct(0.5, 0), "50%"));
    assert(str_eq(rt_fmt_num_pct(0.5, 2), "50.00%"));
    assert(str_eq(rt_fmt_num_pct(1.0, 0), "100%"));
    assert(str_eq(rt_fmt_num_pct(0.0, 1), "0.0%"));
    assert(str_eq(rt_fmt_num_pct(0.123, 1), "12.3%"));
    assert(str_eq(rt_fmt_num_pct(NAN, 2), "NaN%"));

    printf("test_fmt_num_pct: PASSED\n");
}

// ============================================================================
// Bool Tests
// ============================================================================

static void test_fmt_bool()
{
    assert(str_eq(rt_fmt_bool(true), "true"));
    assert(str_eq(rt_fmt_bool(false), "false"));

    printf("test_fmt_bool: PASSED\n");
}

static void test_fmt_bool_yn()
{
    assert(str_eq(rt_fmt_bool_yn(true), "yes"));
    assert(str_eq(rt_fmt_bool_yn(false), "no"));

    printf("test_fmt_bool_yn: PASSED\n");
}

// ============================================================================
// Size Tests
// ============================================================================

static void test_fmt_size()
{
    assert(str_eq(rt_fmt_size(0), "0 B"));
    assert(str_eq(rt_fmt_size(100), "100 B"));
    assert(str_eq(rt_fmt_size(1024), "1.0 KB"));
    assert(str_eq(rt_fmt_size(1536), "1.5 KB"));
    assert(str_eq(rt_fmt_size(1048576), "1.0 MB"));
    assert(str_eq(rt_fmt_size(1073741824), "1.0 GB"));

    printf("test_fmt_size: PASSED\n");
}

// ============================================================================
// Hex Tests
// ============================================================================

static void test_fmt_hex()
{
    assert(str_eq(rt_fmt_hex(0), "0"));
    assert(str_eq(rt_fmt_hex(255), "ff"));
    assert(str_eq(rt_fmt_hex(16), "10"));
    assert(str_eq(rt_fmt_hex(0xDEADBEEF), "deadbeef"));

    printf("test_fmt_hex: PASSED\n");
}

static void test_fmt_hex_pad()
{
    assert(str_eq(rt_fmt_hex_pad(255, 4), "00ff"));
    assert(str_eq(rt_fmt_hex_pad(255, 2), "ff"));
    assert(str_eq(rt_fmt_hex_pad(0, 8), "00000000"));
    assert(str_eq(rt_fmt_hex_pad(0xABCD, 8), "0000abcd"));

    printf("test_fmt_hex_pad: PASSED\n");
}

// ============================================================================
// Bin Tests
// ============================================================================

static void test_fmt_bin()
{
    assert(str_eq(rt_fmt_bin(0), "0"));
    assert(str_eq(rt_fmt_bin(1), "1"));
    assert(str_eq(rt_fmt_bin(10), "1010"));
    assert(str_eq(rt_fmt_bin(255), "11111111"));

    printf("test_fmt_bin: PASSED\n");
}

// ============================================================================
// Oct Tests
// ============================================================================

static void test_fmt_oct()
{
    assert(str_eq(rt_fmt_oct(0), "0"));
    assert(str_eq(rt_fmt_oct(8), "10"));
    assert(str_eq(rt_fmt_oct(63), "77"));
    assert(str_eq(rt_fmt_oct(64), "100"));

    printf("test_fmt_oct: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== Viper.Fmt Tests ===\n\n");

    // Int formatting
    test_fmt_int();
    test_fmt_int_radix();
    test_fmt_int_pad();

    // Num formatting
    test_fmt_num();
    test_fmt_num_fixed();
    test_fmt_num_sci();
    test_fmt_num_pct();

    // Bool formatting
    test_fmt_bool();
    test_fmt_bool_yn();

    // Size formatting
    test_fmt_size();

    // Hex formatting
    test_fmt_hex();
    test_fmt_hex_pad();

    // Bin/Oct formatting
    test_fmt_bin();
    test_fmt_oct();

    printf("\nAll RTFmtTests passed!\n");
    return 0;
}
