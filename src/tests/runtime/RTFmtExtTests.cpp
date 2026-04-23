//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTFmtExtTests.cpp
// Purpose: Tests for extended Viper.Fmt functions (IntGrouped, Currency,
//          ToWords, Ordinal).
//
//===----------------------------------------------------------------------===//

#include "rt_fmt.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cmath>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static rt_string make_bytes(const char *s, size_t len) {
    return rt_string_from_bytes(s, len);
}

static bool str_eq(rt_string s, const char *expected) {
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

static bool bytes_eq(rt_string s, const char *expected, size_t len) {
    const char *data = rt_string_cstr(s);
    return data && (size_t)rt_str_len(s) == len && memcmp(data, expected, len) == 0;
}

// ---------------------------------------------------------------------------
// IntGrouped tests
// ---------------------------------------------------------------------------

static void test_grouped_basic() {
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(1234567, sep);
    assert(str_eq(r, "1,234,567"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_small() {
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(42, sep);
    assert(str_eq(r, "42"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_zero() {
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(0, sep);
    assert(str_eq(r, "0"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_negative() {
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(-1234567, sep);
    assert(str_eq(r, "-1,234,567"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_dot_separator() {
    rt_string sep = make_str(".");
    rt_string r = rt_fmt_int_grouped(1000000, sep);
    assert(str_eq(r, "1.000.000"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_exact_thousand() {
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(1000, sep);
    assert(str_eq(r, "1,000"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_embedded_nul_separator() {
    const char sep_bytes[] = {':', '\0', ':'};
    rt_string sep = make_bytes(sep_bytes, sizeof(sep_bytes));
    rt_string r = rt_fmt_int_grouped(1234567, sep);
    const char expected[] = {'1', ':', '\0', ':', '2', '3', '4',
                             ':', '\0', ':', '5', '6', '7'};
    assert(bytes_eq(r, expected, sizeof(expected)));
    rt_string_unref(r);
    rt_string_unref(sep);
}

// ---------------------------------------------------------------------------
// Currency tests
// ---------------------------------------------------------------------------

static void test_currency_basic() {
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(1234.56, 2, sym);
    assert(str_eq(r, "$1,234.56"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_zero_decimals() {
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(1234.0, 0, sym);
    assert(str_eq(r, "$1,234"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_negative() {
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(-99.99, 2, sym);
    assert(str_eq(r, "-$99.99"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_euro() {
    rt_string sym = make_str("EUR ");
    rt_string r = rt_fmt_currency(42.50, 2, sym);
    assert(str_eq(r, "EUR 42.50"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_empty_symbol() {
    rt_string sym = make_str("");
    rt_string r = rt_fmt_currency(42.50, 2, sym);
    assert(str_eq(r, "42.50"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_rounding_carry() {
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(1.999, 2, sym);
    assert(str_eq(r, "$2.00"));
    rt_string_unref(r);

    r = rt_fmt_currency(-1.999, 2, sym);
    assert(str_eq(r, "-$2.00"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_negative_rounds_to_zero() {
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(-0.004, 2, sym);
    assert(str_eq(r, "$0.00"));
    rt_string_unref(r);

    r = rt_fmt_currency(-0.0, 2, sym);
    assert(str_eq(r, "$0.00"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_special_values() {
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(NAN, 2, sym);
    assert(str_eq(r, "NaN"));
    rt_string_unref(r);

    r = rt_fmt_currency(INFINITY, 2, sym);
    assert(str_eq(r, "Infinity"));
    rt_string_unref(r);

    r = rt_fmt_currency(-INFINITY, 2, sym);
    assert(str_eq(r, "-Infinity"));
    rt_string_unref(r);

    r = rt_fmt_currency(DBL_MAX, 2, sym);
    assert((size_t)rt_str_len(r) > 300);
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_embedded_nul_symbol() {
    const char sym_bytes[] = {'$', '\0', 'U', 'S', 'D'};
    rt_string sym = make_bytes(sym_bytes, sizeof(sym_bytes));
    rt_string r = rt_fmt_currency(42.5, 2, sym);
    const char expected[] = {'$', '\0', 'U', 'S', 'D', '4', '2', '.', '5', '0'};
    assert(bytes_eq(r, expected, sizeof(expected)));
    rt_string_unref(r);
    rt_string_unref(sym);
}

// ---------------------------------------------------------------------------
// ToWords tests
// ---------------------------------------------------------------------------

static void test_words_zero() {
    rt_string r = rt_fmt_to_words(0);
    assert(str_eq(r, "zero"));
    rt_string_unref(r);
}

static void test_words_small() {
    rt_string r = rt_fmt_to_words(5);
    assert(str_eq(r, "five"));
    rt_string_unref(r);
}

static void test_words_teens() {
    rt_string r = rt_fmt_to_words(13);
    assert(str_eq(r, "thirteen"));
    rt_string_unref(r);
}

static void test_words_tens() {
    rt_string r = rt_fmt_to_words(42);
    assert(str_eq(r, "forty-two"));
    rt_string_unref(r);
}

static void test_words_hundred() {
    rt_string r = rt_fmt_to_words(100);
    assert(str_eq(r, "one hundred"));
    rt_string_unref(r);
}

static void test_words_complex() {
    rt_string r = rt_fmt_to_words(1234);
    assert(str_eq(r, "one thousand two hundred thirty-four"));
    rt_string_unref(r);
}

static void test_words_million() {
    rt_string r = rt_fmt_to_words(1000000);
    assert(str_eq(r, "one million"));
    rt_string_unref(r);
}

static void test_words_quadrillion() {
    rt_string r = rt_fmt_to_words(1000000000000000LL);
    assert(str_eq(r, "one quadrillion"));
    rt_string_unref(r);
}

static void test_words_quintillion() {
    rt_string r = rt_fmt_to_words(1000000000000000000LL);
    assert(str_eq(r, "one quintillion"));
    rt_string_unref(r);
}

static void test_words_int64_max() {
    rt_string r = rt_fmt_to_words(INT64_MAX);
    assert(str_eq(r,
                  "nine quintillion two hundred twenty-three quadrillion three hundred "
                  "seventy-two trillion thirty-six billion eight hundred fifty-four million "
                  "seven hundred seventy-five thousand eight hundred seven"));
    rt_string_unref(r);
}

static void test_words_negative() {
    rt_string r = rt_fmt_to_words(-7);
    assert(str_eq(r, "negative seven"));
    rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// Ordinal tests
// ---------------------------------------------------------------------------

static void test_ordinal_1() {
    rt_string r = rt_fmt_ordinal(1);
    assert(str_eq(r, "1st"));
    rt_string_unref(r);
}

static void test_ordinal_2() {
    rt_string r = rt_fmt_ordinal(2);
    assert(str_eq(r, "2nd"));
    rt_string_unref(r);
}

static void test_ordinal_3() {
    rt_string r = rt_fmt_ordinal(3);
    assert(str_eq(r, "3rd"));
    rt_string_unref(r);
}

static void test_ordinal_4() {
    rt_string r = rt_fmt_ordinal(4);
    assert(str_eq(r, "4th"));
    rt_string_unref(r);
}

static void test_ordinal_11() {
    rt_string r = rt_fmt_ordinal(11);
    assert(str_eq(r, "11th"));
    rt_string_unref(r);
}

static void test_ordinal_12() {
    rt_string r = rt_fmt_ordinal(12);
    assert(str_eq(r, "12th"));
    rt_string_unref(r);
}

static void test_ordinal_13() {
    rt_string r = rt_fmt_ordinal(13);
    assert(str_eq(r, "13th"));
    rt_string_unref(r);
}

static void test_ordinal_21() {
    rt_string r = rt_fmt_ordinal(21);
    assert(str_eq(r, "21st"));
    rt_string_unref(r);
}

static void test_ordinal_101() {
    rt_string r = rt_fmt_ordinal(101);
    assert(str_eq(r, "101st"));
    rt_string_unref(r);
}

static void test_ordinal_111() {
    rt_string r = rt_fmt_ordinal(111);
    assert(str_eq(r, "111th"));
    rt_string_unref(r);
}

static void test_ordinal_int64_min() {
    rt_string r = rt_fmt_ordinal(INT64_MIN);
    assert(str_eq(r, "-9223372036854775808th"));
    rt_string_unref(r);
}

int main() {
    // IntGrouped
    test_grouped_basic();
    test_grouped_small();
    test_grouped_zero();
    test_grouped_negative();
    test_grouped_dot_separator();
    test_grouped_exact_thousand();
    test_grouped_embedded_nul_separator();

    // Currency
    test_currency_basic();
    test_currency_zero_decimals();
    test_currency_negative();
    test_currency_euro();
    test_currency_empty_symbol();
    test_currency_rounding_carry();
    test_currency_negative_rounds_to_zero();
    test_currency_special_values();
    test_currency_embedded_nul_symbol();

    // ToWords
    test_words_zero();
    test_words_small();
    test_words_teens();
    test_words_tens();
    test_words_hundred();
    test_words_complex();
    test_words_million();
    test_words_quadrillion();
    test_words_quintillion();
    test_words_int64_max();
    test_words_negative();

    // Ordinal
    test_ordinal_1();
    test_ordinal_2();
    test_ordinal_3();
    test_ordinal_4();
    test_ordinal_11();
    test_ordinal_12();
    test_ordinal_13();
    test_ordinal_21();
    test_ordinal_101();
    test_ordinal_111();
    test_ordinal_int64_min();

    return 0;
}
