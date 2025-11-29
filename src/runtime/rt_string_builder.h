//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include "rt_string.h"
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

    /// What: Initialize a string builder with inline small-buffer.
    /// Why:  Prepare the builder for append operations without immediate heap allocation.
    /// How:  Points data to @ref inline_buffer, sets cap to RT_SB_INLINE_CAPACITY, and zeros len.
    ///
    /// @param sb Builder to initialize (must be non-null).
    /// @post sb->data references inline_buffer, sb->len == 0, sb->cap == RT_SB_INLINE_CAPACITY.
    /// @complexity O(1).
    /// @thread-safety Not thread-safe; intended for stack/local usage.
    void rt_sb_init(rt_string_builder *sb);

    /// What: Free any heap storage associated with the builder.
    /// Why:  Release memory when the builder's lifetime ends.
    /// How:  Frees non-inline @ref data, resets fields to a safe empty state.
    ///
    /// @param sb Builder to tear down (may be partially initialized).
    /// @post Builder is safe to reuse after calling rt_sb_init.
    /// @complexity O(1) excluding free().
    void rt_sb_free(rt_string_builder *sb);

    /// What: Ensure capacity for at least @p required bytes.
    /// Why:  Avoid repeated small allocations and detect overflow early.
    /// How:  Grows backing storage (typically geometrically), preserving existing content.
    ///
    /// @param sb       Builder instance.
    /// @param required Total capacity in bytes needed (>= current length).
    /// @return RT_SB_OK on success; RT_SB_ERROR_ALLOC on allocation failure; RT_SB_ERROR_OVERFLOW
    ///         if size computations would overflow platform limits; RT_SB_ERROR_INVALID for bad
    ///         args.
    /// @pre required >= sb->len.
    /// @post On success, sb->cap >= required and sb->data remains valid; sb->len unchanged.
    /// @complexity Amortized O(1) per growth; O(n) for individual reallocation.
    rt_sb_status rt_sb_reserve(rt_string_builder *sb, size_t required);

    /// What: Append a NUL-terminated C string to the builder.
    /// Why:  Provide a fast path for typical text assembly.
    /// How:  Reserves space, copies strlen(text) bytes, updates length.
    ///
    /// @param sb   Builder instance.
    /// @param text C string pointer (must not alias builder's buffer unless no reallocation
    /// occurs).
    /// @return RT_SB_OK on success or an error code as per rt_sb_reserve.
    /// @post sb->len increased by strlen(text); contents appended verbatim (without extra NULs).
    /// @errors RT_SB_ERROR_INVALID if @p text is NULL.
    rt_sb_status rt_sb_append_cstr(rt_string_builder *sb, const char *text);

    /// What: Append a signed 64-bit integer.
    /// Why:  Support efficient numeric formatting without intermediate strings.
    /// How:  Formats value into a scratch buffer with deterministic representation and appends.
    ///
    /// @param sb    Builder instance.
    /// @param value Integer to append.
    /// @return RT_SB_OK on success or error from reserve/formatting path.
    rt_sb_status rt_sb_append_int(rt_string_builder *sb, int64_t value);

    /// What: Append a double-precision float.
    /// Why:  Provide predictable, runtime-wide numeric formatting.
    /// How:  Uses runtime formatting helpers to produce canonical text (locale-independent).
    ///
    /// @param sb   Builder instance.
    /// @param value Double value to append.
    /// @return RT_SB_OK on success or error from reserve/formatting path.
    rt_sb_status rt_sb_append_double(rt_string_builder *sb, double value);

    /// What: Append formatted text using a printf-style format string.
    /// Why:  Convenience for complex formatting sequences in runtime helpers.
    /// How:  Uses rt_snprintf to format into a temporary buffer, then appends atomically.
    ///
    /// @param sb  Builder instance.
    /// @param fmt printf-style format string (C locale assumed).
    /// @param ... Variadic arguments per @p fmt.
    /// @return RT_SB_OK on success; RT_SB_ERROR_FORMAT on formatting failure; other errors from
    /// reserve.
    rt_sb_status rt_sb_printf(rt_string_builder *sb, const char *fmt, ...);

    // --- Viper.Text.StringBuilder runtime bridge ---
    // These adapters implement the Viper.Text.StringBuilder object surface by
    // operating on the embedded rt_string_builder stored inside the opaque
    // object (see rt_ns_bridge.c for the layout and construction helper).

    /// What: Return builder length (characters) based on embedded state.
    /// Why:  Expose StringBuilder.Length to the runtime.
    /// How:  Reads the builder's current length field.
    ///
    /// @param sb Opaque StringBuilder object pointer (contains embedded rt_string_builder).
    /// @return Number of characters accumulated (bytes in current encoding).
    int64_t rt_text_sb_get_length(void *sb);

    /// What: Return builder capacity in bytes.
    /// Why:  Aid diagnostics and tests; exposes underlying buffer size.
    /// How:  Reads the builder's current capacity field.
    ///
    /// @param sb Opaque StringBuilder object pointer.
    /// @return Capacity in bytes.
    int64_t rt_text_sb_get_capacity(void *sb);

    /// What: Append a runtime string and return the receiver for chaining.
    /// Why:  Match the fluent API style of Viper.Text.StringBuilder.
    /// How:  Appends the string's bytes to the embedded builder and returns @p sb.
    ///
    /// @param sb Opaque StringBuilder object pointer.
    /// @param s  Runtime string to append (NULL treated as empty).
    /// @return The same @p sb pointer for fluent chaining.
    void *rt_text_sb_append(void *sb, rt_string s);

    /// What: Materialize the builder as a runtime string.
    /// Why:  Produce an immutable snapshot of the current content.
    /// How:  Allocates an rt_string and copies the builder's bytes (zero-length yields empty
    /// string).
    ///
    /// @param sb Opaque StringBuilder object pointer.
    /// @return New runtime string with a copy of the content; never NULL (returns empty string on
    /// error).
    rt_string rt_text_sb_to_string(void *sb);

    /// What: Clear the builder contents.
    /// Why:  Reuse the same builder without reallocating.
    /// How:  Resets length to zero; retains capacity to support subsequent appends without growth.
    ///
    /// @param sb Opaque StringBuilder object pointer.
    /// @post Builder length becomes zero; capacity unchanged.
    void rt_text_sb_clear(void *sb);

#ifdef __cplusplus
}
#endif

#endif // RT_STRING_BUILDER_H
