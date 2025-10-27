//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides BASIC runtime helpers that convert between numeric values and string
// representations.  The routines centralise formatting so both the VM and
// native runtimes emit identical text for INPUT parsing, STR$ generation, and
// diagnostic messages.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Numeric/string conversion helpers for the BASIC runtime.
/// @details Implements parsing routines that honour BASIC's whitespace and
///          overflow rules alongside formatting helpers that reuse the runtime's
///          deterministic floating-point printers.

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

/// @brief Parse a runtime string into a 64-bit signed integer.
/// @details Trims ASCII whitespace, copies the digits into a temporary buffer,
///          and invokes @c strtoll.  Overflow and malformed input trigger traps
///          that reproduce BASIC's INPUT error messages.
/// @param s Runtime string to parse; must be non-null.
/// @return Parsed integer value.
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

/// @brief Parse a runtime string into a double-precision value.
/// @details Forwards to @ref rt_val_to_double, surfacing BASIC-compatible traps
///          for overflow or malformed input.
/// @param s Runtime string to parse; must be non-null.
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

/// @brief Convert a 64-bit integer into a runtime string.
/// @details Uses a stack buffer for common cases and dynamically grows it when
///          the decimal representation exceeds the initial capacity.  Formatting
///          or allocation failures raise traps to mirror VM behaviour.
/// @param v Integer to convert.
/// @return Newly allocated runtime string containing the decimal representation.
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
            free(heap_buf);
            rt_trap("rt_int_to_str: overflow");
        }
        size_t new_cap = cap * 2;
        char *new_buf = (char *)malloc(new_cap);
        if (!new_buf)
        {
            free(heap_buf);
            rt_trap("rt_int_to_str: alloc");
        }
        size_t new_written = rt_i64_to_cstr(v, new_buf, new_cap);
        if (new_written == 0 && new_buf[0] == '\0')
        {
            free(new_buf);
            free(heap_buf);
            rt_trap("rt_int_to_str: format");
        }
        free(heap_buf);
        heap_buf = new_buf;
        buf = new_buf;
        cap = new_cap;
        written = new_written;
    }

    rt_string s = rt_string_from_bytes(buf, written);
    free(heap_buf);
    return s;
}

/// @brief Convert a double to a runtime string using BASIC formatting.
/// @details Delegates to @ref rt_format_f64 and wraps the result in an
///          @ref rt_string handle.
/// @param v Value to convert.
/// @return Newly allocated runtime string.
rt_string rt_f64_to_str(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a double for routines expecting @c rt_string ownership.
/// @details Convenience wrapper around @ref rt_f64_to_str used by generated code.
/// @param v Value to convert.
/// @return Newly allocated runtime string.
rt_string rt_str_d_alloc(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a single-precision float as a runtime string.
/// @details Promotes @p v to double before delegating to @ref rt_format_f64 so
///          the textual output matches the VM.
/// @param v Value to convert.
/// @return Newly allocated runtime string.
rt_string rt_str_f_alloc(float v)
{
    char buf[64];
    rt_format_f64((double)v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 32-bit integer as a runtime string.
/// @details Uses @ref rt_str_from_i32 to obtain a canonical decimal
///          representation and wraps the result in an @ref rt_string.
/// @param v Value to convert.
/// @return Newly allocated runtime string.
rt_string rt_str_i32_alloc(int32_t v)
{
    char buf[32];
    rt_str_from_i32(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 16-bit integer as a runtime string.
/// @details Mirrors @ref rt_str_i32_alloc but calls @ref rt_str_from_i16.
/// @param v Value to convert.
/// @return Newly allocated runtime string.
rt_string rt_str_i16_alloc(int16_t v)
{
    char buf[16];
    rt_str_from_i16(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Convert a runtime string to a double following BASIC's VAL rules.
/// @details Delegates to @ref rt_val_to_double and surfaces BASIC-specific traps
///          for overflow.  Unlike @ref rt_to_double the function returns the
///          partially parsed value when the parse fails to match legacy
///          behaviour.
/// @param s Runtime string to parse; must be non-null.
/// @return Parsed double value (possibly @c NaN) when parsing fails.
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

/// @brief Alias for @ref rt_f64_to_str matching legacy naming.
/// @param v Value to format.
/// @return Newly allocated runtime string.
rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}
