//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_builder.h
// Purpose: Small-buffer-aware dynamic string builder for the C runtime, avoiding heap allocation
// for strings up to 128 bytes while growing automatically for longer output.
//
// Key invariants:
//   - Inline buffer (128 bytes) avoids allocation for short strings.
//   - len < cap invariant holds at all times; the NUL terminator is excluded from len.
//   - All operations report errors via rt_sb_status; callers must check before using the result.
//   - rt_sb_finish transfers the built string to an rt_string; the builder must then be freed.
//
// Ownership/Lifetime:
//   - Builder owns its backing buffer (inline or heap-allocated).
//   - Callers must call rt_sb_free after rt_sb_finish or on error to release heap memory.
//   - Stack allocation of rt_string_builder is safe for local use.
//
// Links: src/runtime/core/rt_string_builder.c (implementation), src/runtime/core/rt_string.h,
// src/runtime/core/rt_printf_compat.h
//
//===----------------------------------------------------------------------===//
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
    /// @details Embeds a fixed-size inline buffer to avoid heap allocation for
    ///          short strings. When content exceeds the inline capacity, the
    ///          builder transparently switches to a heap-allocated buffer with
    ///          geometric growth.
    typedef struct rt_string_builder
    {
        char *data; ///< Points to the active buffer (inline or heap-allocated).
        size_t len; ///< Current number of bytes in use (excluding NUL).
        size_t cap; ///< Capacity of @ref data in bytes.
        char inline_buffer[RT_SB_INLINE_CAPACITY]; ///< Inline storage for the small-buffer fast
                                                   ///< path.
    } rt_string_builder;

    /// @brief Initialize a string builder with inline small-buffer storage.
    /// @details Prepares the builder for append operations without immediate heap
    ///          allocation. Points data to the inline_buffer, sets capacity to
    ///          RT_SB_INLINE_CAPACITY, and zeros the length.
    /// @param sb Builder to initialize (must be non-null).
    /// @post sb->data references inline_buffer, sb->len == 0, sb->cap == RT_SB_INLINE_CAPACITY.
    void rt_sb_init(rt_string_builder *sb);

    /// @brief Free any heap storage associated with the builder.
    /// @details Releases the heap buffer (if the builder spilled from inline storage)
    ///          and resets fields to a safe empty state. The builder is safe to reuse
    ///          after calling rt_sb_init again.
    /// @param sb Builder to tear down (may be partially initialized).
    void rt_sb_free(rt_string_builder *sb);

    /// @brief Ensure capacity for at least @p required bytes.
    /// @details Grows backing storage (typically geometrically) when the current
    ///          capacity is insufficient, preserving existing content. Detects
    ///          overflow early to avoid silent corruption.
    /// @param sb       Builder instance.
    /// @param required Total capacity in bytes needed (>= current length).
    /// @return RT_SB_OK on success; RT_SB_ERROR_ALLOC on allocation failure;
    ///         RT_SB_ERROR_OVERFLOW if size computations overflow; RT_SB_ERROR_INVALID
    ///         for bad arguments.
    /// @pre required >= sb->len.
    /// @post On success, sb->cap >= required and sb->data remains valid.
    rt_sb_status rt_sb_reserve(rt_string_builder *sb, size_t required);

    /// @brief Append a NUL-terminated C string to the builder.
    /// @details Reserves space, copies strlen(text) bytes, and updates the length.
    ///          Provides a fast path for typical text assembly.
    /// @param sb   Builder instance.
    /// @param text C string pointer (must not alias builder's buffer unless no
    ///             reallocation occurs).
    /// @return RT_SB_OK on success; RT_SB_ERROR_INVALID if text is NULL; other
    ///         errors as per rt_sb_reserve.
    /// @post sb->len increased by strlen(text); contents appended verbatim.
    rt_sb_status rt_sb_append_cstr(rt_string_builder *sb, const char *text);

    /// @brief Append a fixed-length byte sequence to the builder.
    /// @details Reserves space, copies @p len bytes, and updates the length.
    ///          Useful when the string length is already known (avoids strlen).
    /// @param sb   Builder instance.
    /// @param text Pointer to bytes (may be NULL if len == 0).
    /// @param len  Number of bytes to copy.
    /// @return RT_SB_OK on success; RT_SB_ERROR_INVALID if text is NULL and len > 0;
    ///         other errors as per rt_sb_reserve.
    rt_sb_status rt_sb_append_bytes(rt_string_builder *sb, const char *text, size_t len);

    /// @brief Append a signed 64-bit integer as decimal text.
    /// @details Formats the value into a scratch buffer with deterministic
    ///          representation and appends it. Supports efficient numeric
    ///          formatting without intermediate string allocation.
    /// @param sb    Builder instance.
    /// @param value Integer to append.
    /// @return RT_SB_OK on success; error from reserve/formatting path on failure.
    rt_sb_status rt_sb_append_int(rt_string_builder *sb, int64_t value);

    /// @brief Append a double-precision float as decimal text.
    /// @details Uses runtime formatting helpers to produce canonical, locale-independent
    ///          text representation. Provides predictable, runtime-wide numeric formatting.
    /// @param sb    Builder instance.
    /// @param value Double value to append.
    /// @return RT_SB_OK on success; error from reserve/formatting path on failure.
    rt_sb_status rt_sb_append_double(rt_string_builder *sb, double value);

    /// @brief Append formatted text using a printf-style format string.
    /// @details Uses rt_snprintf to format into a temporary buffer, then appends
    ///          the result atomically. Convenience for complex formatting sequences.
    /// @param sb  Builder instance.
    /// @param fmt printf-style format string (C locale assumed).
    /// @param ... Variadic arguments per @p fmt.
    /// @return RT_SB_OK on success; RT_SB_ERROR_FORMAT on formatting failure;
    ///         other errors from reserve.
    rt_sb_status rt_sb_printf(rt_string_builder *sb, const char *fmt, ...);

    // --- Viper.Text.StringBuilder runtime bridge ---
    // These adapters implement the Viper.Text.StringBuilder object surface by
    // operating on the embedded rt_string_builder stored inside the opaque
    // object (see rt_ns_bridge.c for the layout and construction helper).

    /// @brief Return the builder length (characters) from an opaque StringBuilder object.
    /// @details Exposes StringBuilder.Length to the runtime by reading the embedded
    ///          builder's current length field.
    /// @param sb Opaque StringBuilder object pointer (contains embedded rt_string_builder).
    /// @return Number of characters accumulated (bytes in current encoding).
    int64_t rt_text_sb_get_length(void *sb);

    /// @brief Return the builder capacity in bytes from an opaque StringBuilder object.
    /// @details Aids diagnostics and tests by exposing the underlying buffer size.
    /// @param sb Opaque StringBuilder object pointer.
    /// @return Capacity in bytes.
    int64_t rt_text_sb_get_capacity(void *sb);

    /// @brief Append a runtime string and return the receiver for chaining.
    /// @details Matches the fluent API style of Viper.Text.StringBuilder. Appends
    ///          the string's bytes to the embedded builder and returns the same
    ///          object pointer.
    /// @param sb Opaque StringBuilder object pointer.
    /// @param s  Runtime string to append (NULL treated as empty).
    /// @return The same @p sb pointer for fluent chaining.
    void *rt_text_sb_append(void *sb, rt_string s);

    /// @brief Append a runtime string followed by a newline character.
    /// @details Treats a NULL @p s as empty, but still appends the newline. Uses
    ///          a single LF byte regardless of platform (no CRLF translation).
    /// @param sb Opaque StringBuilder object pointer.
    /// @param s  Runtime string to append (NULL treated as empty).
    /// @return The same @p sb pointer for fluent chaining.
    void *rt_text_sb_append_line(void *sb, rt_string s);

    /// @brief Materialize the builder contents as a runtime string.
    /// @details Allocates an rt_string and copies the builder's bytes to produce
    ///          an immutable snapshot of the current content. Zero-length content
    ///          yields an empty string, never NULL.
    /// @param sb Opaque StringBuilder object pointer.
    /// @return New runtime string with a copy of the content; never NULL.
    rt_string rt_text_sb_to_string(void *sb);

    /// @brief Clear the builder contents.
    /// @details Resets the length to zero while retaining capacity. Allows reuse
    ///          of the same builder without reallocating.
    /// @param sb Opaque StringBuilder object pointer.
    /// @post Builder length becomes zero; capacity unchanged.
    void rt_text_sb_clear(void *sb);

#ifdef __cplusplus
}
#endif

#endif // RT_STRING_BUILDER_H
