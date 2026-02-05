//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_numfmt.h"
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
// Decimals
// ---------------------------------------------------------------------------

static void test_decimals_basic()
{
    rt_string r = rt_numfmt_decimals(3.14159, 2);
    assert(str_eq(r, "3.14"));
    rt_string_unref(r);
}

static void test_decimals_zero()
{
    rt_string r = rt_numfmt_decimals(3.14, 0);
    assert(str_eq(r, "3"));
    rt_string_unref(r);
}

static void test_decimals_padding()
{
    rt_string r = rt_numfmt_decimals(5.0, 3);
    assert(str_eq(r, "5.000"));
    rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// Thousands
// ---------------------------------------------------------------------------

static void test_thousands_basic()
{
    rt_string sep = make_str(",");
    rt_string r = rt_numfmt_thousands(1234567, sep);
    assert(str_eq(r, "1,234,567"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_thousands_small()
{
    rt_string sep = make_str(",");
    rt_string r = rt_numfmt_thousands(999, sep);
    assert(str_eq(r, "999"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_thousands_negative()
{
    rt_string sep = make_str(",");
    rt_string r = rt_numfmt_thousands(-1000000, sep);
    assert(str_eq(r, "-1,000,000"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

static void test_thousands_custom_sep()
{
    rt_string sep = make_str(".");
    rt_string r = rt_numfmt_thousands(1000000, sep);
    assert(str_eq(r, "1.000.000"));
    rt_string_unref(r);
    rt_string_unref(sep);
}

// ---------------------------------------------------------------------------
// Currency
// ---------------------------------------------------------------------------

static void test_currency_basic()
{
    rt_string sym = make_str("$");
    rt_string r = rt_numfmt_currency(1234.56, sym);
    assert(str_eq(r, "$1,234.56"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_negative()
{
    rt_string sym = make_str("$");
    rt_string r = rt_numfmt_currency(-42.50, sym);
    assert(str_eq(r, "-$42.50"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

static void test_currency_euro()
{
    rt_string sym = make_str("\xe2\x82\xac"); // Euro sign UTF-8
    rt_string r = rt_numfmt_currency(1000.00, sym);
    assert(str_eq(r, "\xe2\x82\xac""1,000.00"));
    rt_string_unref(r);
    rt_string_unref(sym);
}

// ---------------------------------------------------------------------------
// Percent
// ---------------------------------------------------------------------------

static void test_percent_basic()
{
    rt_string r = rt_numfmt_percent(0.756);
    assert(str_eq(r, "75.6%"));
    rt_string_unref(r);
}

static void test_percent_whole()
{
    rt_string r = rt_numfmt_percent(0.5);
    assert(str_eq(r, "50%"));
    rt_string_unref(r);
}

static void test_percent_zero()
{
    rt_string r = rt_numfmt_percent(0.0);
    assert(str_eq(r, "0%"));
    rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// Ordinal
// ---------------------------------------------------------------------------

static void test_ordinal()
{
    rt_string r;
    r = rt_numfmt_ordinal(1); assert(str_eq(r, "1st")); rt_string_unref(r);
    r = rt_numfmt_ordinal(2); assert(str_eq(r, "2nd")); rt_string_unref(r);
    r = rt_numfmt_ordinal(3); assert(str_eq(r, "3rd")); rt_string_unref(r);
    r = rt_numfmt_ordinal(4); assert(str_eq(r, "4th")); rt_string_unref(r);
    r = rt_numfmt_ordinal(11); assert(str_eq(r, "11th")); rt_string_unref(r);
    r = rt_numfmt_ordinal(12); assert(str_eq(r, "12th")); rt_string_unref(r);
    r = rt_numfmt_ordinal(13); assert(str_eq(r, "13th")); rt_string_unref(r);
    r = rt_numfmt_ordinal(21); assert(str_eq(r, "21st")); rt_string_unref(r);
    r = rt_numfmt_ordinal(22); assert(str_eq(r, "22nd")); rt_string_unref(r);
    r = rt_numfmt_ordinal(100); assert(str_eq(r, "100th")); rt_string_unref(r);
    r = rt_numfmt_ordinal(101); assert(str_eq(r, "101st")); rt_string_unref(r);
    r = rt_numfmt_ordinal(111); assert(str_eq(r, "111th")); rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// To words
// ---------------------------------------------------------------------------

static void test_to_words()
{
    rt_string r;
    r = rt_numfmt_to_words(0); assert(str_eq(r, "zero")); rt_string_unref(r);
    r = rt_numfmt_to_words(1); assert(str_eq(r, "one")); rt_string_unref(r);
    r = rt_numfmt_to_words(42); assert(str_eq(r, "forty-two")); rt_string_unref(r);
    r = rt_numfmt_to_words(100); assert(str_eq(r, "one hundred")); rt_string_unref(r);
    r = rt_numfmt_to_words(1000); assert(str_eq(r, "one thousand")); rt_string_unref(r);
    r = rt_numfmt_to_words(1001); assert(str_eq(r, "one thousand one")); rt_string_unref(r);
    r = rt_numfmt_to_words(1000000); assert(str_eq(r, "one million")); rt_string_unref(r);
    r = rt_numfmt_to_words(-5); assert(str_eq(r, "negative five")); rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// Bytes
// ---------------------------------------------------------------------------

static void test_bytes()
{
    rt_string r;
    r = rt_numfmt_bytes(0); assert(str_eq(r, "0 B")); rt_string_unref(r);
    r = rt_numfmt_bytes(500); assert(str_eq(r, "500 B")); rt_string_unref(r);
    r = rt_numfmt_bytes(1024); assert(str_eq(r, "1.00 KB")); rt_string_unref(r);
    r = rt_numfmt_bytes(1536); assert(str_eq(r, "1.50 KB")); rt_string_unref(r);
    r = rt_numfmt_bytes(1048576); assert(str_eq(r, "1.00 MB")); rt_string_unref(r);
    r = rt_numfmt_bytes(1073741824); assert(str_eq(r, "1.00 GB")); rt_string_unref(r);
}

// ---------------------------------------------------------------------------
// Pad
// ---------------------------------------------------------------------------

static void test_pad()
{
    rt_string r;
    r = rt_numfmt_pad(42, 5); assert(str_eq(r, "00042")); rt_string_unref(r);
    r = rt_numfmt_pad(42, 2); assert(str_eq(r, "42")); rt_string_unref(r);
    r = rt_numfmt_pad(42, 1); assert(str_eq(r, "42")); rt_string_unref(r);
    r = rt_numfmt_pad(0, 3); assert(str_eq(r, "000")); rt_string_unref(r);
    r = rt_numfmt_pad(-7, 4); assert(str_eq(r, "-007")); rt_string_unref(r);
}

int main()
{
    // Decimals
    test_decimals_basic();
    test_decimals_zero();
    test_decimals_padding();

    // Thousands
    test_thousands_basic();
    test_thousands_small();
    test_thousands_negative();
    test_thousands_custom_sep();

    // Currency
    test_currency_basic();
    test_currency_negative();
    test_currency_euro();

    // Percent
    test_percent_basic();
    test_percent_whole();
    test_percent_zero();

    // Ordinal
    test_ordinal();

    // To words
    test_to_words();

    // Bytes
    test_bytes();

    // Pad
    test_pad();

    return 0;
}
