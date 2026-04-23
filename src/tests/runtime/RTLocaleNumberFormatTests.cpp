//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTLocaleNumberFormatTests.cpp
// Purpose: Validate Viper.Localization.NumberFormat against the baked en-US
//          locale. Covers format and parse round-trips across Decimal /
//          Integer / Percent / Currency / Scientific / Ordinal plus options
//          (MinFractionDigits, MaxFractionDigits, UseGrouping, Strict,
//          RoundingMode). Parsing covers strict-mode rejection and lenient
//          tolerance.
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"
#include "rt_numformat.h"
#include "rt_option.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>

static jmp_buf g_trap_env;
static int g_expect_trap = 0;

extern "C" void vm_trap(const char *msg) {
    if (g_expect_trap)
        longjmp(g_trap_env, 1);
    fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    abort();
}

#define EXPECT_TRAP(expr)                                                         \
    do {                                                                          \
        g_expect_trap = 1;                                                         \
        if (setjmp(g_trap_env) == 0) {                                            \
            (void)(expr);                                                          \
            g_expect_trap = 0;                                                     \
            assert(!"expected runtime trap");                                     \
        } else {                                                                   \
            g_expect_trap = 0;                                                     \
        }                                                                          \
    } while (0)

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static bool eq(rt_string s, const char *expected) {
    const char *cs = rt_string_cstr(s);
    bool ok = cs && strcmp(cs, expected) == 0;
    rt_string_unref(s);
    return ok;
}

static void *en_fmt() {
    rt_string in = S("en-US");
    void *loc = rt_locale_parse(in);
    rt_string_unref(in);
    return rt_numformat_for_locale(loc);
}

//=============================================================================
// Format — Decimal / Integer / Percent
//=============================================================================

static void test_format_decimal() {
    printf("Testing NumberFormat.Decimal (en-US):\n");
    void *fmt = en_fmt();

    test_result("Decimal(1234.5) = \"1,234.5\"",
                eq(rt_numformat_decimal(fmt, 1234.5), "1,234.5"));
    test_result("Decimal(1234567.89) = \"1,234,567.89\"",
                eq(rt_numformat_decimal(fmt, 1234567.89), "1,234,567.89"));
    test_result("Decimal(0) = \"0\"",
                eq(rt_numformat_decimal(fmt, 0.0), "0"));
    test_result("Decimal(-42.5) = \"-42.5\"",
                eq(rt_numformat_decimal(fmt, -42.5), "-42.5"));
    test_result("Decimal(0.125) = \"0.125\"",
                eq(rt_numformat_decimal(fmt, 0.125), "0.125"));
}

static void test_format_decimal_n() {
    printf("Testing NumberFormat.DecimalN (en-US):\n");
    void *fmt = en_fmt();

    test_result("DecimalN(1234.5678, 2) = \"1,234.57\"",
                eq(rt_numformat_decimal_n(fmt, 1234.5678, 2), "1,234.57"));
    test_result("DecimalN(1.0, 3) = \"1.000\"",
                eq(rt_numformat_decimal_n(fmt, 1.0, 3), "1.000"));
    test_result("DecimalN(1234.0, 0) = \"1,234\"",
                eq(rt_numformat_decimal_n(fmt, 1234.0, 0), "1,234"));
}

static void test_format_integer() {
    printf("Testing NumberFormat.Integer (en-US):\n");
    void *fmt = en_fmt();

    test_result("Integer(1234) = \"1,234\"",
                eq(rt_numformat_integer(fmt, 1234), "1,234"));
    test_result("Integer(-1234567) = \"-1,234,567\"",
                eq(rt_numformat_integer(fmt, -1234567), "-1,234,567"));
    test_result("Integer(0) = \"0\"",
                eq(rt_numformat_integer(fmt, 0), "0"));
}

static void test_format_percent() {
    printf("Testing NumberFormat.Percent (en-US):\n");
    void *fmt = en_fmt();

    test_result("Percent(0.1234) = \"12.34%\"",
                eq(rt_numformat_percent(fmt, 0.1234), "12.34%"));
    test_result("Percent(0.5) = \"50%\"",
                eq(rt_numformat_percent(fmt, 0.5), "50%"));
    test_result("Percent(-0.25) = \"-25%\"",
                eq(rt_numformat_percent(fmt, -0.25), "-25%"));
}

//=============================================================================
// Format — Currency / Scientific / Ordinal
//=============================================================================

static void test_format_currency() {
    printf("Testing NumberFormat.Currency (en-US):\n");
    void *fmt = en_fmt();

    test_result("Currency(1234.56) = \"$1,234.56\"",
                eq(rt_numformat_currency(fmt, 1234.56), "$1,234.56"));
    test_result("Currency(5.0) = \"$5.00\"",
                eq(rt_numformat_currency(fmt, 5.0), "$5.00"));
    test_result("Currency(-12.5) = \"-$12.50\"",
                eq(rt_numformat_currency(fmt, -12.5), "-$12.50"));

    rt_string code = S("EUR");
    test_result("CurrencyOf(100, EUR) starts with EUR",
                eq(rt_numformat_currency_of(fmt, 100.0, code), "EUR100.00"));
    rt_string_unref(code);
}

