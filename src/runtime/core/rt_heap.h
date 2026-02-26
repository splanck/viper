//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_heap.h
// Purpose: Unified heap allocation system for all runtime reference types (strings, arrays,
// objects), providing a common header layout, reference counting, and type metadata.
//
// Key invariants:
//   - Magic field (0x52504956 = 'VIPR') validates heap objects; invalid magic indicates corruption.
//   - refcnt == 1 on fresh allocation; the allocating caller owns the initial reference.
//   - len <= cap invariant is maintained by all mutating operations.
//   - Payload pointer is exactly sizeof(rt_heap_hdr_t) bytes after the header base address.
//
// Ownership/Lifetime:
//   - Heap objects are reference-counted; the last rt_heap_release call frees the memory.
//   - rt_heap_retain increments the refcount; rt_heap_release decrements and frees at zero.
//   - rt_heap_release_deferred decrements without immediate free for batch cleanup.
//
// Links: src/runtime/core/rt_heap.c (implementation), src/runtime/core/rt_string.h,
// src/runtime/arrays/rt_array.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Optional callback invoked before freeing a heap payload.
/// @details Finalizers run only for RT_HEAP_OBJECT payloads when their reference count reaches
///          zero and the owning code calls the corresponding free routine (e.g., rt_obj_free).
typedef void (*rt_heap_finalizer_t)(void *payload);

/// @brief Heap object kind tag.
/// @details Distinguishes between the three major runtime reference types
///          for type-safe operations and proper cleanup logic.
typedef enum
{
    RT_HEAP_STRING = 1, ///< Heap-allocated string (UTF-8 payload).
    RT_HEAP_ARRAY = 2,  ///< Heap-allocated array (element payload).
    RT_HEAP_OBJECT = 3, ///< Heap-allocated OOP object.
} rt_heap_kind_t;

/// @brief Element type tag for array payloads.
/// @details Stored in the heap header's elem_kind field. Determines element
///          size, alignment, and cleanup behavior (e.g., RT_ELEM_STR requires
///          releasing each string element).
typedef enum
{
    RT_ELEM_NONE = 0, ///< No element type (used for non-array heap objects).
    RT_ELEM_I32 = 1,  ///< 32-bit signed integer elements.
    RT_ELEM_I64 = 2,  ///< 64-bit signed integer elements.
    RT_ELEM_F64 = 3,  ///< 64-bit floating-point elements.
    RT_ELEM_U8 = 4,   ///< Unsigned byte elements (used for strings).
    RT_ELEM_STR = 5,  ///< String pointer (rt_string) elements requiring reference counting.
    RT_ELEM_BOX = 6,  ///< Boxed primitive value (rt_box_t) elements with type tag.
} rt_elem_kind_t;

/// @brief Heap object header preceding every payload.
/// @details Contains metadata for validation, type safety, reference counting,
///          and capacity management. The payload immediately follows this header.
typedef struct rt_heap_hdr
{
    uint32_t magic;                ///< Validation marker (must be RT_MAGIC = 0x52504956).
    uint16_t kind;                 ///< Heap object kind (rt_heap_kind_t).
    uint16_t elem_kind;            ///< Element type tag (rt_elem_kind_t).
    uint32_t flags;                ///< Debug/status flags: bit0=disposed, bit1=pool-allocated.
    size_t refcnt;                 ///< Current reference count.
    size_t len;                    ///< Current logical length (number of valid elements).
    size_t cap;                    ///< Total capacity (maximum elements before reallocation).
    size_t alloc_size;             ///< Total allocation size in bytes (header + payload).
    int64_t class_id;              ///< Optional runtime class identifier (objects only).
    rt_heap_finalizer_t finalizer; ///< Optional finalizer callback (objects only).
} rt_heap_hdr_t;

/// @brief Flag indicating the allocation came from the pool allocator.
#define RT_HEAP_FLAG_POOLED 0x2u

