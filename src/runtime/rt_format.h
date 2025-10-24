// File: src/runtime/rt_format.h
// Purpose: Declares deterministic formatting helpers for runtime numeric types.
// Key invariants: Formatting is locale-independent and normalizes special values.
// Ownership/Lifetime: Callers provide output buffers and own their storage.
// Links: docs/codemap.md

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
