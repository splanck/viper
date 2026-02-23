//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_int_format.h
// Purpose: Portable, locale-independent 64-bit integer to decimal string formatting, providing signed and unsigned variants that write into caller-supplied buffers.
//
// Key invariants:
//   - Always null-terminates the output buffer when capacity > 0.
//   - Returns 0 on buffer overflow (capacity too small) rather than truncating.
//   - Output is identical across platforms regardless of locale settings.
//   - Minimum buffer size for int64 is 21 bytes (20 digits + NUL).
//
// Ownership/Lifetime:
//   - Callers provide and own the destination buffer.
//   - No heap allocation occurs; functions are safe to call from interrupt context.
//
// Links: src/runtime/core/rt_int_format.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Format a signed 64-bit integer into the supplied buffer using the C locale.
    ///
    /// @param value Signed integer value to format.
    /// @param buffer Destination buffer that receives the textual representation.
    /// @param capacity Size of @p buffer in bytes, including space for the null terminator.
    /// @return Number of characters written excluding the null terminator; zero on failure.
    size_t rt_i64_to_cstr(int64_t value, char *buffer, size_t capacity);

    /// @brief Format an unsigned 64-bit integer into the supplied buffer using the C locale.
    ///
    /// @param value Unsigned integer value to format.
    /// @param buffer Destination buffer that receives the textual representation.
    /// @param capacity Size of @p buffer in bytes, including space for the null terminator.
    /// @return Number of characters written excluding the null terminator; zero on failure.
    size_t rt_u64_to_cstr(uint64_t value, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
