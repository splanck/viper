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
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// ---------------------------------------------------------------------------
// IntGrouped tests
// ---------------------------------------------------------------------------

static void test_grouped_basic()
{
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(1234567, sep);
    assert(str_eq(r, "1,234,567"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_small()
{
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(42, sep);
    assert(str_eq(r, "42"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_zero()
{
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(0, sep);
    assert(str_eq(r, "0"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_negative()
{
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(-1234567, sep);
    assert(str_eq(r, "-1,234,567"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_dot_separator()
{
    rt_string sep = make_str(".");
    rt_string r = rt_fmt_int_grouped(1000000, sep);
    assert(str_eq(r, "1.000.000"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_grouped_exact_thousand()
{
    rt_string sep = make_str(",");
    rt_string r = rt_fmt_int_grouped(1000, sep);
    assert(str_eq(r, "1,000"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

// ---------------------------------------------------------------------------
// Currency tests
// ---------------------------------------------------------------------------

static void test_currency_basic()
{
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(1234.56, 2, sym);
    assert(str_eq(r, "$1,234.56"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_zero_decimals()
{
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(1234.0, 0, sym);
    assert(str_eq(r, "$1,234"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_negative()
{
    rt_string sym = make_str("$");
    rt_string r = rt_fmt_currency(-99.99, 2, sym);
    assert(str_eq(r, "-$99.99"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_euro()
{
    rt_string sym = make_str("EUR ");
    rt_string r = rt_fmt_currency(42.50, 2, sym);
    assert(str_eq(r, "EUR 42.50"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

// ---------------------------------------------------------------------------
// ToWords tests
// ---------------------------------------------------------------------------

static void test_words_zero()
{
    rt_string r = rt_fmt_to_words(0);
    assert(str_eq(r, "zero"));
    rt_string_unref(r);
}

static void test_words_small()
{
    rt_string r = rt_fmt_to_words(5);
    assert(str_eq(r, "five"));
    rt_string_unref(r);
}

static void test_words_teens()
{
    rt_string r = rt_fmt_to_words(13);
    assert(str_eq(r, "thirteen"));
    rt_string_unref(r);
}

static void test_words_tens()
{
    rt_string r = rt_fmt_to_words(42);
    assert(str_eq(r, "forty-two"));
    rt_string_unref(r);
}

static void test_words_hundred()
{
    rt_string r = rt_fmt_to_words(100);
    assert(str_eq(r, "one hundred"));
    rt_string_unref(r);
}

static void test_words_complex()
{
    rt_string r = rt_fmt_to_words(1234);
    assert(str_eq(r, "one thousand two hundred thirty-four"));
    rt_string_unref(r);
}

static void test_words_million()
{
    rt_string r = rt_fmt_to_words(1000000);
    assert(str_eq(r, "one million"));
    rt_string_unref(r);
}

static void test_words_negative()
{
    rt_string r = rt_fmt_to_words(-7);
    assert(str_eq(r, "negative seven"));
    rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// Ordinal tests
// ---------------------------------------------------------------------------

static void test_ordinal_1()
{
    rt_string r = rt_fmt_ordinal(1);
    assert(str_eq(r, "1st"));
    rt_string_unref(r);
}

static void test_ordinal_2()
{
    rt_string r = rt_fmt_ordinal(2);
    assert(str_eq(r, "2nd"));
    rt_string_unref(r);
}

static void test_ordinal_3()
{
    rt_string r = rt_fmt_ordinal(3);
    assert(str_eq(r, "3rd"));
    rt_string_unref(r);
}

static void test_ordinal_4()
{
    rt_string r = rt_fmt_ordinal(4);
    assert(str_eq(r, "4th"));
    rt_string_unref(r);
}

static void test_ordinal_11()
{
    rt_string r = rt_fmt_ordinal(11);
    assert(str_eq(r, "11th"));
    rt_string_unref(r);
}

static void test_ordinal_12()
{
    rt_string r = rt_fmt_ordinal(12);
    assert(str_eq(r, "12th"));
    rt_string_unref(r);
}

static void test_ordinal_13()
{
    rt_string r = rt_fmt_ordinal(13);
    assert(str_eq(r, "13th"));
    rt_string_unref(r);
}

static void test_ordinal_21()
{
    rt_string r = rt_fmt_ordinal(21);
    assert(str_eq(r, "21st"));
    rt_string_unref(r);
}

static void test_ordinal_101()
{
    rt_string r = rt_fmt_ordinal(101);
    assert(str_eq(r, "101st"));
    rt_string_unref(r);
}

static void test_ordinal_111()
{
    rt_string r = rt_fmt_ordinal(111);
    assert(str_eq(r, "111th"));
    rt_string_unref(r);
}

int main()
{
    // IntGrouped
    test_grouped_basic();
    test_grouped_small();
    test_grouped_zero();
    test_grouped_negative();
    test_grouped_dot_separator();
    test_grouped_exact_thousand();

    // Currency
    test_currency_basic();
    test_currency_zero_decimals();
    test_currency_negative();
    test_currency_euro();

    // ToWords
    test_words_zero();
    test_words_small();
    test_words_teens();
    test_words_tens();
    test_words_hundred();
    test_words_complex();
    test_words_million();
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

    return 0;
}
