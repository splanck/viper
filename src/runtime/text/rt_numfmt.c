//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_numfmt.c
// Purpose: Implements numeric formatting utilities for the Viper.Text.NumFmt
//          class. Provides FormatInt (integer with thousands separators),
//          FormatFloat (float with configurable decimal places), FormatPercent,
//          FormatCurrency, FormatScientific, and FormatOrdinal.
//
// Key invariants:
//   - Thousands separator defaults to ',' and decimal separator to '.'.
//   - FormatPercent multiplies by 100 and appends the '%' symbol.
//   - FormatCurrency prepends the currency symbol and applies thousands grouping.
//   - FormatScientific uses standard IEEE notation (e.g. 1.23e+04).
//   - FormatOrdinal appends "st", "nd", "rd", or "th" per English rules.
//   - All functions handle negative values and zero correctly.
//
// Ownership/Lifetime:
//   - All returned rt_string values are fresh allocations owned by the caller.
//   - No state is retained between calls.
//
// Links: src/runtime/text/rt_numfmt.h (public API),
//        src/runtime/rt_string_builder.h (used to accumulate formatted output)
//
//===----------------------------------------------------------------------===//

#include "rt_numfmt.h"
#include "rt_internal.h"
#include "rt_string_builder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// rt_numfmt_decimals
// ---------------------------------------------------------------------------

rt_string rt_numfmt_decimals(double n, int64_t decimals)
{
    if (decimals < 0)
        decimals = 0;
    if (decimals > 20)
        decimals = 20;

    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%.*f", (int)decimals, n);
    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}

// ---------------------------------------------------------------------------
// rt_numfmt_thousands
// ---------------------------------------------------------------------------

rt_string rt_numfmt_thousands(int64_t n, rt_string sep)
{
    const char *sep_cstr = sep ? rt_string_cstr(sep) : ",";
    if (!sep_cstr || *sep_cstr == '\0')
        sep_cstr = ",";

    int negative = n < 0;
    // Handle INT64_MIN
    uint64_t abs_n;
    if (n == INT64_MIN)
        abs_n = (uint64_t)INT64_MAX + 1;
    else
        abs_n = (uint64_t)(negative ? -n : n);

    // Format the absolute number first
    char digits[32];
    int dlen = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)abs_n);
    if (dlen < 0)
        dlen = 0;

    size_t sep_len = strlen(sep_cstr);
    int num_seps = (dlen - 1) / 3;

    rt_string_builder sb;
    rt_sb_init(&sb);

    if (negative)
        rt_sb_append_cstr(&sb, "-");

    int first_group = dlen % 3;
    if (first_group == 0)
        first_group = 3;

    rt_sb_append_bytes(&sb, digits, (size_t)first_group);
    int pos = first_group;

    while (pos < dlen)
    {
        rt_sb_append_bytes(&sb, sep_cstr, sep_len);
        rt_sb_append_bytes(&sb, digits + pos, 3);
        pos += 3;
    }

    (void)num_seps;
    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// rt_numfmt_currency
// ---------------------------------------------------------------------------

