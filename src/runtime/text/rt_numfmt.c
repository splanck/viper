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
#include "rt_numfmt_internal.h"
#include "rt_string_builder.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void numfmt_check_sb(rt_sb_status_t status, const char *op) {
    if (status != RT_SB_OK)
        rt_trap(op);
}

static uint64_t abs_i64_magnitude(int64_t n) {
    if (n >= 0)
        return (uint64_t)n;
    return (uint64_t)(-(n + 1)) + 1;
}

static char *numfmt_alloc_buffer(int needed) {
    if (needed < 0) {
        rt_trap("NumberFormat: formatting failed");
    }

    char *buf = (char *)malloc((size_t)needed + 1);
    if (!buf)
        rt_trap("NumberFormat: memory allocation failed");
    return buf;
}

static rt_string numfmt_finish_buffer(char *buf, int written, int needed) {
    if (written < 0 || written > needed) {
        free(buf);
        rt_trap("NumberFormat: formatting failed");
    }

    rt_string result = rt_string_from_bytes(buf, (size_t)written);
    free(buf);
    return result;
}

static rt_string numfmt_fixed(double value, int decimals) {
    int needed = snprintf(NULL, 0, "%.*f", decimals, value);
    char *buf = numfmt_alloc_buffer(needed);
    int written = snprintf(buf, (size_t)needed + 1, "%.*f", decimals, value);
    return numfmt_finish_buffer(buf, written, needed);
}

static rt_string numfmt_percent_fixed(double value, int decimals) {
    int needed = decimals == 0 ? snprintf(NULL, 0, "%.0f%%", value)
                               : snprintf(NULL, 0, "%.1f%%", value);
    char *buf = numfmt_alloc_buffer(needed);
    int written = decimals == 0 ? snprintf(buf, (size_t)needed + 1, "%.0f%%", value)
                                : snprintf(buf, (size_t)needed + 1, "%.1f%%", value);
    return numfmt_finish_buffer(buf, written, needed);
}

// ---------------------------------------------------------------------------
// rt_numfmt_decimals
// ---------------------------------------------------------------------------

/// @brief Format `n` with exactly `decimals` digits after the decimal point.
/// @details Clamps `decimals` into [0, 20] to keep the printf format
///          sane, then delegates to `snprintf("%.*f")` which handles
///          the rounding (banker's-round on most C runtimes — close
///          enough to half-up that the output matches user
///          expectations for currency-style display).
rt_string rt_numfmt_decimals(double n, int64_t decimals) {
    if (decimals < 0)
        decimals = 0;
    if (decimals > 20)
        decimals = 20;

    return numfmt_fixed(n, (int)decimals);
}

// ---------------------------------------------------------------------------
// rt_numfmt_thousands
// ---------------------------------------------------------------------------

