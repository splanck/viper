//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_string_builder.c
// Purpose: Provide the growable string buffer used by the BASIC runtime's
//          formatting helpers.
// Key invariants: Builders always keep their buffers null-terminated, respect
//                 a fixed in-struct inline capacity before allocating on the
//                 heap, and surface allocation/overflow failures via explicit
//                 status codes instead of trapping.
// Ownership/Lifetime: Callers own the builder object and are responsible for
//                     eventually releasing any heap storage via rt_sb_free.
// Links: docs/runtime/strings.md#string-builder
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implementation of the runtime string builder helper.
/// @details Provides reserve and append helpers that keep the buffer
///          null-terminated and surface allocation/overflow failures back to
///          callers instead of trapping directly.

#include "rt_string_builder.h"

#include "rt_format.h"
#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_ns_bridge.h"
#include "rt_object.h"
#include "rt_string.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Determine whether the builder currently points at its inline buffer.
/// @details Builders start life with @ref rt_string_builder::data referencing
///          @ref rt_string_builder::inline_buffer.  When the buffer grows beyond
///          the inline capacity the data pointer is redirected to heap storage.
///          This helper allows other routines to branch on that condition
///          without duplicating pointer comparisons.
/// @param sb Builder to inspect; may be @c NULL.
/// @return Non-zero when the builder uses its inline buffer; zero otherwise.
static bool rt_sb_is_inline(const rt_string_builder *sb)
{
    return sb && sb->data == sb->inline_buffer;
}

/// @brief Restore a builder to a prior state after formatting overflow.
/// @details When formatting functions attempt to append into a buffer that was
///          subsequently found to be too small, callers use this helper to
///          release any temporary heap allocation, restore the caller-provided
///          length/capacity snapshot, and ensure the buffer remains
///          null-terminated.  No work is performed when @p sb is @c NULL.
/// @param sb Builder being restored.
/// @param original_len Length of the string before the attempted append.
/// @param original_cap Capacity (in bytes) before the attempted append.
/// @param was_inline Whether the builder resided in its inline buffer prior to
///        the append attempt.
static void rt_sb_restore_on_overflow(rt_string_builder *sb,
                                      size_t original_len,
                                      size_t original_cap,
                                      bool was_inline)
{
    if (!sb)
        return;

    if (was_inline)
    {
        if (!rt_sb_is_inline(sb) && sb->data)
            free(sb->data);
        sb->data = sb->inline_buffer;
    }

    sb->len = original_len;
    sb->cap = original_cap;
    sb->data[sb->len] = '\0';
}

/// @brief Initialise a builder so it starts with the inline small buffer.
/// @details Resets length and capacity bookkeeping, points @c data at the inline
///          storage, and seeds the buffer with a null terminator.  Passing a
///          null pointer is tolerated as a no-op so callers can unconditionally
///          initialise arrays of builders.
/// @param sb Builder instance to prepare.
void rt_sb_init(rt_string_builder *sb)
{
    if (!sb)
        return;
    sb->data = sb->inline_buffer;
    sb->len = 0;
    sb->cap = RT_SB_INLINE_CAPACITY;
    sb->inline_buffer[0] = '\0';
}

/// @brief Release any heap storage owned by the builder and reset it to empty.
/// @details Frees the heap buffer when the builder outgrew the inline storage,
///          resets bookkeeping fields, and reinstates the inline buffer as the
///          active storage.  After the call the builder is indistinguishable from
///          a freshly initialised instance.
/// @param sb Builder instance to tear down; null pointers are ignored.
void rt_sb_free(rt_string_builder *sb)
{
    if (!sb)
        return;
    if (!rt_sb_is_inline(sb) && sb->data)
        free(sb->data);
    sb->data = sb->inline_buffer;
    sb->len = 0;
    sb->cap = RT_SB_INLINE_CAPACITY;
    sb->inline_buffer[0] = '\0';
}

