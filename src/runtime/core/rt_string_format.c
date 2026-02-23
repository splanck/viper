//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_format.c
// Purpose: Implements BASIC's numeric/string conversion pipeline for the
//          native runtime. Provides parsing helpers for INPUT-style statements
//          and allocation routines that format numeric types (int, float) into
//          fresh reference-counted runtime strings.
//
// Key invariants:
//   - Parsing trims leading/trailing ASCII whitespace before conversion;
//     trailing non-numeric characters after a valid number cause a trap.
//   - Overflow (ERANGE from strtoll/strtod) is detected and trapped with a
//     BASIC-style diagnostic rather than silently wrapping.
//   - Formatting always produces locale-stable output (decimal separator is
//     always '.'); locale-specific separators are rewritten post-format.
//   - All allocation routines return reference-counted runtime strings that
//     transfer ownership to the caller (caller must eventually rt_string_unref).
//   - Errors surface through rt_trap so VM and native executions diverge only
//     at the diagnostic boundary.
//
// Ownership/Lifetime:
//   - Returned rt_string values are newly allocated; the caller owns the
//     reference and must call rt_string_unref when done.
//   - Intermediate scratch buffers are stack-allocated or freed before return.
//
// Links: src/runtime/core/rt_string.h (rt_string type),
//        src/runtime/core/rt_format.c (double formatting helpers),
//        src/runtime/core/rt_string_builder.c (growable buffer),
//        docs/runtime/strings.md#numeric-formatting
//
//===----------------------------------------------------------------------===//

#include "rt_format.h"
#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_numeric.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Parse a runtime string as a signed 64-bit integer.
/// @details Performs a staged conversion so diagnostics match the historical
///          BASIC runtime:
///          1. Trim leading/trailing ASCII whitespace without touching locale
///             state.
///          2. Copy the trimmed slice into a scratch buffer allocated via
///             @ref rt_alloc so the process works even with embedded NUL bytes
///             (which trap later).
///          3. Invoke @ref strtoll to honour sign handling and detect overflow.
///          4. Trap with a BASIC-style message on overflow or trailing junk.
/// @param s Runtime string containing the textual representation.
/// @return Parsed 64-bit integer value.
int64_t rt_to_int(rt_string s)
{
    if (!s)
        rt_trap("rt_to_int: null");
    const char *p = s->data;
    size_t len = (size_t)rt_str_len(s);
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
/// @details Defers to the shared @ref rt_val_to_double helper so floating-point
///          quirks (NaN tokens, INF spelling, banker rounding) remain
///          centralised.  Overflow raises a dedicated BASIC diagnostic while any
///          other parse failure becomes the generic "expected numeric value"
///          trap, mirroring INPUT semantics.
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
/// @details Builds the textual representation in a @ref rt_string_builder so the
///          implementation benefits from the builder's overflow-aware reserve
///          logic.  Formatting failures propagate through status codes and are
///          converted to rt_trap messages to preserve BASIC's fatal-error model.
/// @param v Integer value to format.
/// @return Fresh runtime string containing the decimal representation.
rt_string rt_int_to_str(int64_t v)
{
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_sb_status status = rt_sb_append_int(&sb, v);
    if (status != RT_SB_OK)
    {
        const char *msg = "rt_int_to_str: format";
        if (status == RT_SB_ERROR_ALLOC)
            msg = "rt_int_to_str: alloc";
        else if (status == RT_SB_ERROR_OVERFLOW)
            msg = "rt_int_to_str: overflow";
        else if (status == RT_SB_ERROR_INVALID)
            msg = "rt_int_to_str: invalid";
        rt_sb_free(&sb);
        rt_trap(msg);
    }

    rt_string s = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return s;
}

/// @brief Convert a double to a runtime string using BASIC formatting rules.
/// @details Relies on @ref rt_format_f64 to produce locale-stable decimal text,
///          then copies the result into a freshly allocated runtime string whose
///          ownership transfers to the caller.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_f64_to_str(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Legacy entry point that forwards to @ref rt_f64_to_str.
/// @details Retained for ABI compatibility with historical runtime releases
///          that exported @c rt_str_d_alloc directly.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_str_d_alloc(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a float value as a runtime string.
/// @details Promotes to double so @ref rt_format_f64 can be reused, guaranteeing
///          the same rounding behaviour as other BASIC numeric printers.
/// @param v Float value to format.
/// @return Newly allocated runtime string with the formatted value.
rt_string rt_str_f_alloc(float v)
{
    char buf[64];
    rt_format_f64((double)v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 32-bit integer into a runtime string.
/// @details Uses @ref rt_str_from_i32 to write into a stack buffer before
///          wrapping that buffer in a runtime-managed allocation.  Using the
///          shared helper keeps zero-padding and sign handling consistent.
/// @param v Integer value to format.
/// @return Newly allocated runtime string containing the decimal text.
rt_string rt_str_i32_alloc(int32_t v)
{
    char buf[32];
    rt_str_from_i32(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 16-bit integer into a runtime string.
/// @details Calls @ref rt_str_from_i16 so behaviour matches the runtime's other
///          integer printers, including sign handling and overflow checking.
/// @param v Integer value to format.
/// @return Newly allocated runtime string containing the decimal text.
rt_string rt_str_i16_alloc(int16_t v)
{
    char buf[16];
    rt_str_from_i16(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Parse a runtime string using BASIC's `VAL` semantics.
/// @details Calls @ref rt_val_to_double to perform the heavy lifting but, unlike
///          @ref rt_to_double, returns the floating-point value even when the
///          parse fails.  The caller can then decide whether infinities indicate
///          overflow.  Null handles trap eagerly to avoid dereferencing invalid
///          pointers.
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

/// @brief Convenience wrapper mirroring the historic `STR$` intrinsic.
/// @details Forwards to @ref rt_f64_to_str so the intrinsic reuses the same
///          formatting code path and therefore shares rounding and NaN/INF
///          behaviour with the rest of the runtime.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}
