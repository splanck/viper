//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project and is released under the MIT
// License. See the LICENSE file accompanying this source for the full terms.
//
//===----------------------------------------------------------------------===//
//
// Implements numeric and CSV formatting helpers that mirror the BASIC runtime
// semantics.  The routines encapsulate locale-sensitive behaviour, handle
// special floating-point values deterministically, and generate quoted CSV
// strings without leaking heap ownership conventions across the runtime.
// Centralising the logic keeps formatting decisions consistent between the VM
// and native backends.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Floating-point and CSV formatting utilities for the runtime.
/// @details Provides deterministic double formatting with consistent decimal
///          separators and exposes an allocator that quotes runtime strings for
///          CSV emission.  Helpers trap on invalid buffer parameters so that
///          misuse fails fast during runtime development.

#include "rt_format.h"
#include "rt_internal.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Copy formatted text into a caller-provided buffer.
/// @details Validates buffer arguments, traps on truncation, and performs a
///          full copy including the null terminator.  Keeping the check in a
///          helper avoids repeating guard logic across the formatting functions.
/// @param text Null-terminated string to write.
/// @param buffer Destination buffer supplied by the caller.
/// @param capacity Size of the destination buffer in bytes.
static void rt_format_write(const char *text, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        rt_trap("rt_format_f64: invalid buffer");
    size_t len = strlen(text);
    if (len + 1 > capacity)
        rt_trap("rt_format_f64: truncated");
    memcpy(buffer, text, len + 1);
}

/// @brief Replace locale-specific decimal separators with '.'.
/// @details Scans the formatted buffer for the locale's decimal separator and
///          rewrites it to a period so BASIC output remains deterministic across
///          environments.  Multi-character separators are collapsed to a single
///          '.' by shifting the trailing substring.
/// @param buffer Mutable buffer containing the formatted number.
/// @param decimal_point Locale-specific decimal separator string.
static void rt_format_normalize_decimal(char *buffer, const char *decimal_point)
{
    if (!buffer || !decimal_point)
        return;
    if (decimal_point[0] == '\0' || (decimal_point[0] == '.' && decimal_point[1] == '\0'))
        return;
    size_t dp_len = strlen(decimal_point);
    if (dp_len == 0)
        return;

    char *pos = strstr(buffer, decimal_point);
    if (!pos)
        return;

    pos[0] = '.';
    if (dp_len > 1)
    {
        char *src = pos + dp_len;
        memmove(pos + 1, src, strlen(src) + 1);
    }
}

/// @brief Format a double according to BASIC runtime rules.
/// @details Handles NaN and infinity explicitly, otherwise emits up to 15
///          significant digits using `snprintf`.  After formatting, the locale
///          decimal separator is normalised to a period via
///          @ref rt_format_normalize_decimal.
/// @param value Floating-point value to format.
/// @param buffer Destination buffer supplied by the caller.
/// @param capacity Size of the destination buffer in bytes.
void rt_format_f64(double value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        rt_trap("rt_format_f64: invalid buffer");

    if (isnan(value))
    {
        rt_format_write("NaN", buffer, capacity);
        return;
    }
    if (isinf(value))
    {
        if (signbit(value))
            rt_format_write("-Inf", buffer, capacity);
        else
            rt_format_write("Inf", buffer, capacity);
        return;
    }

    int written = snprintf(buffer, capacity, "%.15g", value);
    if (written < 0)
        rt_trap("rt_format_f64: format error");
    if ((size_t)written >= capacity)
        rt_trap("rt_format_f64: truncated");

    struct lconv *info = localeconv();
    const char *decimal_point = info ? info->decimal_point : NULL;
    if (decimal_point)
        rt_format_normalize_decimal(buffer, decimal_point);
}

/// @brief Allocate a freshly quoted CSV string from a runtime string handle.
/// @details Duplicates the incoming text, doubles embedded quotes, wraps the
///          content in leading and trailing quotes, and returns a new
///          @ref rt_string that owns the allocated buffer.  The function traps
///          on allocation failure to match runtime expectations.
/// @param value Runtime string handle to quote. May be null for an empty string.
/// @return A newly allocated runtime string containing the CSV-safe text.
rt_string rt_csv_quote_alloc(rt_string value)
{
    const char *data = "";
    size_t len = 0;
    if (value)
    {
        data = rt_string_cstr(value);
        len = (size_t)rt_len(value);
    }

    size_t extra = 0;
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] == '"')
            ++extra;
    }

    size_t total = len + extra + 2; // leading and trailing quotes
    char *buffer = (char *)malloc(total + 1);
    if (!buffer)
        rt_trap("rt_csv_quote_alloc: out of memory");

    size_t pos = 0;
    buffer[pos++] = '"';
    for (size_t i = 0; i < len; ++i)
    {
        char ch = data[i];
        buffer[pos++] = ch;
        if (ch == '"')
        {
            buffer[pos++] = '"';
        }
    }
    buffer[pos++] = '"';
    buffer[pos] = '\0';

    rt_string result = rt_string_from_bytes(buffer, total);
    free(buffer);
    return result;
}