static void test_format_scientific() {
    printf("Testing NumberFormat.Scientific (en-US):\n");
    void *fmt = en_fmt();

    // en-US uses '.' as decimal separator and 'E' as exponent char.
    test_result("Scientific(12345, 3) = \"1.234E+04\"",
                eq(rt_numformat_scientific(fmt, 12345.0, 3), "1.234E+04"));
    test_result("Scientific(0.00025, 2) = \"2.50E-04\"",
                eq(rt_numformat_scientific(fmt, 0.00025, 2), "2.50E-04"));
}

static void test_format_ordinal() {
    printf("Testing NumberFormat.Ordinal (en-US):\n");
    void *fmt = en_fmt();

    test_result("Ordinal(1) = \"1st\"", eq(rt_numformat_ordinal(fmt, 1), "1st"));
    test_result("Ordinal(2) = \"2nd\"", eq(rt_numformat_ordinal(fmt, 2), "2nd"));
    test_result("Ordinal(3) = \"3rd\"", eq(rt_numformat_ordinal(fmt, 3), "3rd"));
    test_result("Ordinal(11) = \"11th\"", eq(rt_numformat_ordinal(fmt, 11), "11th"));
    test_result("Ordinal(22) = \"22nd\"", eq(rt_numformat_ordinal(fmt, 22), "22nd"));
}

//=============================================================================
// Options: grouping, fraction digits, rounding mode
//=============================================================================

static void test_grouping_toggle() {
    printf("Testing NumberFormat grouping toggle:\n");
    void *fmt = en_fmt();
    rt_numformat_set_grouping(fmt, 0);
    test_result("UseGrouping=false -> no separator",
                eq(rt_numformat_integer(fmt, 1234567), "1234567"));
    rt_numformat_set_grouping(fmt, 1);
    test_result("UseGrouping=true -> separator",
                eq(rt_numformat_integer(fmt, 1234567), "1,234,567"));
}

static void test_fraction_digits_clamp() {
    printf("Testing Min/Max fraction digits:\n");
    void *fmt = en_fmt();

    rt_numformat_set_min_frac(fmt, 2);
    rt_numformat_set_max_frac(fmt, 4);
    test_result("Decimal(1.5) with min=2 -> \"1.50\"",
                eq(rt_numformat_decimal(fmt, 1.5), "1.50"));
    test_result("Decimal(1.23456) with max=4 -> \"1.2346\"",
                eq(rt_numformat_decimal(fmt, 1.23456), "1.2346"));
}

static void test_rounding_modes() {
    printf("Testing rounding modes:\n");
    void *fmt = en_fmt();
    rt_numformat_set_min_frac(fmt, 0);
    rt_numformat_set_max_frac(fmt, 0);

    rt_string up_mode = S("halfUp");
    rt_numformat_set_rounding(fmt, up_mode);
    rt_string_unref(up_mode);
    test_result("halfUp 2.5 -> \"3\"",
                eq(rt_numformat_decimal(fmt, 2.5), "3"));

    rt_string floor_mode = S("floor");
    rt_numformat_set_rounding(fmt, floor_mode);
    rt_string_unref(floor_mode);
    test_result("floor 2.9 -> \"2\"",
                eq(rt_numformat_decimal(fmt, 2.9), "2"));

    rt_string ceil_mode = S("ceiling");
    rt_numformat_set_rounding(fmt, ceil_mode);
    rt_string_unref(ceil_mode);
    test_result("ceiling 2.1 -> \"3\"",
                eq(rt_numformat_decimal(fmt, 2.1), "3"));
}

//=============================================================================
// Parse — Decimal / Integer / Currency
//=============================================================================

static void test_parse_decimal() {
    printf("Testing NumberFormat.ParseDecimal (en-US):\n");
    void *fmt = en_fmt();

    rt_string s1 = S("1234.5");
    double v1 = rt_numformat_parse_decimal(fmt, s1);
    rt_string_unref(s1);
    test_result("ParseDecimal(\"1234.5\") == 1234.5", std::fabs(v1 - 1234.5) < 1e-9);

    rt_string s2 = S("1,234.5");
    double v2 = rt_numformat_parse_decimal(fmt, s2);
    rt_string_unref(s2);
    test_result("ParseDecimal(\"1,234.5\") == 1234.5", std::fabs(v2 - 1234.5) < 1e-9);

    rt_string s3 = S("-42.75");
    double v3 = rt_numformat_parse_decimal(fmt, s3);
    rt_string_unref(s3);
    test_result("ParseDecimal(\"-42.75\") == -42.75", std::fabs(v3 - (-42.75)) < 1e-9);

    rt_string s4 = S("  1,000,000  ");
    double v4 = rt_numformat_parse_decimal(fmt, s4);
    rt_string_unref(s4);
    test_result("ParseDecimal whitespace-tolerant", std::fabs(v4 - 1000000.0) < 1e-9);
}

