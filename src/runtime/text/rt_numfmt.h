//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_numfmt.h
// Purpose: Number formatting utilities.
// Key invariants: All returned strings are allocated and must be managed by caller.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Format a number with specified decimal places.
    /// @param n Number to format.
    /// @param decimals Number of decimal places.
    /// @return Formatted string (e.g., "1234.57").
    rt_string rt_numfmt_decimals(double n, int64_t decimals);

    /// @brief Format an integer with thousands separator.
    /// @param n Number to format.
    /// @param sep Separator string (e.g., ",").
    /// @return Formatted string (e.g., "1,234,567").
    rt_string rt_numfmt_thousands(int64_t n, rt_string sep);

    /// @brief Format a number as currency.
    /// @param n Amount to format.
    /// @param symbol Currency symbol (e.g., "$").
    /// @return Formatted string (e.g., "$1,234.56").
    rt_string rt_numfmt_currency(double n, rt_string symbol);

    /// @brief Format a number as a percentage.
    /// @param n Number to format (0.756 -> "75.6%").
    /// @return Formatted percentage string.
    rt_string rt_numfmt_percent(double n);

    /// @brief Format an integer as an ordinal.
    /// @param n Number to format.
    /// @return Ordinal string (e.g., "1st", "2nd", "3rd", "4th").
    rt_string rt_numfmt_ordinal(int64_t n);

    /// @brief Convert a number to English words.
    /// @param n Number to convert (supports up to trillions).
    /// @return English word string (e.g., "forty-two").
    rt_string rt_numfmt_to_words(int64_t n);

    /// @brief Format bytes as human-readable size.
    /// @param bytes Number of bytes.
    /// @return Formatted string (e.g., "1.5 KB", "3.2 MB").
    rt_string rt_numfmt_bytes(int64_t bytes);

    /// @brief Format an integer with zero-padding.
    /// @param n Number to format.
    /// @param width Minimum width (pad with leading zeros).
    /// @return Padded string (e.g., 42, 5 -> "00042").
    rt_string rt_numfmt_pad(int64_t n, int64_t width);

#ifdef __cplusplus
}
#endif
