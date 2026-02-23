//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_fmt.h
// Purpose: Value formatting functions for the Viper.Fmt namespace, providing decimal, radix, padded, fixed-precision, scientific, percentage, boolean, and human-readable byte-size string conversions.
//
// Key invariants:
//   - All formatting functions return newly allocated rt_string objects.
//   - Radix formatting accepts bases 2-36; invalid radix returns an empty string.
//   - rt_fmt_size uses IEC binary prefixes (KiB, MiB, GiB) for byte counts.
//   - Boolean formatting produces 'True'/'False' (rt_fmt_bool) or 'Yes'/'No' (rt_fmt_bool_yn).
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated with refcount 1; callers must release them.
//   - Input rt_string parameters are not retained; callers retain ownership.
//
// Links: src/runtime/core/rt_fmt.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Format an integer as decimal string.
    /// @param value The integer to format.
    /// @return Formatted string (e.g., "42", "-123").
    rt_string rt_fmt_int(int64_t value);

    /// @brief Format an integer in specified radix (base 2-36).
    /// @param value The integer to format.
    /// @param radix Base for formatting (2-36).
    /// @return Formatted string, or empty string if radix invalid.
    rt_string rt_fmt_int_radix(int64_t value, int64_t radix);

    /// @brief Format an integer with minimum width and padding character.
    /// @param value The integer to format.
    /// @param width Minimum width (pads on left if needed).
    /// @param pad_char String containing pad character (uses first char).
    /// @return Formatted string with padding.
    rt_string rt_fmt_int_pad(int64_t value, int64_t width, rt_string pad_char);

    /// @brief Format a number as decimal string with default precision.
    /// @param value The number to format.
    /// @return Formatted string (e.g., "3.14159").
    rt_string rt_fmt_num(double value);

    /// @brief Format a number with fixed decimal places.
    /// @param value The number to format.
    /// @param decimals Number of decimal places (0-20).
    /// @return Formatted string (e.g., "3.14" for decimals=2).
    rt_string rt_fmt_num_fixed(double value, int64_t decimals);

    /// @brief Format a number in scientific notation.
    /// @param value The number to format.
    /// @param decimals Number of decimal places in mantissa.
    /// @return Formatted string (e.g., "3.14e+00").
    rt_string rt_fmt_num_sci(double value, int64_t decimals);

    /// @brief Format a number as percentage.
    /// @param value The number to format (0.5 = 50%).
    /// @param decimals Number of decimal places.
    /// @return Formatted string with % suffix (e.g., "50.00%").
    rt_string rt_fmt_num_pct(double value, int64_t decimals);

    /// @brief Format a boolean as "true" or "false".
    /// @param value The boolean to format.
    /// @return "true" or "false".
    rt_string rt_fmt_bool(bool value);

    /// @brief Format a boolean as "Yes" or "No".
    /// @param value The boolean to format.
    /// @return "Yes" or "No".
    rt_string rt_fmt_bool_yn(bool value);

    /// @brief Format a byte size as human-readable string.
    /// @param bytes The size in bytes.
    /// @return Human-readable size (e.g., "1.5 KB", "2.3 MB").
    rt_string rt_fmt_size(int64_t bytes);

    /// @brief Format an integer as hexadecimal (lowercase).
    /// @param value The integer to format.
    /// @return Hex string without prefix (e.g., "ff", "deadbeef").
    rt_string rt_fmt_hex(int64_t value);

    /// @brief Format an integer as padded hexadecimal.
    /// @param value The integer to format.
    /// @param width Minimum width (pads with '0' on left).
    /// @return Padded hex string (e.g., "00ff" for width=4).
    rt_string rt_fmt_hex_pad(int64_t value, int64_t width);

    /// @brief Format an integer as binary.
    /// @param value The integer to format.
    /// @return Binary string without prefix (e.g., "1010").
    rt_string rt_fmt_bin(int64_t value);

    /// @brief Format an integer as octal.
    /// @param value The integer to format.
    /// @return Octal string without prefix (e.g., "77").
    rt_string rt_fmt_oct(int64_t value);

    /// @brief Format an integer with thousands separators.
    /// @param value The integer to format.
    /// @param sep Separator string (e.g., "," or ".").
    /// @return Formatted string (e.g., "1,234,567").
    rt_string rt_fmt_int_grouped(int64_t value, rt_string sep);

    /// @brief Format a number as currency.
    /// @param value The amount.
    /// @param decimals Decimal places (typically 2).
    /// @param symbol Currency symbol (e.g., "$", "€").
    /// @return Formatted string (e.g., "$1,234.56").
    rt_string rt_fmt_currency(double value, int64_t decimals, rt_string symbol);

    /// @brief Convert a non-negative integer to English words.
    /// @param value The number (0 to 999,999,999,999).
    /// @return English text (e.g., "one hundred twenty-three").
    rt_string rt_fmt_to_words(int64_t value);

    /// @brief Convert an integer to ordinal string.
    /// @param value The number.
    /// @return Ordinal string (e.g., "1st", "2nd", "3rd", "4th").
    rt_string rt_fmt_ordinal(int64_t value);

#ifdef __cplusplus
}
#endif
