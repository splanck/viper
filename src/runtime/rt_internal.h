//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares internal runtime structures and utilities shared across
// multiple runtime implementation files. These definitions provide the
// scaffolding for memory management, input buffering, and allocation hooks
// used by the higher-level runtime APIs exposed to IL programs.
//
// The runtime system must coordinate memory allocation across different
// subsystems (strings, arrays, file buffers). This file centralizes internal
// helpers that manage buffer growth, allocation hooks for testing, and shared
// data structures that don't belong in the public runtime interface.
//
// Key Components:
// - Input buffer management: rt_input_try_grow handles dynamic buffer expansion
//   for file I/O operations, detecting allocation failures and overflow conditions
// - Allocation hooks: rt_set_alloc_hook provides test infrastructure for
//   simulating allocation failures and tracking memory usage patterns
// - Internal type definitions: Structures used by implementation files but
//   not exposed to IL programs or external C code
//
// This file is part of the runtime's implementation layer and should only be
// included by runtime .c/.cpp files, never by IL-generated code or user programs.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt.hpp"
#include "rt_heap.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum rt_input_grow_result
    {
        RT_INPUT_GROW_OK = 0,
        RT_INPUT_GROW_ALLOC_FAILED = 1,
        RT_INPUT_GROW_OVERFLOW = 2
    } rt_input_grow_result;

    /// What: Attempt to grow an input buffer in-place.
    /// Why:  Expand buffers during I/O without excessive reallocations.
    /// How:  Computes a safe new capacity, reallocates, and updates pointers.
    rt_input_grow_result rt_input_try_grow(char **buf, size_t *cap);

    typedef void *(*rt_alloc_hook_fn)(int64_t bytes, void *(*next)(int64_t bytes));

    /// @brief Install or remove the allocation hook used for testing.
    /// @details When non-null, @p hook receives the requested byte count and a
    ///          pointer to the default allocator implementation.  Passing
    ///          @c NULL restores the default behaviour.
    /// @param hook Replacement hook or @c NULL to disable overrides.
    void rt_set_alloc_hook(rt_alloc_hook_fn hook);

    //=========================================================================
    // Bytes Extraction Utilities
    //=========================================================================

    /// @brief Extract raw bytes from a Bytes object into a newly allocated buffer.
    /// @details Allocates a new buffer and copies the bytes data. Caller is
    ///          responsible for freeing the returned buffer with free().
    /// @param bytes Bytes object pointer (may be NULL).
    /// @param out_len Output parameter for the length of the data.
    /// @return Newly allocated buffer containing the bytes, or NULL if empty/NULL.
    ///         Traps on allocation failure.
    uint8_t *rt_bytes_extract_raw(void *bytes, size_t *out_len);

    /// @brief Create a Bytes object from raw data.
    /// @details Allocates a new Bytes object and copies the data into it.
    /// @param data Raw data buffer (may be NULL if len is 0).
    /// @param len Length of the data in bytes.
    /// @return New Bytes object containing a copy of the data.
    void *rt_bytes_from_raw(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

//=============================================================================
// Shared Hex Encoding/Decoding Utilities
//=============================================================================

/// @brief Hexadecimal character lookup table for byte-to-hex encoding.
/// @details Maps nibble values (0-15) to lowercase hexadecimal characters.
///          Used by hex encoding functions throughout the runtime.
static const char rt_hex_chars[] = "0123456789abcdef";

/// @brief Converts a hexadecimal character to its numeric value.
/// @param c The character to convert ('0'-'9', 'a'-'f', or 'A'-'F').
/// @return The numeric value 0-15, or -1 if the character is not valid hex.
static inline int rt_hex_digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

//=============================================================================
// Array Implementation Macros
//=============================================================================
//
// These macros reduce boilerplate for array type implementations.
// Each array type (i32, i64, f64, str, obj) has similar header retrieval,
// validation, and bounds checking patterns. These macros generate type-safe
// functions while avoiding code duplication.
//

/// @brief Generate an array header retrieval function.
/// @param fn_name Name of the generated function (e.g., rt_arr_i32_hdr).
/// @param elem_type C type of array elements (e.g., int32_t).
/// @code
///   RT_ARR_DEFINE_HDR_FN(rt_arr_i32_hdr, int32_t)
/// @endcode
#define RT_ARR_DEFINE_HDR_FN(fn_name, elem_type)                                                   \
    static inline rt_heap_hdr_t *fn_name(const elem_type *payload)                                 \
    {                                                                                              \
        return payload ? rt_heap_hdr((void *)payload) : NULL;                                      \
    }

/// @brief Generate an array header assertion function (static).
/// @param fn_name Name of the generated function (e.g., rt_arr_i32_assert_header).
/// @param expected_elem_kind Expected RT_ELEM_* constant (e.g., RT_ELEM_I32).
/// @code
///   RT_ARR_DEFINE_ASSERT_HEADER_FN(rt_arr_i32_assert_header, RT_ELEM_I32)
/// @endcode
#define RT_ARR_DEFINE_ASSERT_HEADER_FN(fn_name, expected_elem_kind)                                \
    static void fn_name(rt_heap_hdr_t *hdr)                                                        \
    {                                                                                              \
        assert(hdr);                                                                               \
        assert(hdr->kind == RT_HEAP_ARRAY);                                                        \
        assert(hdr->elem_kind == (expected_elem_kind));                                            \
    }

/// @brief Generate a payload byte size calculation function (static).
/// @param fn_name Name of the generated function (e.g., rt_arr_i32_payload_bytes).
/// @param elem_type C type of array elements (e.g., int32_t).
/// @code
///   RT_ARR_DEFINE_PAYLOAD_BYTES_FN(rt_arr_i32_payload_bytes, int32_t)
/// @endcode
#define RT_ARR_DEFINE_PAYLOAD_BYTES_FN(fn_name, elem_type)                                         \
    static size_t fn_name(size_t cap)                                                              \
    {                                                                                              \
        if (cap == 0)                                                                              \
            return 0;                                                                              \
        if (cap > (SIZE_MAX - sizeof(rt_heap_hdr_t)) / sizeof(elem_type))                          \
            return 0;                                                                              \
        return cap * sizeof(elem_type);                                                            \
    }

/// @brief Generate an in-place array grow function.
/// @param fn_name Name of the generated function (e.g., rt_arr_i32_grow_in_place).
/// @param elem_type C type of array elements (e.g., int32_t).
/// @param payload_bytes_fn Name of the payload bytes function (e.g., rt_arr_i32_payload_bytes).
/// @code
///   RT_ARR_DEFINE_GROW_IN_PLACE_FN(rt_arr_i32_grow_in_place, int32_t, rt_arr_i32_payload_bytes)
/// @endcode
#define RT_ARR_DEFINE_GROW_IN_PLACE_FN(fn_name, elem_type, payload_bytes_fn)                        \
    static int fn_name(rt_heap_hdr_t **hdr_inout, elem_type **payload_inout, size_t new_len)        \
    {                                                                                               \
        rt_heap_hdr_t *hdr = *hdr_inout;                                                            \
        size_t old_len = hdr ? hdr->len : 0;                                                        \
        size_t new_cap = new_len;                                                                   \
        size_t payload_bytes = payload_bytes_fn(new_cap);                                           \
        if (new_cap > 0 && payload_bytes == 0)                                                      \
            return -1;                                                                              \
        size_t total_bytes = sizeof(rt_heap_hdr_t) + payload_bytes;                                 \
        rt_heap_hdr_t *resized = (rt_heap_hdr_t *)realloc(hdr, total_bytes);                        \
        if (!resized)                                                                               \
            return -1;                                                                              \
        elem_type *payload = (elem_type *)rt_heap_data(resized);                                    \
        if (new_len > old_len)                                                                      \
        {                                                                                           \
            size_t grow = new_len - old_len;                                                        \
            memset(payload + old_len, 0, grow * sizeof(elem_type));                                 \
        }                                                                                           \
        resized->cap = new_cap;                                                                     \
        resized->len = new_len;                                                                     \
        *hdr_inout = resized;                                                                       \
        *payload_inout = payload;                                                                   \
        return 0;                                                                                   \
    }

/// @brief Generate an array resize function with copy-on-write semantics.
/// @param fn_name Name of the generated function (e.g., rt_arr_i32_resize).
/// @param elem_type C type of array elements (e.g., int32_t).
/// @param hdr_fn Header retrieval function (e.g., rt_arr_i32_hdr).
/// @param assert_header_fn Header assertion function (e.g., rt_arr_i32_assert_header).
/// @param new_fn Allocation function (e.g., rt_arr_i32_new).
/// @param copy_fn Payload copy function (e.g., rt_arr_i32_copy_payload).
/// @param release_fn Release function (e.g., rt_arr_i32_release).
/// @param grow_fn In-place grow function (e.g., rt_arr_i32_grow_in_place).
#define RT_ARR_DEFINE_RESIZE_FN(fn_name, elem_type, hdr_fn, assert_header_fn, new_fn, copy_fn,      \
                                release_fn, grow_fn)                                                \
    int fn_name(elem_type **a_inout, size_t new_len)                                                \
    {                                                                                               \
        if (!a_inout)                                                                               \
            return -1;                                                                              \
        elem_type *arr = *a_inout;                                                                  \
        if (!arr)                                                                                   \
        {                                                                                           \
            elem_type *fresh = new_fn(new_len);                                                     \
            if (!fresh)                                                                             \
                return -1;                                                                          \
            *a_inout = fresh;                                                                       \
            return 0;                                                                               \
        }                                                                                           \
        rt_heap_hdr_t *hdr = hdr_fn(arr);                                                           \
        assert_header_fn(hdr);                                                                      \
        size_t old_len = hdr->len;                                                                  \
        size_t cap = hdr->cap;                                                                      \
        if (new_len <= cap)                                                                         \
        {                                                                                           \
            if (new_len > old_len)                                                                  \
                memset(arr + old_len, 0, (new_len - old_len) * sizeof(elem_type));                  \
            rt_heap_set_len(arr, new_len);                                                          \
            return 0;                                                                               \
        }                                                                                           \
        if (__atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED) > 1)                                    \
        {                                                                                           \
            elem_type *fresh = new_fn(new_len);                                                     \
            if (!fresh)                                                                             \
                return -1;                                                                          \
            size_t copy_len = old_len < new_len ? old_len : new_len;                                \
            copy_fn(fresh, arr, copy_len);                                                          \
            release_fn(arr);                                                                        \
            *a_inout = fresh;                                                                       \
            return 0;                                                                               \
        }                                                                                           \
        rt_heap_hdr_t *hdr_mut = hdr;                                                               \
        elem_type *payload = arr;                                                                   \
        if (grow_fn(&hdr_mut, &payload, new_len) != 0)                                              \
            return -1;                                                                              \
        *a_inout = payload;                                                                         \
        return 0;                                                                                   \
    }

//=============================================================================
// String Implementation
//=============================================================================

struct rt_string_impl
{
    uint64_t magic;
    char *data;
    rt_heap_hdr_t *heap;
    size_t literal_len;
    size_t literal_refs;
};

#define RT_STRING_MAGIC 0x5354524D41474943ULL /* "STRMAGIC" */

/// @brief Maximum string length for embedded (SSO) allocation.
/// @details Strings up to this length are allocated with their data embedded
///          immediately after the rt_string_impl struct, eliminating one heap
///          allocation. The value 32 is chosen to balance allocation savings
///          against memory overhead for the combined allocation.
#define RT_SSO_MAX_LEN 32

/// @brief Sentinel value for heap pointer indicating embedded string data.
/// @details When heap equals this value, the string data is embedded directly
///          after the rt_string_impl struct in the same allocation. The data
///          pointer points to this embedded storage.
#define RT_SSO_SENTINEL ((rt_heap_hdr_t *)(uintptr_t)0xDEADBEEFCAFEBABEULL)