static void test_parse_decimal_roundtrip() {
    printf("Testing Decimal format/parse round-trip:\n");
    void *fmt = en_fmt();
    double values[] = { 0.0, 1.0, -1.0, 1234.5, -99999.99, 0.125 };
    for (double v : values) {
        rt_string s = rt_numformat_decimal(fmt, v);
        double parsed = rt_numformat_parse_decimal(fmt, s);
        rt_string_unref(s);
        char msg[128];
        snprintf(msg, sizeof(msg), "round-trip %g", v);
        test_result(msg, std::fabs(parsed - v) < 1e-9);
    }
}

static void test_try_parse_decimal() {
    printf("Testing NumberFormat.TryParseDecimal:\n");
    void *fmt = en_fmt();

    rt_string good = S("123.45");
    void *r1 = rt_numformat_try_parse_decimal(fmt, good);
    rt_string_unref(good);
    test_result("TryParseDecimal valid -> Some", rt_option_is_some(r1) == 1);

    rt_string bad = S("nope");
    void *r2 = rt_numformat_try_parse_decimal(fmt, bad);
    rt_string_unref(bad);
    test_result("TryParseDecimal invalid -> None", rt_option_is_none(r2) == 1);
}

static void test_parse_decimal_traps() {
    printf("Testing ParseDecimal trap paths:\n");
    void *fmt = en_fmt();

    rt_string bad = S("not a number");
    EXPECT_TRAP(rt_numformat_parse_decimal(fmt, bad));
    rt_string_unref(bad);
    test_result("ParseDecimal(bogus) traps", true);

    rt_string empty = S("");
    EXPECT_TRAP(rt_numformat_parse_decimal(fmt, empty));
    rt_string_unref(empty);
    test_result("ParseDecimal(empty) traps", true);
}

static void test_parse_integer() {
    printf("Testing NumberFormat.ParseInteger:\n");
    void *fmt = en_fmt();

    rt_string s1 = S("1,000");
    int64_t v1 = rt_numformat_parse_integer(fmt, s1);
    rt_string_unref(s1);
    test_result("ParseInteger(\"1,000\") == 1000", v1 == 1000);

    rt_string s2 = S("1,234.5"); // fractional -> reject
    EXPECT_TRAP(rt_numformat_parse_integer(fmt, s2));
    rt_string_unref(s2);
    test_result("ParseInteger rejects fractional", true);
}

static void test_parse_currency() {
    printf("Testing NumberFormat.ParseCurrency:\n");
    void *fmt = en_fmt();

    rt_string s1 = S("$1,234.56");
    double v1 = rt_numformat_parse_currency(fmt, s1);
    rt_string_unref(s1);
    test_result("ParseCurrency(\"$1,234.56\") == 1234.56",
                std::fabs(v1 - 1234.56) < 1e-9);

    rt_string s2 = S("1,000.00");
    double v2 = rt_numformat_parse_currency(fmt, s2);
    rt_string_unref(s2);
    test_result("ParseCurrency(symbol-less) == 1000",
                std::fabs(v2 - 1000.0) < 1e-9);
}

//=============================================================================
// Strict vs lenient parse
//=============================================================================

static void test_strict_mode_rejects_ambiguous() {
    printf("Testing Strict parse mode:\n");
    void *fmt = en_fmt();
    rt_numformat_set_strict(fmt, 1);

    // "1,00" under en-US has only 2 digits after the group separator,
    // violating the group_size=3 expectation. Strict mode rejects.
    rt_string bad = S("1,00");
    void *r = rt_numformat_try_parse_decimal(fmt, bad);
    rt_string_unref(bad);
    test_result("Strict TryParseDecimal(\"1,00\") = None",
                rt_option_is_none(r) == 1);

    // Lenient mode accepts.
    rt_numformat_set_strict(fmt, 0);
    rt_string bad2 = S("1,00");
    void *r2 = rt_numformat_try_parse_decimal(fmt, bad2);
    rt_string_unref(bad2);
    test_result("Lenient TryParseDecimal(\"1,00\") = Some",
                rt_option_is_some(r2) == 1);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== RT LocaleNumberFormat Tests ===\n\n");
    test_format_decimal();
    test_format_decimal_n();
    test_format_integer();
    test_format_percent();
    test_format_currency();
    test_format_scientific();
    test_format_ordinal();
    test_grouping_toggle();
    test_fraction_digits_clamp();
    test_rounding_modes();
    test_parse_decimal();
    test_parse_decimal_roundtrip();
    test_try_parse_decimal();
    test_parse_decimal_traps();
    test_parse_integer();
    test_parse_currency();
    test_strict_mode_rejects_ambiguous();
    printf("\nAll LocaleNumberFormat tests passed!\n");
    return 0;
}
