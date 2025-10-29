//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides a small-buffer string builder for the C runtime.  The helper
// centralises the repeated grow-append logic used by printf-style formatting and
// numeric conversions so runtime utilities can share predictable allocation and
// error handling semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Small-buffer-aware dynamic string builder for the runtime.
/// @details Offers bounded inline storage, automatic growth, and helpers for
///          appending numeric values with deterministic formatting.  All
///          functions report allocation and overflow failures to the caller so
///          runtime helpers can surface precise trap diagnostics.

#ifndef RT_STRING_BUILDER_H
#define RT_STRING_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @brief Inline storage size reserved in each builder instance.
#define RT_SB_INLINE_CAPACITY 128

    /// @brief Status codes returned by string builder operations.
    typedef enum rt_sb_status
    {
        RT_SB_OK = 0,         ///< Operation completed successfully.
        RT_SB_ERROR_ALLOC,    ///< Memory allocation failed.
        RT_SB_ERROR_OVERFLOW, ///< Size computation overflowed the platform limit.
        RT_SB_ERROR_INVALID,  ///< Caller supplied invalid arguments.
        RT_SB_ERROR_FORMAT    ///< Formatting helper reported an error.
    } rt_sb_status;

    /// @brief Small-buffer string builder used by the runtime.
    typedef struct rt_string_builder
    {
        char *data; ///< Points to the active buffer.
        size_t len; ///< Current number of bytes in use (excluding NUL).
        size_t cap; ///< Capacity of @ref data in bytes.
        char inline_buffer[RT_SB_INLINE_CAPACITY]; ///< Inline storage for the small-buffer fast
                                                   ///< path.
    } rt_string_builder;

    void rt_sb_init(rt_string_builder *sb);
    void rt_sb_free(rt_string_builder *sb);
    rt_sb_status rt_sb_reserve(rt_string_builder *sb, size_t required);
    rt_sb_status rt_sb_append_cstr(rt_string_builder *sb, const char *text);
    rt_sb_status rt_sb_append_int(rt_string_builder *sb, int64_t value);
    rt_sb_status rt_sb_append_double(rt_string_builder *sb, double value);
    rt_sb_status rt_sb_printf(rt_string_builder *sb, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // RT_STRING_BUILDER_H