/// @brief Grow the builder capacity to at least @p new_cap bytes.
/// @details Allocates or reallocates storage when the requested capacity exceeds
///          the current capacity.  Inline buffers transition to heap storage via
///          @c malloc, while existing heap buffers are resized with
///          @c realloc.  The method preserves the string contents and trailing
///          null terminator on success.
/// @param sb Builder to grow.
/// @param new_cap Desired capacity including space for the terminator.
/// @return Status code describing the outcome of the operation.
static rt_sb_status rt_sb_grow(rt_string_builder *sb, size_t new_cap)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;
    if (new_cap <= sb->cap)
        return RT_SB_OK;

    if (new_cap < sb->cap)
        return RT_SB_ERROR_OVERFLOW;

    char *new_data = NULL;
    if (rt_sb_is_inline(sb))
    {
        new_data = (char *)malloc(new_cap);
        if (!new_data)
            return RT_SB_ERROR_ALLOC;
        memcpy(new_data, sb->inline_buffer, sb->len + 1);
    }
    else
    {
        new_data = (char *)realloc(sb->data, new_cap);
        if (!new_data)
            return RT_SB_ERROR_ALLOC;
    }

    sb->data = new_data;
    sb->cap = new_cap;
    return RT_SB_OK;
}

/// @brief Ensure the builder can store @p required bytes including the terminator.
/// @details Rounds the requested capacity up to the next power-of-two style
///          growth factor, respecting the inline capacity first, before
///          delegating to @ref rt_sb_grow.  The helper avoids integer overflow by
///          clamping the capacity increase once @c SIZE_MAX would be exceeded.
/// @param sb Builder whose storage should be grown on demand.
/// @param required Minimum number of bytes required (excluding the implicit
///        null terminator).
/// @return Status describing success, allocation failure, or overflow.
rt_sb_status rt_sb_reserve(rt_string_builder *sb, size_t required)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;
    if (required <= sb->len + 1)
        required = sb->len + 1;
    if (required <= sb->cap)
        return RT_SB_OK;

    size_t new_cap = sb->cap;
    if (new_cap == 0)
        new_cap = RT_SB_INLINE_CAPACITY;

    while (new_cap < required)
    {
        if (new_cap > SIZE_MAX / 2)
        {
            new_cap = required;
            break;
        }
        new_cap *= 2;
    }

    if (new_cap < required)
        new_cap = required;

    return rt_sb_grow(sb, new_cap);
}

/// @brief Append raw bytes to the builder without performing formatting.
/// @details Validates arguments, expands the buffer to fit @p len additional
///          bytes plus the null terminator, copies the data, and updates the
///          length bookkeeping.  Overflow checks ensure callers never observe
///          wrap-around behaviour even when appending enormous strings.
/// @param sb Builder receiving the bytes.
/// @param text Pointer to the bytes to append; may be null when @p len is zero.
/// @param len Number of bytes to copy from @p text.
/// @return Status code describing success, allocation failure, or invalid input.
static rt_sb_status rt_sb_append_bytes(rt_string_builder *sb, const char *text, size_t len)
{
    if (!sb || (!text && len > 0))
        return RT_SB_ERROR_INVALID;
    if (len == 0)
        return RT_SB_OK;

    if (len > SIZE_MAX - sb->len - 1)
        return RT_SB_ERROR_OVERFLOW;

    rt_sb_status status = rt_sb_reserve(sb, sb->len + len + 1);
    if (status != RT_SB_OK)
        return status;

    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return RT_SB_OK;
}

/// @brief Append a null-terminated C string to the builder.
/// @details Rejects null pointers and otherwise forwards to
///          @ref rt_sb_append_bytes after computing the string length with
///          @ref strlen.  The helper exists so callers do not need to manually
///          compute lengths for common cases.
/// @param sb Destination builder.
/// @param text Null-terminated UTF-8 string to append.
/// @return Status code indicating success or the error that occurred.
rt_sb_status rt_sb_append_cstr(rt_string_builder *sb, const char *text)
{
    if (!text)
        return RT_SB_ERROR_INVALID;
    return rt_sb_append_bytes(sb, text, strlen(text));
}