rt_string rt_numfmt_currency(double n, rt_string symbol)
{
    const char *sym = symbol ? rt_string_cstr(symbol) : "$";
    if (!sym)
        sym = "$";

    int negative = n < 0;
    double abs_n = fabs(n);

    // Round to 2 decimal places
    char amount[64];
    int alen = snprintf(amount, sizeof(amount), "%.2f", abs_n);
    if (alen < 0)
        alen = 0;
    if (alen >= (int)sizeof(amount))
        alen = (int)sizeof(amount) - 1;

    // Find the decimal point
    char *dot = strchr(amount, '.');
    int int_len = dot ? (int)(dot - amount) : alen;

    rt_string_builder sb;
    rt_sb_init(&sb);

    if (negative)
        rt_sb_append_cstr(&sb, "-");
    rt_sb_append_cstr(&sb, sym);

    // Add thousands separators to integer part
    int first_group = int_len % 3;
    if (first_group == 0)
        first_group = 3;

    rt_sb_append_bytes(&sb, amount, (size_t)first_group);
    int pos = first_group;

    while (pos < int_len)
    {
        rt_sb_append_cstr(&sb, ",");
        rt_sb_append_bytes(&sb, amount + pos, 3);
        pos += 3;
    }

    // Add decimal part
    if (dot)
    {
        rt_sb_append_cstr(&sb, dot);
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// rt_numfmt_percent
// ---------------------------------------------------------------------------

rt_string rt_numfmt_percent(double n)
{
    double pct = n * 100.0;
    char buf[64];

    // Use at most 1 decimal place, but omit trailing .0
    double rounded = round(pct * 10.0) / 10.0;
    double frac = rounded - (int64_t)rounded;

    int len;
    if (frac == 0.0 || frac == -0.0)
        len = snprintf(buf, sizeof(buf), "%.0f%%", rounded);
    else
        len = snprintf(buf, sizeof(buf), "%.1f%%", rounded);

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}

// ---------------------------------------------------------------------------
// rt_numfmt_ordinal
// ---------------------------------------------------------------------------

rt_string rt_numfmt_ordinal(int64_t n)
{
    const char *suffix;
    int64_t abs_n = (n == INT64_MIN) ? INT64_MAX : (n < 0 ? -n : n);
    int64_t mod100 = abs_n % 100;
    int64_t mod10 = abs_n % 10;

    if (mod100 >= 11 && mod100 <= 13)
        suffix = "th";
    else if (mod10 == 1)
        suffix = "st";
    else if (mod10 == 2)
        suffix = "nd";
    else if (mod10 == 3)
        suffix = "rd";
    else
        suffix = "th";

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld%s", (long long)n, suffix);
    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}

// ---------------------------------------------------------------------------
// rt_numfmt_to_words
// ---------------------------------------------------------------------------

static const char *const ones[] = {"",        "one",     "two",       "three",    "four",
                                   "five",    "six",     "seven",     "eight",    "nine",
                                   "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                                   "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};

static const char *const tens[] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

static void append_chunk(rt_string_builder *sb, int64_t n, int *has_prev)
{
    if (n == 0)
        return;

    if (*has_prev)
        rt_sb_append_cstr(sb, " ");

    if (n >= 100)
    {
        rt_sb_append_cstr(sb, ones[n / 100]);
        rt_sb_append_cstr(sb, " hundred");
        n %= 100;
        if (n > 0)
            rt_sb_append_cstr(sb, " ");
    }

    if (n >= 20)
    {
        rt_sb_append_cstr(sb, tens[n / 10]);
        n %= 10;
        if (n > 0)
        {
            rt_sb_append_cstr(sb, "-");
            rt_sb_append_cstr(sb, ones[n]);
        }
    }
    else if (n > 0)
    {
        rt_sb_append_cstr(sb, ones[n]);
    }

    *has_prev = 1;
}

rt_string rt_numfmt_to_words(int64_t n)
{
    if (n == 0)
        return rt_string_from_bytes("zero", 4);

    rt_string_builder sb;
    rt_sb_init(&sb);

    int negative = n < 0;
    uint64_t abs_n;
    if (n == INT64_MIN)
        abs_n = (uint64_t)INT64_MAX + 1;
    else
        abs_n = (uint64_t)(negative ? -n : n);

    if (negative)
        rt_sb_append_cstr(&sb, "negative ");

    // Break into groups of three
    static const char *const scale[] = {
        "", "thousand", "million", "billion", "trillion", "quadrillion", "quintillion"};
    int groups[7] = {0};
    int num_groups = 0;

    uint64_t temp = abs_n;
    while (temp > 0 && num_groups < 7)
    {
        groups[num_groups++] = (int)(temp % 1000);
        temp /= 1000;
    }

    int has_prev = 0;
    for (int i = num_groups - 1; i >= 0; i--)
    {
        if (groups[i] == 0)
            continue;
        append_chunk(&sb, groups[i], &has_prev);
        if (i > 0 && scale[i][0] != '\0')
        {
            rt_sb_append_cstr(&sb, " ");
            rt_sb_append_cstr(&sb, scale[i]);
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// rt_numfmt_bytes
// ---------------------------------------------------------------------------

rt_string rt_numfmt_bytes(int64_t bytes)
{
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    double val = (double)(bytes < 0 ? -bytes : bytes);
    int unit_idx = 0;

    while (val >= 1024.0 && unit_idx < 6)
    {
        val /= 1024.0;
        unit_idx++;
    }

    char buf[64];
    int len;

    if (unit_idx == 0)
    {
        len = snprintf(buf,
                       sizeof(buf),
                       "%s%lld %s",
                       bytes < 0 ? "-" : "",
                       (long long)(bytes < 0 ? -bytes : bytes),
                       units[0]);
    }
    else
    {
        if (bytes < 0)
            val = -val;
        // Use 1 decimal place for values >= 10, 2 for smaller
        if (fabs(val) >= 10.0)
            len = snprintf(buf, sizeof(buf), "%.1f %s", val, units[unit_idx]);
        else
            len = snprintf(buf, sizeof(buf), "%.2f %s", val, units[unit_idx]);
    }

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}

// ---------------------------------------------------------------------------
// rt_numfmt_pad
// ---------------------------------------------------------------------------

rt_string rt_numfmt_pad(int64_t n, int64_t width)
{
    if (width < 1)
        width = 1;
    if (width > 64)
        width = 64;

    char buf[128];
    int len;

    if (n >= 0)
        len = snprintf(buf, sizeof(buf), "%0*lld", (int)width, (long long)n);
    else
        len = snprintf(buf, sizeof(buf), "-%0*lld", (int)(width - 1), (long long)(-n));

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}
