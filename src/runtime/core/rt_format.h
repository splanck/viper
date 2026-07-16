//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_format.h
// Purpose: Deterministic, locale-independent formatting utilities providing f64-to-string
// conversion and CSV quoting for BASIC PRINT, WRITE#, and conversion helpers.
//
// Key invariants:
//   - rt_format_f64 output is identical across platforms and locales.
//   - rt_format_f64_roundtrip output parses back to the same IEEE-754 value.
//   - The caller supplies the output buffer and must ensure sufficient capacity.
//   - rt_csv_quote_alloc always surrounds the value with double-quotes and doubles any internal
//   quotes.
//   - NULL input to rt_csv_quote_alloc is treated as empty string.
//
// Ownership/Lifetime:
//   - rt_format_f64 and rt_format_f64_roundtrip write into caller-managed buffers with no
//     heap allocation.
//   - rt_csv_quote_alloc returns a new rt_string owned by the caller (must release).
//
// Links: src/runtime/core/rt_format.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief snprintf that always uses the C numeric locale for the conversion.
/// @details Deterministic decimal separators regardless of the embedding
///          process's LC_NUMERIC. Shared by the text-format emitters so their
///          numeric output is identical across locales (VDOC-041).
/// @param buffer Destination buffer.
/// @param capacity Size of @p buffer in bytes.
/// @param fmt printf-style format string.
/// @return Number of bytes written (excluding the terminator), or negative on error.
int rt_format_snprintf_c_locale(char *buffer, size_t capacity, const char *fmt, ...);

/// @brief Format a double-precision value into a deterministic display string.
///
/// @details This is the BASIC PRINT/WRITE# display form. It keeps historical
/// 15-significant-digit output for finite values and canonical spellings for
/// NaN/Inf. Use @ref rt_format_f64_roundtrip when the string must reparse to
/// the exact same double.
///
/// @param value Value to format.
/// @param buffer Destination buffer for the textual representation.
/// @param capacity Size of @p buffer in bytes, including space for the null terminator.
void rt_format_f64(double value, char *buffer, size_t capacity);

/// @brief Format a double-precision value into an exact round-trip string.
///
/// @param value Value to format.
/// @param buffer Destination buffer for the textual representation.
/// @param capacity Size of @p buffer in bytes, including space for the null terminator.
void rt_format_f64_roundtrip(double value, char *buffer, size_t capacity);

/// @brief Produce a CSV-escaped string literal for WRITE # statements.
/// @param value Source string to escape; NULL treated as empty.
/// @return Newly allocated string with surrounding quotes and doubled quotes inside.
rt_string rt_csv_quote_alloc(rt_string value);

#ifdef __cplusplus
}
#endif
