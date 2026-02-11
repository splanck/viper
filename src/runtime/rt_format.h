//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_format.h
// Purpose: Deterministic, locale-independent formatting utilities for runtime
//          values, providing f64-to-string conversion and CSV quoting for
//          BASIC PRINT and WRITE# statement output.
// Key invariants: rt_format_f64 output is identical across platforms and
//                 locales; the caller supplies the buffer and capacity;
//                 rt_csv_quote_alloc returns a newly allocated string with
//                 surrounding quotes and doubled internal quotes.
// Ownership/Lifetime: rt_format_f64 writes into a caller-managed buffer with
//                     no allocation; rt_csv_quote_alloc returns a new rt_string
//                     owned by the caller.
// Links: docs/viperlib.md
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
