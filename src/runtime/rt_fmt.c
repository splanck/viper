//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_fmt.c
// Purpose: Value formatting functions for Viper.Fmt namespace.
//
// Key invariants: All functions return valid strings, never trap.
//                 Invalid inputs produce empty strings or sensible defaults.
// Ownership/Lifetime: Returns newly allocated strings; caller owns result.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements formatting helpers for the Viper.Fmt runtime namespace.
/// @details Provides conversions from numeric and boolean values to runtime
///          strings. Functions are defensive: invalid inputs or formatting
///          failures yield empty strings rather than trapping.

#include "rt_fmt.h"
#include "rt_internal.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Buffer size for most formatting operations
/// @brief Default temporary buffer size for formatting operations.
/// @details Chosen to accommodate typical numeric formats without heap
///          allocation while keeping stack usage predictable.
#define FMT_BUFFER_SIZE 128
// Buffer size for binary formatting (64 bits + null)
/// @brief Buffer size for binary formatting of 64-bit values.
/// @details Holds 64 digits plus a NUL terminator.
#define FMT_BIN_BUFFER_SIZE 65

    /// @brief Format a signed 64-bit integer in decimal.
    /// @details Uses `snprintf` with the C locale and returns an empty string
    ///          on formatting failure or truncation. The result owns its bytes
    ///          and must be released by the caller.
    /// @param value Integer value to format.
    /// @return Newly allocated runtime string with decimal text.
    rt_string rt_fmt_int(int64_t value)
    {
        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format a signed integer using a specified radix (base 2-36).
    /// @details Radix values outside 2-36 return an empty string. For radix 10,
    ///          negative values are emitted with a leading '-' and INT64_MIN is
    ///          handled safely. For non-decimal radices, the value is treated
    ///          as unsigned so the bit pattern is preserved.
    /// @param value Integer to format.
    /// @param radix Base to use (2-36).
    /// @return Newly allocated string containing the formatted number.
    rt_string rt_fmt_int_radix(int64_t value, int64_t radix)
    {
        // Validate radix
        if (radix < 2 || radix > 36)
            return rt_string_from_bytes("", 0);

        // Handle zero specially
        if (value == 0)
            return rt_string_from_bytes("0", 1);

        // Handle negative numbers for non-decimal bases
        bool negative = false;
        uint64_t uval;
        if (value < 0 && radix == 10)
        {
            negative = true;
            uval = (uint64_t)(-(value + 1)) + 1; // Handle INT64_MIN
        }
        else
        {
            // For non-decimal bases, treat as unsigned
            uval = (uint64_t)value;
        }

        static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        char buffer[FMT_BUFFER_SIZE];
        char *p = buffer + sizeof(buffer) - 1;
        *p = '\0';

        while (uval > 0)
        {
            --p;
            *p = digits[uval % (uint64_t)radix];
            uval /= (uint64_t)radix;
        }

        if (negative)
        {
            --p;
            *p = '-';
        }

        size_t len = (size_t)(buffer + sizeof(buffer) - 1 - p);
        return rt_string_from_bytes(p, len);
    }

    /// @brief Format an integer with a minimum width and pad character.
    /// @details The output is left-padded to @p width using the first character
    ///          of @p pad_char (defaulting to space if empty). When padding with
    ///          '0' and the value is negative, the sign is emitted before the
    ///          zeros to match typical numeric formatting conventions. Width is
    ///          clamped to the internal buffer size to avoid overflow.
    /// @param value Integer to format.
    /// @param width Minimum output width in characters.
    /// @param pad_char Runtime string containing the pad character.
    /// @return Newly allocated string containing the padded number.
    rt_string rt_fmt_int_pad(int64_t value, int64_t width, rt_string pad_char)
    {
        // Get the pad character (default to space)
        char pad = ' ';
        const char *pad_str = rt_string_cstr(pad_char);
        if (pad_str && pad_str[0] != '\0')
            pad = pad_str[0];

        // Format the number
        char num_buf[FMT_BUFFER_SIZE];
        int num_len = snprintf(num_buf, sizeof(num_buf), "%" PRId64, value);
        if (num_len < 0 || (size_t)num_len >= sizeof(num_buf))
            return rt_string_from_bytes("", 0);

        // If width <= number length, return as-is
        if (width <= 0 || width <= num_len)
            return rt_string_from_bytes(num_buf, (size_t)num_len);

        // Build padded result
        char buffer[FMT_BUFFER_SIZE * 2];
        if ((size_t)width >= sizeof(buffer))
            width = (int64_t)(sizeof(buffer) - 1);

        size_t pad_count = (size_t)width - (size_t)num_len;

        // Handle negative numbers: put sign before padding if pad is '0'
        size_t pos = 0;
        if (value < 0 && pad == '0')
        {
            buffer[pos++] = '-';
            // Skip the minus sign in num_buf
            for (size_t i = 0; i < pad_count; ++i)
                buffer[pos++] = pad;
            memcpy(buffer + pos, num_buf + 1, (size_t)num_len - 1);
            pos += (size_t)num_len - 1;
        }
        else
        {
            for (size_t i = 0; i < pad_count; ++i)
                buffer[pos++] = pad;
            memcpy(buffer + pos, num_buf, (size_t)num_len);
            pos += (size_t)num_len;
        }

        return rt_string_from_bytes(buffer, pos);
    }

    /// @brief Format a floating-point number with default precision.
    /// @details Uses `%g` formatting to produce a compact representation. NaN
    ///          and infinity are mapped to "NaN", "Infinity", or "-Infinity".
    ///          Formatting failures return an empty string.
    /// @param value Floating-point value to format.
    /// @return Newly allocated runtime string for the formatted number.
    rt_string rt_fmt_num(double value)
    {
        // Handle special cases
        if (isnan(value))
            return rt_string_from_bytes("NaN", 3);
        if (isinf(value))
            return value > 0 ? rt_string_from_bytes("Infinity", 8)
                             : rt_string_from_bytes("-Infinity", 9);

        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%g", value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format a floating-point number with fixed decimal places.
    /// @details Decimal places are clamped to [0, 20] to keep buffer usage
    ///          bounded. NaN and infinity are mapped to their textual forms.
    /// @param value Floating-point value to format.
    /// @param decimals Requested number of fractional digits.
    /// @return Newly allocated runtime string for the formatted number.
    rt_string rt_fmt_num_fixed(double value, int64_t decimals)
    {
        // Handle special cases
        if (isnan(value))
            return rt_string_from_bytes("NaN", 3);
        if (isinf(value))
            return value > 0 ? rt_string_from_bytes("Infinity", 8)
                             : rt_string_from_bytes("-Infinity", 9);

        // Clamp decimals
        if (decimals < 0)
            decimals = 0;
        if (decimals > 20)
            decimals = 20;

        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%.*f", (int)decimals, value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format a floating-point number in scientific notation.
    /// @details Decimal places are clamped to [0, 20]. NaN and infinity are
    ///          mapped to their textual forms. Uses `%e` for exponent output.
    /// @param value Floating-point value to format.
    /// @param decimals Requested number of fractional digits.
    /// @return Newly allocated runtime string in scientific notation.
    rt_string rt_fmt_num_sci(double value, int64_t decimals)
    {
        // Handle special cases
        if (isnan(value))
            return rt_string_from_bytes("NaN", 3);
        if (isinf(value))
            return value > 0 ? rt_string_from_bytes("Infinity", 8)
                             : rt_string_from_bytes("-Infinity", 9);

        // Clamp decimals
        if (decimals < 0)
            decimals = 0;
        if (decimals > 20)
            decimals = 20;

        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%.*e", (int)decimals, value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format a floating-point number as a percentage.
    /// @details Multiplies the input by 100, formats with fixed decimals, and
    ///          appends a '%' suffix. NaN and infinity are mapped to textual
    ///          forms that include the suffix.
    /// @param value Input value where 1.0 means 100%.
    /// @param decimals Number of fractional digits to emit.
    /// @return Newly allocated runtime string with percent formatting.
    rt_string rt_fmt_num_pct(double value, int64_t decimals)
    {
        // Handle special cases
        if (isnan(value))
            return rt_string_from_bytes("NaN%", 4);
        if (isinf(value))
            return value > 0 ? rt_string_from_bytes("Infinity%", 9)
                             : rt_string_from_bytes("-Infinity%", 10);

        // Clamp decimals
        if (decimals < 0)
            decimals = 0;
        if (decimals > 20)
            decimals = 20;

        double pct = value * 100.0;
        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%.*f%%", (int)decimals, pct);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format a boolean as lowercase "true"/"false".
    /// @details This mirrors the canonical Viper boolean spelling used in
    ///          diagnostics and string conversions.
    /// @param value Boolean value to format.
    /// @return Newly allocated runtime string with "true" or "false".
    rt_string rt_fmt_bool(bool value)
    {
        return value ? rt_string_from_bytes("true", 4) : rt_string_from_bytes("false", 5);
    }

    /// @brief Format a boolean as "Yes"/"No".
    /// @details Provides a user-friendly, title-cased representation for
    ///          boolean values in UI-facing contexts.
    /// @param value Boolean value to format.
    /// @return Newly allocated runtime string with "Yes" or "No".
    rt_string rt_fmt_bool_yn(bool value)
    {
        return value ? rt_string_from_bytes("Yes", 3) : rt_string_from_bytes("No", 2);
    }

    /// @brief Format a byte count into a human-readable size string.
    /// @details Converts the absolute byte count into units of 1024, selecting
    ///          the largest unit where the magnitude remains >= 1. For bytes,
    ///          the output is an integer (e.g., "512 B"); for larger units, one
    ///          decimal place is emitted (e.g., "1.5 MB"). Negative sizes keep
    ///          a leading '-' sign.
    /// @param bytes Byte count to format.
    /// @return Newly allocated runtime string with size and unit suffix.
    rt_string rt_fmt_size(int64_t bytes)
    {
        static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
        static const int num_units = sizeof(units) / sizeof(units[0]);

        // Handle negative sizes
        bool negative = bytes < 0;
        uint64_t ubytes;
        if (bytes < 0)
            ubytes = (uint64_t)(-(bytes + 1)) + 1; // Handle INT64_MIN
        else
            ubytes = (uint64_t)bytes;

        double size = (double)ubytes;
        int unit_idx = 0;

        while (size >= 1024.0 && unit_idx < num_units - 1)
        {
            size /= 1024.0;
            ++unit_idx;
        }

        char buffer[FMT_BUFFER_SIZE];
        int len;

        if (unit_idx == 0)
        {
            // Bytes - show as integer
            len = snprintf(buffer,
                           sizeof(buffer),
                           "%s%" PRIu64 " %s",
                           negative ? "-" : "",
                           ubytes,
                           units[unit_idx]);
        }
        else
        {
            // Units >= KB always show one decimal digit (e.g., 1.0 KB).
            len = snprintf(
                buffer, sizeof(buffer), "%s%.1f %s", negative ? "-" : "", size, units[unit_idx]);
        }

        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format an integer as lowercase hexadecimal.
    /// @details Treats the input as unsigned so the full 64-bit pattern is
    ///          preserved, then formats without a "0x" prefix.
    /// @param value Integer value to format.
    /// @return Newly allocated runtime string with hex digits.
    rt_string rt_fmt_hex(int64_t value)
    {
        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%" PRIx64, (uint64_t)value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format an integer as zero-padded hexadecimal.
    /// @details The width is clamped to [1, 16] to match 64-bit output size.
    ///          Padding uses '0' and no prefix is emitted.
    /// @param value Integer value to format.
    /// @param width Minimum width in hex digits.
    /// @return Newly allocated runtime string with padded hex digits.
    rt_string rt_fmt_hex_pad(int64_t value, int64_t width)
    {
        if (width <= 0)
            width = 1;
        if (width > 16)
            width = 16;

        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%0*" PRIx64, (int)width, (uint64_t)value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    /// @brief Format an integer as a binary string.
    /// @details Treats the input as unsigned and emits a minimal-length binary
    ///          representation without any prefix. Zero is returned as "0".
    /// @param value Integer value to format.
    /// @return Newly allocated runtime string containing binary digits.
    rt_string rt_fmt_bin(int64_t value)
    {
        // Handle zero specially
        if (value == 0)
            return rt_string_from_bytes("0", 1);

        char buffer[FMT_BIN_BUFFER_SIZE];
        char *p = buffer + sizeof(buffer) - 1;
        *p = '\0';

        uint64_t uval = (uint64_t)value;
        while (uval > 0)
        {
            --p;
            *p = (uval & 1) ? '1' : '0';
            uval >>= 1;
        }

        size_t len = (size_t)(buffer + sizeof(buffer) - 1 - p);
        return rt_string_from_bytes(p, len);
    }

    /// @brief Format an integer as lowercase octal.
    /// @details Treats the input as unsigned and emits an octal string without
    ///          any prefix.
    /// @param value Integer value to format.
    /// @return Newly allocated runtime string containing octal digits.
    rt_string rt_fmt_oct(int64_t value)
    {
        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%" PRIo64, (uint64_t)value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

#ifdef __cplusplus
}
#endif
