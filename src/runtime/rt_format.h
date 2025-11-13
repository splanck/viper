//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares deterministic formatting utilities for runtime values,
// particularly focused on numeric-to-string conversion with reproducible output.
// The BASIC language requires specific formatting behavior for PRINT statements
// and file I/O that differs from standard C library functions.
//
// Standard library formatting functions like printf and sprintf are locale-dependent
// and have platform-specific behavior for special floating-point values (NaN, infinity).
// Viper programs compiled from BASIC source must produce identical output regardless
// of the system locale or platform conventions. This file provides locale-independent
// formatters with well-defined handling of edge cases.
//
// Key Responsibilities:
// - Floating-point formatting: rt_format_f64 converts doubles to strings with
//   consistent precision, special value handling, and no locale dependencies
// - CSV escaping: rt_csv_quote_alloc handles WRITE # statement string escaping,
//   implementing BASIC's CSV format with doubled quotes and proper delimiters
// - Caller-managed buffers: Functions accept pre-allocated buffers, avoiding
//   hidden allocation and supporting stack-based usage in performance-critical paths
//
// These formatters are used by the PRINT and WRITE # statement lowering in the
// BASIC frontend, ensuring deterministic output for golden test validation and
// cross-platform compatibility.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Format a double-precision value into a deterministic, locale-independent string.
    ///
    /// @param value Value to format.
    /// @param buffer Destination buffer for the textual representation.
    /// @param capacity Size of @p buffer in bytes, including space for the null terminator.
    void rt_format_f64(double value, char *buffer, size_t capacity);

    /// @brief Produce a CSV-escaped string literal for WRITE # statements.
    /// @param value Source string to escape; NULL treated as empty.
    /// @return Newly allocated string with surrounding quotes and doubled quotes inside.
    rt_string rt_csv_quote_alloc(rt_string value);

#ifdef __cplusplus
}
#endif
