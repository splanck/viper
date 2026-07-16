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
#include "rt_string_internal.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Validate @p s as a runtime string and return its byte view, trapping on misuse.
/// @details Central input-validation helper used by `rt_to_int`, `rt_to_double`, and
///          related parsers. Traps with @p fn_name as the diagnostic when @p s is NULL,
///          with a generic "INPUT: invalid string handle" message when the pointer fails
///          `rt_string_is_handle`, and with "INPUT: invalid string data" when the underlying
///          data pointer is somehow NULL despite a valid handle. On success writes the
///          byte length to @p len and returns the bytes pointer (aliasing the string's
///          storage; valid only for the lifetime of @p s).
static const char *rt_string_format_bytes(rt_string s, size_t *len, const char *fn_name) {
    if (len)
        *len = 0;
    if (!s) {
        rt_trap(fn_name);
        return "";
    }
    if (!rt_string_is_handle(s)) {
        rt_trap("INPUT: invalid string handle");
        return "";
    }
    size_t n = rt_string_len_bytes(s);
    const char *data = rt_string_cstr(s);
    if (!data) {
        rt_trap("INPUT: invalid string data");
        return "";
    }
    if (len)
        *len = n;
    return data;
}

/// @brief Return true if @p s contains an embedded NUL byte before its declared end.
/// @details Used by `rt_to_double` to reject strings that would silently truncate when
///          handed to `strtod`. Routes through `rt_string_format_bytes` so handle
///          validation happens up front; a NULL string is treated as "no NUL" without
///          trapping (the strict path is the validation in `rt_string_format_bytes`).
static bool rt_string_contains_embedded_nul(rt_string s) {
    if (!s)
        return false;
    size_t len = 0;
    const char *data = rt_string_format_bytes(s, &len, "rt_to_double: null");
    return data && memchr(data, '\0', len) != NULL;
}

/// @brief Locale-independent test for ASCII whitespace characters.
/// @details Mirrors the ASCII-only behaviour of the BASIC runtime so input parsing
///          doesn't depend on the host's `LC_CTYPE` setting. Recognises space, tab,
///          newline, carriage return, form-feed, and vertical-tab.
static bool rt_string_format_is_ascii_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

/// @brief Parse a runtime string as a signed 64-bit integer.
/// @details Performs a staged conversion so diagnostics match the historical
///          BASIC runtime:
///          1. Trim leading/trailing ASCII whitespace without touching locale
///             state.
///          2. Reject embedded NUL bytes, then copy the trimmed slice into a
///             scratch buffer allocated via @ref rt_alloc.
///          3. Invoke @ref strtoll to honour sign handling and detect overflow.
///          4. Trap with a BASIC-style message on overflow or trailing junk.
/// @param s Runtime string containing the textual representation.
/// @return Parsed 64-bit integer value.
int64_t rt_to_int(rt_string s) {
    size_t len = 0;
    const char *p = rt_string_format_bytes(s, &len, "rt_to_int: null");
    size_t i = 0;
    while (i < len && rt_string_format_is_ascii_space((unsigned char)p[i]))
        ++i;
    size_t j = len;
    while (j > i && rt_string_format_is_ascii_space((unsigned char)p[j - 1]))
        --j;
    if (i == j) {
        rt_trap("INPUT: expected numeric value");
        return 0;
    }
    size_t sz = j - i;
    if (memchr(p + i, '\0', sz)) {
        rt_trap("INPUT: expected numeric value");
        return 0;
    }
    if (sz == SIZE_MAX || sz > (size_t)INT64_MAX - 1) {
        rt_trap("INPUT: numeric value too large");
        return 0;
    }
    char *buf = (char *)rt_alloc((int64_t)(sz + 1));
    if (!buf) {
        rt_trap("INPUT: allocation failed");
        return 0;
    }
    memcpy(buf, p + i, sz);
    buf[sz] = '\0';
    errno = 0;
    char *endp = NULL;
    long long v = strtoll(buf, &endp, 10);
    if (errno == ERANGE) {
        rt_free(buf);
        rt_trap("INPUT: numeric overflow");
        return 0;
    }
    if (!endp || *endp != '\0') {
        rt_free(buf);
        rt_trap("INPUT: expected numeric value");
        return 0;
    }
    rt_free(buf);
    return (int64_t)v;
}

