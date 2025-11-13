//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares portable, locale-independent integer formatting utilities
// for the Viper runtime. These functions convert 64-bit integers to decimal string
// representation with consistent output across all platforms and locales.
//
// The standard C library's sprintf and snprintf functions have locale-dependent
// formatting behavior and varying buffer safety guarantees across implementations.
// Viper requires deterministic, reproducible output for test validation and
// cross-platform compatibility. These helpers provide explicit buffer management
// with guaranteed null termination and no locale dependencies.
//
// Key Design Features:
// - Locale independence: Always uses C locale decimal formatting (no thousands separators)
// - Explicit buffer management: Callers provide pre-allocated buffers with size
// - Safe termination: Always null-terminates output or returns zero on buffer overflow
// - Return value clarity: Returns character count excluding null terminator, not buffer position
//
// These formatters are used by PRINT statement lowering for integer output and
// by runtime diagnostic messages. They provide a stable foundation for text
// conversion without dependencies on platform-specific printf implementations.
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