/// @brief Magic number for heap object validation ('VIPR' in little-endian).
#define RT_MAGIC 0x52504956u

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Allocate a new heap object with header and payload.
    /// @details Provides a unified allocation path for all runtime reference types
    ///          (strings, arrays, objects) with consistent metadata and refcount
    ///          semantics. Allocates a contiguous block consisting of an rt_heap_hdr_t
    ///          followed by a payload region sized for @p init_cap elements of
    ///          @p elem_size. Initializes header fields (magic/kind/elem_kind/refcnt/
    ///          len/cap) and returns a pointer to the payload.
    /// @param kind       Logical heap object kind (string/array/object).
    /// @param elem_kind  Element type tag for arrays (RT_ELEM_*); RT_ELEM_NONE for others.
    /// @param elem_size  Size in bytes of one logical element in the payload.
    /// @param init_len   Initial logical length; must be <= init_cap.
    /// @param init_cap   Initial capacity in elements; 0 permitted (no payload).
    /// @return Payload pointer on success; NULL on allocation failure or invalid params.
    /// @pre init_len <= init_cap.
    /// @post refcnt == 1; len == init_len; cap == init_cap.
    void *rt_heap_alloc(rt_heap_kind_t kind,
                        rt_elem_kind_t elem_kind,
                        size_t elem_size,
                        size_t init_len,
                        size_t init_cap);

    /// @brief Increment the reference count of a heap payload.
    /// @details Shares ownership of a heap object safely across callers.
    ///          No-op when @p payload is NULL.
    /// @param payload Payload pointer previously returned by rt_heap_alloc (may be NULL).
    /// @post refcnt is increased by 1 when payload != NULL.
    void rt_heap_retain(void *payload);

    /// @brief Decrement the reference count, freeing the object when it reaches zero.
    /// @details Releases ownership of a heap object. When the reference count drops
    ///          to zero, the header and payload memory are freed.
    /// @param payload Payload pointer or NULL (NULL is ignored).
    /// @return New reference count after decrement (0 when freed).
    size_t rt_heap_release(void *payload);

    /// @brief Decrement the reference count without immediate free.
    /// @details Allows batched cleanup in contexts where immediate free is unsafe
    ///          (e.g., re-entrant callbacks) or to avoid deep recursive frees. The
    ///          payload is recorded for deferred reclamation by a later sweep.
    /// @param payload Payload pointer or NULL (NULL is ignored).
    /// @return New reference count after decrement.
    /// @remarks Call rt_heap_free_zero_ref() later to reclaim memory.
    size_t rt_heap_release_deferred(void *payload);

    /// @brief Free a payload whose reference count is already zero.
    /// @details Provides an explicit free entry point after deferred release or
    ///          external handoff. Validates zero refcnt in debug builds.
    /// @param payload Payload pointer with refcnt == 0; NULL is ignored.
    /// @pre refcnt == 0 (prior rt_heap_release_deferred or external setting).
    void rt_heap_free_zero_ref(void *payload);

    /// @brief Retrieve the header from a payload pointer.
    /// @details Computes the header address by subtracting sizeof(rt_heap_hdr_t)
    ///          from the payload pointer. Used to access metadata (len, cap,
    ///          refcnt, kind) associated with the payload.
    /// @param payload Payload pointer as returned by allocation APIs.
    /// @return Pointer to the associated rt_heap_hdr_t.
    rt_heap_hdr_t *rt_heap_hdr(void *payload);

    /// @brief Retrieve the payload address from a header pointer.
    /// @details Returns a pointer immediately after the header structure.
    ///          Converts between header and payload views when manipulating metadata.
    /// @param h Header pointer from rt_heap_hdr().
    /// @return Payload pointer.
    void *rt_heap_data(rt_heap_hdr_t *h);

    /// @brief Read the current logical length from the header.
    /// @details Returns header->len for the given payload.
    /// @param payload Payload pointer.
    /// @return Logical element count.
    size_t rt_heap_len(void *payload);

    /// @brief Read the current capacity from the header.
    /// @details Returns header->cap for the given payload.
    /// @param payload Payload pointer.
    /// @return Capacity in elements.
    size_t rt_heap_cap(void *payload);

    /// @brief Update the logical length stored in the header.
    /// @details Writes header->len to @p new_len. Used to record changes after
    ///          append or resize operations.
    /// @param payload Payload pointer.
    /// @param new_len New logical length; must be <= current capacity.
    /// @pre 0 <= new_len <= cap.
    /// @post Subsequent rt_heap_len(payload) == new_len.
    void rt_heap_set_len(void *payload, size_t new_len);

    /// @brief Mark an object payload as disposed (debug aid).
    /// @details Sets a header bit to guard against double-dispose bugs in
    ///          higher-level object lifecycles. Intended for assertions and
    ///          diagnostics; does not change the refcount. No-op for NULL payloads.
    /// @param payload Object payload pointer (may be NULL).
    /// @return 0 when marking for the first time; 1 when already marked disposed.
    int32_t rt_heap_mark_disposed(void *payload);

#ifdef __cplusplus
}
#endif
