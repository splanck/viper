//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime string conversion helpers that bridge between
// textual input and numeric values.  The routines here parse integer and double
// literals, format numbers with deterministic semantics, and allocate new
// runtime strings while honouring the shared heap ownership rules.  Centralising
// these utilities keeps the VM and native runtime perfectly aligned when
// handling INPUT statements and intrinsic string conversions.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Numeric-to-string conversion helpers for the runtime.
/// @details Supplies parsing helpers used by INPUT-style statements and a set
///          of allocation routines that format numeric types into fresh runtime
///          strings.  All functions trap on invalid arguments so host
///          applications observe consistent failure behaviour.

#include "rt_format.h"
#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_numeric.h"
#include "rt_string.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Parse a runtime string as a signed 64-bit integer.
/// @details Trims ASCII whitespace, validates that the entire string represents
///          a base-10 integer, and traps on overflow or invalid characters.
///          Conversion uses @ref strtoll so platform locale rules around sign
///          handling are respected.
/// @param s Runtime string containing the textual representation.
/// @return Parsed 64-bit integer value.
int64_t rt_to_int(rt_string s)
{
    if (!s)
        rt_trap("rt_to_int: null");
    const char *p = s->data;
    size_t len = (size_t)rt_len(s);
    size_t i = 0;
    while (i < len && isspace((unsigned char)p[i]))
        ++i;
    size_t j = len;
    while (j > i && isspace((unsigned char)p[j - 1]))
        --j;
    if (i == j)
        rt_trap("INPUT: expected numeric value");
    size_t sz = j - i;
    char *buf = (char *)rt_alloc(sz + 1);
    memcpy(buf, p + i, sz);
    buf[sz] = '\0';
    errno = 0;
    char *endp = NULL;
    long long v = strtoll(buf, &endp, 10);
    if (errno == ERANGE)
    {
        free(buf);
        rt_trap("INPUT: numeric overflow");
    }
    if (!endp || *endp != '\0')
    {
        free(buf);
        rt_trap("INPUT: expected numeric value");
    }
    free(buf);
    return (int64_t)v;
}

/// @brief Parse a runtime string into a double.
/// @details Delegates to @ref rt_val_to_double so the conversion logic remains
///          shared with other parts of the runtime.  The helper validates the
///          input handle, traps on overflow, and reports generic parse failures
///          using the BASIC diagnostic wording.
/// @param s Runtime string handle.
/// @return Parsed floating-point value.
double rt_to_double(rt_string s)
{
    if (!s)
        rt_trap("rt_to_double: null");
    bool ok = true;
    double value = rt_val_to_double(s->data, &ok);
    if (!ok)
    {
        if (!isfinite(value))
            rt_trap("INPUT: numeric overflow");
        rt_trap("INPUT: expected numeric value");
    }
    return value;
}

/// @brief Format a signed 64-bit integer into a newly allocated runtime string.
/// @details Uses a stack buffer for common cases, falling back to heap buffers
///          when the decimal representation does not fit.  The result is wrapped
///          into a @ref rt_string with ownership transferred to the caller.
/// @param v Integer value to format.
/// @return Fresh runtime string containing the decimal representation.
rt_string rt_int_to_str(int64_t v)
{
    char stack_buf[32];
    char *buf = stack_buf;
    size_t cap = sizeof(stack_buf);
    char *heap_buf = NULL;

    size_t written = rt_i64_to_cstr(v, buf, cap);
    if (written == 0 && buf[0] == '\0')
        rt_trap("rt_int_to_str: format");

    while (written + 1 >= cap)
    {
        if (cap > SIZE_MAX / 2)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: overflow");
        }
        size_t new_cap = cap * 2;
        char *new_buf = (char *)malloc(new_cap);
        if (!new_buf)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: alloc");
        }
        size_t new_written = rt_i64_to_cstr(v, new_buf, new_cap);
        if (new_written == 0 && new_buf[0] == '\0')
        {
            free(new_buf);
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: format");
        }
        if (heap_buf)
            free(heap_buf);
        heap_buf = new_buf;
        buf = new_buf;
        cap = new_cap;
        written = new_written;
    }

    rt_string s = rt_string_from_bytes(buf, written);
    if (heap_buf)
        free(heap_buf);
    return s;
}

/// @brief Convert a double to a runtime string using BASIC formatting rules.
/// @details Formats the value via @ref rt_format_f64 and copies the textual
///          result into a new runtime string.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_f64_to_str(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Alias for @ref rt_f64_to_str retained for compatibility with legacy
///        callers.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_str_d_alloc(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a float value as a runtime string.
/// @details Promotes the value to double to reuse @ref rt_format_f64 and allocates
///          the resulting textual representation.
/// @param v Float value to format.
/// @return Newly allocated runtime string with the formatted value.
rt_string rt_str_f_alloc(float v)
{
    char buf[64];
    rt_format_f64((double)v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 32-bit integer into a runtime string.
/// @details Utilises @ref rt_str_from_i32 to populate a caller-managed buffer
///          before wrapping it into a @ref rt_string instance.
/// @param v Integer value to format.
/// @return Newly allocated runtime string containing the decimal text.
rt_string rt_str_i32_alloc(int32_t v)
{
    char buf[32];
    rt_str_from_i32(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 16-bit integer into a runtime string.
/// @details Calls @ref rt_str_from_i16 so the behaviour matches the integer
///          printing routines used elsewhere in the runtime.
/// @param v Integer value to format.
/// @return Newly allocated runtime string containing the decimal text.
rt_string rt_str_i16_alloc(int16_t v)
{
    char buf[16];
    rt_str_from_i16(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Parse a runtime string using BASIC's `VAL` semantics.
/// @details Mirrors @ref rt_to_double but exposes the return value even when the
///          parse fails so callers can inspect overflow results.  The function
///          traps when the handle is null or when the parse produced NaN.
/// @param s Runtime string handle.
/// @return Parsed floating-point value (possibly infinity on overflow).
double rt_val(rt_string s)
{
    if (!s)
        rt_trap("rt_val: null");
    bool ok = true;
    double value = rt_val_to_double(s->data, &ok);
    if (!ok)
    {
        if (!isfinite(value))
            rt_trap("rt_val: overflow");
        return value;
    }
    return value;
}

/// @brief Convenience wrapper that formats a double via @ref rt_f64_to_str.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}
