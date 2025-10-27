//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime helpers that format 64-bit integers into caller
// supplied buffers.  The routines guarantee locale-independent output and
// explicit null termination so callers can safely use the generated strings when
// emitting diagnostics or serialising values to text streams.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Integer-to-string helpers shared by the runtime.
/// @details Provides thin wrappers over `snprintf` that clamp truncation and
///          preserve deterministic formatting regardless of the host locale.

#include "rt_int_format.h"

#include <inttypes.h>
#include <stdio.h>

/// @brief Format a signed 64-bit integer into @p buffer.
/// @details Validates the destination buffer, writes the decimal representation
///          using the C locale, and ensures the output is null-terminated even
///          when truncated.  The return value reports the number of characters
///          written excluding the terminator.
size_t rt_i64_to_cstr(int64_t value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        return 0;
    int written = snprintf(buffer, capacity, "%" PRId64, (int64_t)value);
    if (written < 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    if ((size_t)written >= capacity)
    {
        buffer[capacity - 1] = '\0';
        return capacity - 1;
    }
    return (size_t)written;
}

/// @brief Format an unsigned 64-bit integer into @p buffer.
/// @details Mirrors @ref rt_i64_to_cstr but prints the value using the unsigned
///          conversion specifier.  The function always leaves the buffer
///          null-terminated and reports the number of characters produced.
size_t rt_u64_to_cstr(uint64_t value, char *buffer, size_t capacity)
{
    if (!buffer || capacity == 0)
        return 0;
    int written = snprintf(buffer, capacity, "%" PRIu64, (uint64_t)value);
    if (written < 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    if ((size_t)written >= capacity)
    {
        buffer[capacity - 1] = '\0';
        return capacity - 1;
    }
    return (size_t)written;
}
