//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_numeric.h
// Purpose: Numeric type conversions, formatting, and parsing with BASIC semantics, including float-to-integer rounding (banker's rounding), overflow detection, and checked arithmetic variants.
//
// Key invariants:
//   - Float-to-integer conversion uses round-to-nearest-even (banker's rounding).
//   - Overflow and NaN inputs set the error flag; they never produce undefined behavior.
//   - Checked variants (rt_sqrt_chk_f64, etc.) trap on domain errors.
//   - Integer division and modulo functions trap on zero divisor.
//
// Ownership/Lifetime:
//   - Callers own output buffers for formatting functions.
//   - No heap allocation in pure arithmetic functions; formatting may allocate strings.
//
// Links: src/runtime/core/rt_numeric.c (implementation), src/runtime/core/rt_error.h, src/runtime/core/rt_math.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rt_error.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Convert double @p x to INTEGER using round-to-nearest-even.
    /// @param x Input value.
    /// @param ok Output flag cleared when the conversion overflows or input is non-finite.
    /// @return Rounded INTEGER value when @p ok is true; unspecified otherwise.
    int16_t rt_cint_from_double(double x, bool *ok);

    /// @brief Convert double @p x to LONG using round-to-nearest-even.
    /// @param x Input value.
    /// @param ok Output flag cleared when the conversion overflows or input is non-finite.
    /// @return Rounded LONG value when @p ok is true; unspecified otherwise.
    int32_t rt_clng_from_double(double x, bool *ok);

    /// @brief Convert double @p x to SINGLE.
    /// @param x Input value.
    /// @param ok Output flag cleared when the conversion overflows or input is non-finite.
    /// @return Rounded SINGLE value when @p ok is true; unspecified otherwise.
    float rt_csng_from_double(double x, bool *ok);

    /// @brief Promote any finite numeric input to DOUBLE.
    /// @param x Input value (already validated as finite).
    /// @return Equivalent DOUBLE representation.
    double rt_cdbl_from_any(double x);

    /// @brief Compute INT(x) using floor semantics.
    /// @param x Input value.
    /// @return Greatest integer less than or equal to @p x as DOUBLE.
    double rt_int_floor(double x);

    /// @brief Compute FIX(x) using truncation toward zero.
    /// @param x Input value.
    /// @return Truncated value as DOUBLE.
    double rt_fix_trunc(double x);

    /// @brief Convert DOUBLE to i64 by truncating toward zero.
    /// @param x Input floating-point value.
    /// @return Truncated value as 64-bit signed integer.
    long long rt_f64_to_i64(double x);

    /// @brief Round @p x to @p ndigits decimal places using banker's rounding.
    /// @param x Input value.
    /// @param ndigits Number of digits after the decimal point (negative for tens, hundreds, ...).
    /// @return Rounded DOUBLE value.
    double rt_round_even(double x, int ndigits);

    /// @brief Parse C string @p s using BASIC VAL semantics.
    /// @param s Null-terminated input string; must not be NULL.
    /// @param ok Output flag cleared on overflow or invalid input format.
    /// @return Parsed DOUBLE value; returns 0 when no digits were consumed.
    double rt_val_to_double(const char *s, bool *ok);

    /// @brief Format DOUBLE @p x into @p out using round-trip precision.
    /// @param x Value to format.
    /// @param out Destination buffer; must not be NULL.
    /// @param cap Capacity of @p out in bytes; must be non-zero.
    /// @param out_err Optional error record receiving Err_None on success.
    void rt_str_from_double(double x, char *out, size_t cap, RtError *out_err);

    /// @brief Format SINGLE @p x into @p out using round-trip precision.
    /// @param x Value to format.
    /// @param out Destination buffer; must not be NULL.
    /// @param cap Capacity of @p out in bytes; must be non-zero.
    /// @param out_err Optional error record receiving Err_None on success.
    void rt_str_from_float(float x, char *out, size_t cap, RtError *out_err);

    /// @brief Format LONG @p x into @p out as minimal decimal digits.
    /// @param x Value to format.
    /// @param out Destination buffer; must not be NULL.
    /// @param cap Capacity of @p out in bytes; must be non-zero.
    /// @param out_err Optional error record receiving Err_None on success.
    void rt_str_from_i32(int32_t x, char *out, size_t cap, RtError *out_err);

    /// @brief Format INTEGER @p x into @p out as minimal decimal digits.
    /// @param x Value to format.
    /// @param out Destination buffer; must not be NULL.
    /// @param cap Capacity of @p out in bytes; must be non-zero.
    /// @param out_err Optional error record receiving Err_None on success.
    void rt_str_from_i16(int16_t x, char *out, size_t cap, RtError *out_err);

    /// @brief Parse signed 64-bit integer from trimmed C string @p text.
    /// @param text Null-terminated ASCII string; may contain leading/trailing whitespace.
    /// @param out_value Destination for parsed result; must not be NULL.
    /// @return Err_None on success, otherwise an appropriate runtime error code.
    int32_t rt_parse_int64(const char *text, int64_t *out_value);

    /// @brief Parse double-precision floating point from trimmed C string @p text.
    /// @param text Null-terminated ASCII string; may contain leading/trailing whitespace.
    /// @param out_value Destination for parsed result; must not be NULL.
    /// @return Err_None on success, otherwise an appropriate runtime error code.
    int32_t rt_parse_double(const char *text, double *out_value);

#ifdef __cplusplus
}
#endif
