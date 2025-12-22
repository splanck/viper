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
#define FMT_BUFFER_SIZE 128
// Buffer size for binary formatting (64 bits + null)
#define FMT_BIN_BUFFER_SIZE 65

    rt_string rt_fmt_int(int64_t value)
    {
        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

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

    rt_string rt_fmt_bool(bool value)
    {
        return value ? rt_string_from_bytes("true", 4) : rt_string_from_bytes("false", 5);
    }

    rt_string rt_fmt_bool_yn(bool value)
    {
        return value ? rt_string_from_bytes("Yes", 3) : rt_string_from_bytes("No", 2);
    }

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
            len = snprintf(buffer,
                           sizeof(buffer),
                           "%s%.1f %s",
                           negative ? "-" : "",
                           size,
                           units[unit_idx]);
        }

        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

    rt_string rt_fmt_hex(int64_t value)
    {
        char buffer[FMT_BUFFER_SIZE];
        int len = snprintf(buffer, sizeof(buffer), "%" PRIx64, (uint64_t)value);
        if (len < 0 || (size_t)len >= sizeof(buffer))
            return rt_string_from_bytes("", 0);
        return rt_string_from_bytes(buffer, (size_t)len);
    }

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