/// @brief Append the decimal representation of a signed 64-bit integer.
/// @details Ensures enough capacity for a typical 64-bit integer string, grows
///          the buffer if necessary, formats the integer via
///          @ref rt_i64_to_cstr, and updates the builder length.  Overflow and
///          formatting failures are reported via explicit status codes so the
///          caller can trap or recover.
/// @param sb Destination builder.
/// @param value Integer to append in base 10.
/// @return Status describing whether the append succeeded.
rt_sb_status rt_sb_append_int(rt_string_builder *sb, int64_t value)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;

    const size_t extra = 32;
    if (extra > SIZE_MAX - sb->len - 1)
        return RT_SB_ERROR_OVERFLOW;

    rt_sb_status status = rt_sb_reserve(sb, sb->len + extra);
    if (status != RT_SB_OK)
        return status;

    size_t avail = sb->cap - sb->len;
    size_t written = rt_i64_to_cstr(value, sb->data + sb->len, avail);
    if (written == 0 && sb->data[sb->len] == '\0')
        return RT_SB_ERROR_FORMAT;
    if (written + 1 > avail)
        return RT_SB_ERROR_OVERFLOW;

    sb->len += written;
    sb->data[sb->len] = '\0';
    return RT_SB_OK;
}

/// @brief Append a floating-point value formatted with BASIC semantics.
/// @details Reserves additional space, captures the builder's previous state in
///          case the formatted output exceeds the buffer, invokes
///          @ref rt_format_f64, and rolls back to the original state on overflow.
///          Successful calls leave the buffer null-terminated with the appended
///          text at the end.
/// @param sb Destination builder.
/// @param value Floating-point value to append.
/// @return Status describing whether the append succeeded.
rt_sb_status rt_sb_append_double(rt_string_builder *sb, double value)
{
    if (!sb)
        return RT_SB_ERROR_INVALID;

    const size_t extra = 64;
    if (extra > SIZE_MAX - sb->len - 1)
        return RT_SB_ERROR_OVERFLOW;

    size_t original_len = sb->len;
    size_t original_cap = sb->cap;
    bool was_inline = rt_sb_is_inline(sb);

    rt_sb_status status = rt_sb_reserve(sb, sb->len + extra);
    if (status != RT_SB_OK)
        return status;

    size_t avail = sb->cap - sb->len;
    rt_format_f64(value, sb->data + sb->len, avail);
    size_t appended = strlen(sb->data + sb->len);

    if (appended + 1 > avail)
    {
        rt_sb_restore_on_overflow(sb, original_len, original_cap, was_inline);
        return RT_SB_ERROR_OVERFLOW;
    }

    sb->len += appended;
    if (sb->len >= sb->cap)
    {
        rt_sb_restore_on_overflow(sb, original_len, original_cap, was_inline);
        return RT_SB_ERROR_OVERFLOW;
    }

    sb->data[sb->len] = '\0';
    return RT_SB_OK;
}

/// @brief Append formatted text using @c vsnprintf semantics.
/// @details Repeatedly attempts to render the formatted text into the available
///          space, expanding the builder whenever @c vsnprintf reports that the
///          buffer was insufficient.  Formatting failures or allocation issues
///          result in descriptive status codes so callers can handle the error.
/// @param sb Destination builder.
/// @param fmt printf-style format string.
/// @param args Variadic argument list to render.
/// @return Status describing whether the formatted append succeeded.
static rt_sb_status rt_sb_vprintf_internal(rt_string_builder *sb, const char *fmt, va_list args)
{
    if (!sb || !fmt)
        return RT_SB_ERROR_INVALID;

    va_list copy;
    while (1)
    {
        size_t avail = (sb->cap > sb->len) ? (sb->cap - sb->len) : 0;
        if (avail < 2)
        {
            size_t target = sb->cap ? sb->cap * 2 : RT_SB_INLINE_CAPACITY;
            if (sb->cap > SIZE_MAX / 2)
                target = SIZE_MAX;
            rt_sb_status status = rt_sb_reserve(sb, target);
            if (status != RT_SB_OK)
                return status;
            continue;
        }

        va_copy(copy, args);
        int written = vsnprintf(sb->data + sb->len, avail, fmt, copy);
        va_end(copy);

        if (written < 0)
            return RT_SB_ERROR_FORMAT;

        if ((size_t)written + 1 > avail)
        {
            size_t needed = sb->len + (size_t)written + 1;
            if (needed < sb->len)
                return RT_SB_ERROR_OVERFLOW;
            rt_sb_status status = rt_sb_reserve(sb, needed);
            if (status != RT_SB_OK)
                return status;
            continue;
        }

        sb->len += (size_t)written;
        sb->data[sb->len] = '\0';
        return RT_SB_OK;
    }
}