/// @brief Parse a runtime string into a double.
/// @details Uses the strict low-level parser so the entire string must be a
///          decimal floating literal after ASCII trimming. Overflow raises a
///          dedicated BASIC diagnostic while any other parse failure becomes
///          the generic "expected numeric value" trap, mirroring INPUT
///          semantics.
/// @param s Runtime string handle.
/// @return Parsed floating-point value.
double rt_to_double(rt_string s) {
    if (rt_string_contains_embedded_nul(s)) {
        rt_trap("INPUT: expected numeric value");
        return 0.0;
    }
    double value = 0.0;
    size_t len = 0;
    const char *data = rt_string_format_bytes(s, &len, "rt_to_double: null");
    (void)len;
    int32_t err = rt_parse_double(data, &value);
    if (err == (int32_t)Err_Overflow) {
        rt_trap("INPUT: numeric overflow");
        return 0.0;
    }
    if (err != (int32_t)Err_None) {
        rt_trap("INPUT: expected numeric value");
        return 0.0;
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
rt_string rt_int_to_str(int64_t v) {
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_sb_status_t status = rt_sb_append_int(&sb, v);
    if (status != RT_SB_OK) {
        const char *msg = "rt_int_to_str: format";
        if (status == RT_SB_ERROR_ALLOC)
            msg = "rt_int_to_str: alloc";
        else if (status == RT_SB_ERROR_OVERFLOW)
            msg = "rt_int_to_str: overflow";
        else if (status == RT_SB_ERROR_INVALID)
            msg = "rt_int_to_str: invalid";
        rt_sb_free(&sb);
        rt_trap(msg);
        return rt_string_from_bytes("", 0);
    }

    rt_string s = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return s;
}

/// @brief Convert a double to a runtime string using exact round-trip formatting.
/// @details Relies on @ref rt_format_f64_roundtrip to produce locale-stable
///          decimal text that parses back to the same IEEE-754 value, then
///          copies the result into a freshly allocated runtime string whose
///          ownership transfers to the caller.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_f64_to_str(double v) {
    char buf[64];
    rt_format_f64_roundtrip(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Legacy entry point that forwards to @ref rt_f64_to_str.
/// @details Retained for ABI compatibility with historical runtime releases
///          that exported @c rt_str_d_alloc directly.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_str_d_alloc(double v) {
    char buf[64];
    rt_format_f64_roundtrip(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a value with single-precision (Float32) semantics.
/// @details Accepts a C `double` so the exported ABI matches the registered
///          `str(f64)` signature on every dispatch path (VDOC-162: taking a
///          C `float` here made direct symbol dispatch read the wrong bit
///          layout). The value is narrowed to `float` first so the output
///          reflects single precision, then formatted through
///          @ref rt_format_f64 for consistent rounding.
/// @param v Value to format (narrowed to single precision).
/// @return Newly allocated runtime string with the formatted value.
rt_string rt_str_f_alloc(double v) {
    char buf[64];
    rt_format_f64((double)(float)v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 32-bit integer into a runtime string.
/// @details Uses @ref rt_str_from_i32 to write into a stack buffer before
///          wrapping that buffer in a runtime-managed allocation.  Using the
///          shared helper keeps zero-padding and sign handling consistent.
/// @param v Integer value to format.
/// @return Newly allocated runtime string containing the decimal text.
rt_string rt_str_i32_alloc(int32_t v) {
    char buf[32];
    rt_str_from_i32(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Format a 16-bit integer into a runtime string.
/// @details Calls @ref rt_str_from_i16 so behaviour matches the runtime's other
///          integer printers, including sign handling and overflow checking.
/// @param v Integer value to format.
/// @return Newly allocated runtime string containing the decimal text.
rt_string rt_str_i16_alloc(int16_t v) {
    char buf[16];
    rt_str_from_i16(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/// @brief Parse a runtime string using BASIC's `VAL` semantics.
/// @details BASIC `VAL` is forgiving: it skips leading whitespace, parses the
///          longest valid numeric prefix, and returns whatever strtod produced
///          (stopping at the first non-numeric character). Trailing garbage is
///          silently ignored — `VAL("  -12.5E+1x")` returns -125.0, not zero.
///          This differs from the stricter @ref rt_val_to_double which rejects
///          any residue. Null handles trap eagerly to avoid dereferencing
///          invalid pointers; overflow traps so callers don't silently accept
///          infinity from user input.
/// @param s Runtime string handle.
/// @return Parsed floating-point value; 0.0 when no numeric prefix is present.
double rt_val(rt_string s) {
    if (!s) {
        rt_trap("rt_val: null");
        return 0.0;
    }
    const char *data = s->data;
    if (!data)
        return 0.0;
    // BASIC VAL semantics: skip leading ASCII whitespace, parse longest
    // numeric prefix via strtod, ignore any trailing garbage. Process-default
    // LC_NUMERIC is C across every runtime path (we never call setlocale),
    // so plain strtod is safe without the C-locale-swap dance.
    while (*data == ' ' || *data == '\t' || *data == '\n' || *data == '\r' || *data == '\v' ||
           *data == '\f')
        ++data;
    if (!*data)
        return 0.0;
    errno = 0;
    char *end = NULL;
    double value = strtod(data, &end);
    if (end == data)
        return 0.0; // no numeric prefix at all
    if (errno == ERANGE && !isfinite(value))
        rt_trap("rt_val: overflow");
    return value;
}

/// @brief Convenience wrapper mirroring the historic `STR$` intrinsic.
/// @details Forwards to @ref rt_f64_to_str so the intrinsic reuses the same
///          formatting code path and therefore shares rounding and NaN/INF
///          behaviour with the rest of the runtime.
/// @param v Floating-point value to format.
/// @return Newly allocated runtime string containing the formatted value.
rt_string rt_str(double v) {
    return rt_f64_to_str(v);
}