/// @brief Format an integer with `sep` inserted every three digits from the right.
/// @details `sep` defaults to `","` if NULL or empty. Steps:
///          1. Stringify the absolute value via `snprintf` (handles
///             `INT64_MIN` via the `INT64_MAX + 1` cast — direct
///             negation would overflow).
///          2. Emit a leading minus if needed, then the leading
///             group (1, 2, or 3 digits depending on length mod 3).
///          3. Loop: append separator + 3-digit group until
///             exhausted.
///          Examples: `1234567 → "1,234,567"`, `-12345 → "-12,345"`.
rt_string rt_numfmt_thousands(int64_t n, rt_string sep) {
    const char *sep_bytes = ",";
    size_t sep_len = 1;
    if (sep) {
        const char *candidate = rt_string_cstr(sep);
        int64_t candidate_len = rt_str_len(sep);
        if (candidate && candidate_len > 0) {
            sep_bytes = candidate;
            sep_len = (size_t)candidate_len;
        }
    }

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

    rt_string_builder sb;
    rt_sb_init(&sb);

    if (negative)
        numfmt_check_sb(rt_sb_append_cstr(&sb, "-"), "NumberFormat.Thousands: formatting failed");

    rt_numfmt_group_digits(&sb, digits, dlen, sep_bytes, sep_len, /*group_size=*/3);

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// Shared helper: group digits with locale-specific separator (see
// rt_numfmt_internal.h for the full contract).
// ---------------------------------------------------------------------------
void rt_numfmt_group_digits(rt_string_builder *sb,
                            const char *digits, int dlen,
                            const char *sep, size_t sep_len,
                            int group_size) {
    if (dlen <= 0)
        return;

    // Degenerate cases: no separator, zero-length separator, or group size
    // that would swallow the entire input (<=0, or >=dlen). Emit the digits
    // verbatim and skip the grouping machinery.
    if (!sep || sep_len == 0 || group_size <= 0 || group_size >= dlen) {
        numfmt_check_sb(
            rt_sb_append_bytes(sb, digits, (size_t)dlen),
            "NumberFormat.Thousands: formatting failed");
        return;
    }

    int first_group = dlen % group_size;
    if (first_group == 0)
        first_group = group_size;

    numfmt_check_sb(
        rt_sb_append_bytes(sb, digits, (size_t)first_group),
        "NumberFormat.Thousands: formatting failed");
    int pos = first_group;

    while (pos < dlen) {
        numfmt_check_sb(
            rt_sb_append_bytes(sb, sep, sep_len),
            "NumberFormat.Thousands: formatting failed");
        numfmt_check_sb(
            rt_sb_append_bytes(sb, digits + pos, (size_t)group_size),
            "NumberFormat.Thousands: formatting failed");
        pos += group_size;
    }
}

// ---------------------------------------------------------------------------
// rt_numfmt_currency
// ---------------------------------------------------------------------------

/// @brief Format `n` as a currency value: `[symbol]X,XXX.XX` (always 2 decimals).
/// @details Defaults symbol to `"$"` if NULL. Layout:
///          `[-][symbol][int with thousand separators].[2 decimals]`.
///          Always emits exactly 2 decimal places (so `5.0 → "$5.00"`).
///          Hard-codes `,` as the thousands separator — locale-aware
///          formatting is intentionally out of scope.
rt_string rt_numfmt_currency(double n, rt_string symbol) {
    const char *sym = "$";
    size_t sym_len = 1;
    if (symbol) {
        sym = rt_string_cstr(symbol);
        sym_len = (size_t)rt_str_len(symbol);
    }
    if (!sym) {
        sym = "$";
        sym_len = 1;
    }

    int negative = n < 0;
    double abs_n = fabs(n);

    // Round to 2 decimal places
    rt_string amount_str = numfmt_fixed(abs_n, 2);
    const char *amount = rt_string_cstr(amount_str);
    int alen = (int)rt_str_len(amount_str);

    // Find the decimal point
    char *dot = (char *)memchr(amount, '.', (size_t)alen);
    int int_len = dot ? (int)(dot - amount) : alen;

    rt_string_builder sb;
    rt_sb_init(&sb);

    if (negative)
        numfmt_check_sb(rt_sb_append_cstr(&sb, "-"), "NumberFormat.Currency: formatting failed");
    numfmt_check_sb(
        rt_sb_append_bytes(&sb, sym, sym_len), "NumberFormat.Currency: formatting failed");

    // Add thousands separators to integer part
    int first_group = int_len % 3;
    if (first_group == 0)
        first_group = 3;

    numfmt_check_sb(
        rt_sb_append_bytes(&sb, amount, (size_t)first_group),
        "NumberFormat.Currency: formatting failed");
    int pos = first_group;

    while (pos < int_len) {
        numfmt_check_sb(rt_sb_append_cstr(&sb, ","), "NumberFormat.Currency: formatting failed");
        numfmt_check_sb(
            rt_sb_append_bytes(&sb, amount + pos, 3),
            "NumberFormat.Currency: formatting failed");
        pos += 3;
    }

    // Add decimal part
    if (dot) {
        numfmt_check_sb(
            rt_sb_append_bytes(&sb, dot, (size_t)(alen - int_len)),
            "NumberFormat.Currency: formatting failed");
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    rt_string_unref(amount_str);
    return result;
}

// ---------------------------------------------------------------------------
// rt_numfmt_percent
// ---------------------------------------------------------------------------

/// @brief Format `n` as a percentage (multiplies by 100, appends `%`).
/// @details Uses 1 decimal of precision unless the rounded value
///          would have a `.0` fractional part, in which case the
///          decimal is omitted for cleaner output.
///          Examples: `0.5 → "50%"`, `0.123 → "12.3%"`,
///          `0.345 → "34.5%"`, `0.5 → "50%"` (not `"50.0%"`).
rt_string rt_numfmt_percent(double n) {
    double pct = n * 100.0;

    // Use at most 1 decimal place, but omit trailing .0
    double rounded = round(pct * 10.0) / 10.0;
    if (!isfinite(rounded))
        return numfmt_percent_fixed(rounded, 0);

    rt_string formatted = numfmt_percent_fixed(rounded, 1);
    const char *text = rt_string_cstr(formatted);
    int64_t len = rt_str_len(formatted);

    if (len >= 3 && text[len - 3] == '.' && text[len - 2] == '0' && text[len - 1] == '%') {
        rt_string_builder sb;
        rt_sb_init(&sb);
        numfmt_check_sb(
            rt_sb_append_bytes(&sb, text, (size_t)(len - 3)),
            "NumberFormat.Percent: formatting failed");
        numfmt_check_sb(rt_sb_append_cstr(&sb, "%"), "NumberFormat.Percent: formatting failed");
        rt_string trimmed = rt_string_from_bytes(sb.data, sb.len);
        rt_sb_free(&sb);
        rt_string_unref(formatted);
        return trimmed;
    }

    return formatted;
}

// ---------------------------------------------------------------------------
// rt_numfmt_ordinal
// ---------------------------------------------------------------------------

/// @brief Append the English ordinal suffix to `n` (1 → "1st", 22 → "22nd", 113 → "113th").
/// @details Standard English rules:
///          - Numbers ending in 11/12/13 always take `th` (so "11th",
///            not "11st").
///          - Otherwise: 1 → `st`, 2 → `nd`, 3 → `rd`, anything else → `th`.
///          Sign is preserved on the number portion (so `-1 → "-1st"`).
rt_string rt_numfmt_ordinal(int64_t n) {
    const char *suffix;
    uint64_t abs_n = abs_i64_magnitude(n);
    uint64_t mod100 = abs_n % 100;
    uint64_t mod10 = abs_n % 10;

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

/// @brief Append the English-words form of `n` (`0..999`) to the builder.
/// @details Implements the standard 100s/tens/ones decomposition:
///          - `>= 100` → "X hundred" + remainder.
///          - `>= 20`  → tens word + optional `-` + ones (e.g. "thirty-two").
///          - `< 20`   → direct lookup in the `ones[]` table (which
///            also covers the irregular teen forms like "fifteen").
///          `has_prev` is set to 1 on first emission so callers can
///          insert a space before subsequent chunks (used by
///          `rt_numfmt_to_words` to separate scale words).
static void append_chunk(rt_string_builder *sb, int64_t n, int *has_prev) {
    if (n == 0)
        return;

    if (*has_prev)
        numfmt_check_sb(rt_sb_append_cstr(sb, " "), "NumberFormat.ToWords: formatting failed");

    if (n >= 100) {
        numfmt_check_sb(
            rt_sb_append_cstr(sb, ones[n / 100]), "NumberFormat.ToWords: formatting failed");
        numfmt_check_sb(
            rt_sb_append_cstr(sb, " hundred"), "NumberFormat.ToWords: formatting failed");
        n %= 100;
        if (n > 0)
            numfmt_check_sb(rt_sb_append_cstr(sb, " "), "NumberFormat.ToWords: formatting failed");
    }

    if (n >= 20) {
        numfmt_check_sb(
            rt_sb_append_cstr(sb, tens[n / 10]), "NumberFormat.ToWords: formatting failed");
        n %= 10;
        if (n > 0) {
            numfmt_check_sb(
                rt_sb_append_cstr(sb, "-"), "NumberFormat.ToWords: formatting failed");
            numfmt_check_sb(
                rt_sb_append_cstr(sb, ones[n]), "NumberFormat.ToWords: formatting failed");
        }
    } else if (n > 0) {
        numfmt_check_sb(rt_sb_append_cstr(sb, ones[n]), "NumberFormat.ToWords: formatting failed");
    }

    *has_prev = 1;
}

/// @brief Convert an integer to its English word representation (e.g., 42 -> "forty-two").
rt_string rt_numfmt_to_words(int64_t n) {
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
        numfmt_check_sb(
            rt_sb_append_cstr(&sb, "negative "), "NumberFormat.ToWords: formatting failed");

    // Break into groups of three
    static const char *const scale[] = {"",
                                        "thousand",
                                        "million",
                                        "billion",
                                        "trillion",
                                        "quadrillion",
                                        "quintillion",
                                        "sextillion"};
    int groups[8] = {0};
    int num_groups = 0;

    uint64_t temp = abs_n;
    while (temp > 0 && num_groups < 8) {
        groups[num_groups++] = (int)(temp % 1000);
        temp /= 1000;
    }

    int has_prev = 0;
    for (int i = num_groups - 1; i >= 0; i--) {
        if (groups[i] == 0)
            continue;
        append_chunk(&sb, groups[i], &has_prev);
        if (i > 0 && scale[i][0] != '\0') {
            numfmt_check_sb(
                rt_sb_append_cstr(&sb, " "), "NumberFormat.ToWords: formatting failed");
            numfmt_check_sb(
                rt_sb_append_cstr(&sb, scale[i]), "NumberFormat.ToWords: formatting failed");
        }
    }

    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

// ---------------------------------------------------------------------------
// rt_numfmt_bytes
// ---------------------------------------------------------------------------

/// @brief Format a byte count with a human-readable unit suffix (`B`, `KB`, `MB`, ...).
/// @details Steps the value down by factors of 1024 (binary, not
///          decimal) until it sits in `[0, 1024)`, then formats with
///          one decimal of precision and the matching unit. Capped
///          at exabytes (`EB`) — values above that just stay in EB.
///          Negative byte counts emit a leading `-` and use the
///          absolute value for unit selection.
rt_string rt_numfmt_bytes(int64_t bytes) {
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    uint64_t magnitude = abs_i64_magnitude(bytes);
    double val = (double)magnitude;
    int unit_idx = 0;

    while (val >= 1024.0 && unit_idx < 6) {
        val /= 1024.0;
        unit_idx++;
    }

    char buf[64];
    int len;

    if (unit_idx == 0) {
        len = snprintf(buf,
                       sizeof(buf),
                       "%s%" PRIu64 " %s",
                       bytes < 0 ? "-" : "",
                       magnitude,
                       units[0]);
    } else {
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

/// @brief Zero-pad an integer to the specified character width.
/// @details `width` is clamped to `[1, 64]`. For positive values,
///          uses printf's `%0*lld` directly. For negatives, the
///          width budget includes the leading `-` so `pad(-5, 4)`
///          produces `"-005"` (4 chars total, not 5).
rt_string rt_numfmt_pad(int64_t n, int64_t width) {
    if (width < 1)
        width = 1;
    if (width > 64)
        width = 64;

    char buf[128];
    int len;

    if (n >= 0)
        len = snprintf(buf, sizeof(buf), "%0*lld", (int)width, (long long)n);
    else {
        uint64_t magnitude = abs_i64_magnitude(n);
        len = snprintf(buf, sizeof(buf), "-%0*" PRIu64, (int)(width - 1), magnitude);
    }

    if (len < 0)
        len = 0;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;
    return rt_string_from_bytes(buf, (size_t)len);
}