/// @brief Variadic convenience wrapper around @ref rt_sb_vprintf_internal.
/// @details Initializes a @c va_list, forwards it to the internal formatting
///          helper, and then tears down the variadic state before returning the
///          resulting status code.
/// @param sb Destination builder to append to.
/// @param fmt printf-style format string.
/// @param ... Variadic arguments consumed by @p fmt.
/// @return Status from the underlying formatting routine.
rt_sb_status rt_sb_printf(rt_string_builder *sb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    rt_sb_status status = rt_sb_vprintf_internal(sb, fmt, args);
    va_end(args);
    return status;
}

// --------------------
// Bridge functions for Viper.Text.StringBuilder
// These functions provide the runtime interface for the OOP StringBuilder class.
// The StringBuilder object layout has a vptr at offset 0 and an embedded
// rt_string_builder struct at offset 8 (after the vptr).

#include <assert.h>

// StringBuilder object layout (must match rt_ns_bridge.c)
typedef struct
{
    void *vptr;                // vtable pointer (8 bytes)
    rt_string_builder builder; // embedded builder state
} StringBuilder;

// Helper to validate and get the embedded builder
static rt_string_builder *get_builder(void *sb)
{
    if (!sb)
    {
        // Fail loudly on null StringBuilder
        assert(0 && "rt_text_sb_* called with null StringBuilder object");
        return NULL;
    }

    StringBuilder *obj = (StringBuilder *)sb;
    // The builder is embedded right after the vptr
    return &obj->builder;
}

int64_t rt_text_sb_get_length(void *sb)
{
    rt_string_builder *builder = get_builder(sb);
    if (!builder)
        return 0;

    // Return the current string length in bytes
    // Note: for ASCII/UTF-8, byte length = character count for ASCII chars
    return (int64_t)builder->len;
}

int64_t rt_text_sb_get_capacity(void *sb)
{
    rt_string_builder *builder = get_builder(sb);
    if (!builder)
        return 0;

    // Return the current allocated capacity in bytes
    return (int64_t)builder->cap;
}

void *rt_text_sb_append(void *sb, rt_string s)
{
    rt_string_builder *builder = get_builder(sb);
    if (!builder)
        return sb;

    // Get the string data - rt_string is a pointer to struct with data field
    const char *str_data = s ? s->data : NULL;
    size_t str_len = s ? rt_len(s) : 0;

    if (str_data && str_len > 0)
    {
        // Need to append the string data - we'll have to make a null-terminated copy
        // or add each character. Since we have the data and length, let's reserve
        // space and copy directly.

        // First, ensure we have enough space
        rt_sb_status status = rt_sb_reserve(builder, builder->len + str_len + 1);
        if (status == RT_SB_OK)
        {
            // Copy the string data directly
            memcpy(builder->data + builder->len, str_data, str_len);
            builder->len += str_len;
            builder->data[builder->len] = '\0';
        }
        else if (status != RT_SB_ERROR_ALLOC)
        {
            // For errors other than allocation failure, fail loudly
            assert(0 && "rt_text_sb_append failed with unexpected error");
        }
        // For allocation failure, silently continue (could alternatively trap)
    }

    // Return the receiver for method chaining
    return sb;
}

rt_string rt_text_sb_to_string(void *sb)
{
    rt_string_builder *builder = get_builder(sb);
    if (!builder)
        return rt_str_empty();

    // Create a string from the builder's current contents
    // The builder maintains a null-terminated buffer
    if (builder->len == 0)
        return rt_str_empty();

    // Allocate and return a new string with the builder's contents
    return rt_string_from_bytes(builder->data, builder->len);
}

void rt_text_sb_clear(void *sb)
{
    rt_string_builder *builder = get_builder(sb);
    if (!builder)
        return;

    // Reset the builder to empty state while keeping allocated memory
    builder->len = 0;
    if (builder->data)
        builder->data[0] = '\0';
}
