//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the unified heap allocation system used by all runtime
// reference types (strings, arrays, objects). The heap API provides a common
// foundation for memory management, reference counting, and metadata tracking
// across different runtime data structures.
//
// All heap-allocated runtime objects share a common memory layout:
//   [rt_heap_hdr_t header | element payload data]
//                           ^
//                           returned pointer
//
// The header precedes the payload and contains:
// - magic: Validation marker (0x52504956 = "VIPR") for corruption detection
// - kind: Object type (string, array, object) for type-safe operations
// - elem_kind: Element type for arrays (i32, i64, f64, str, etc.)
// - refcnt: Reference count for automatic memory management
// - len: Current logical length (number of valid elements)
// - cap: Total capacity (maximum elements before reallocation)
//
// Reference Counting Architecture:
// The heap system implements automatic memory management through reference
// counting. Each heap object tracks how many references (pointers) exist to
// it. When the count drops to zero, the object is automatically freed.
//
// Reference counting operations:
// - rt_heap_retain: Increment refcount (share ownership)
// - rt_heap_release: Decrement refcount, free when zero
// - rt_heap_release_deferred: Decrement without immediate free (batch cleanup)
// - rt_heap_free_zero_ref: Explicit free of zero-refcount objects
//
// Type Safety:
// The kind and elem_kind fields enable runtime type checking. Arrays track
// their element type to ensure type-safe access and proper cleanup of nested
// references (e.g., string arrays must release each string element).
//
// Memory Management Strategy:
// The heap allocator uses malloc/free with header metadata. Future implementations
// may add pooling, generational collection, or arena allocation for performance.
// The current design prioritizes simplicity and correctness over optimization.
//
// Corruption Detection:
// The magic field provides basic heap corruption detection. Invalid magic values
// indicate memory corruption, use-after-free, or wild pointers. This catches
// common memory safety bugs during development and testing.
//
// Integration with Runtime Types:
// - Strings (rt_string): Use RT_HEAP_STRING, elem_kind = RT_ELEM_U8
// - Int arrays: Use RT_HEAP_ARRAY, elem_kind = RT_ELEM_I32/I64
// - Float arrays: Use RT_HEAP_ARRAY, elem_kind = RT_ELEM_F64
// - String arrays: Use RT_HEAP_ARRAY, elem_kind = RT_ELEM_STR (requires special cleanup)
// - Objects: Use RT_HEAP_OBJECT for OOP runtime support
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Optional callback invoked before freeing a heap payload.
/// @details Finalizers run only for RT_HEAP_OBJECT payloads when their reference count reaches
///          zero and the owning code calls the corresponding free routine (e.g., rt_obj_free).
typedef void (*rt_heap_finalizer_t)(void *payload);

typedef enum
{
    RT_HEAP_STRING = 1,
    RT_HEAP_ARRAY = 2,
    RT_HEAP_OBJECT = 3,
} rt_heap_kind_t;

typedef enum
{
    RT_ELEM_NONE = 0,
    RT_ELEM_I32 = 1,
    RT_ELEM_I64 = 2,
    RT_ELEM_F64 = 3,
    RT_ELEM_U8 = 4,
    RT_ELEM_STR = 5, // String pointer (rt_string) requiring reference counting
} rt_elem_kind_t;

typedef struct rt_heap_hdr
{
    uint32_t magic;
    uint16_t kind;
    uint16_t elem_kind;
    uint32_t flags; /* debug/status flags: bit0=disposed */
    size_t refcnt;
    size_t len;
    size_t cap;
    int64_t class_id;              ///< Optional runtime class identifier (objects only).
    rt_heap_finalizer_t finalizer; ///< Optional finalizer callback (objects only).
} rt_heap_hdr_t;

#define RT_MAGIC 0x52504956u /* 'VIPR' little-endian */

#ifdef __cplusplus
extern "C"
{
#endif

    /// What: Allocate a new heap object with header + payload.
    /// Why:  Provide a unified allocation path for all runtime reference types (strings,
    ///       arrays, objects) with consistent metadata and refcount semantics.
    /// How:  Allocates a contiguous block consisting of an rt_heap_hdr_t followed by a
    ///       payload region sized for @p init_cap elements of @p elem_size. Initializes
    ///       header fields (magic/kind/elem_kind/refcnt/len/cap) and returns a pointer to
    ///       the payload (i.e., immediately after the header).
    ///
    /// @param kind       Logical heap object kind (string/array/object).
    /// @param elem_kind  Element type tag for arrays (RT_ELEM_*); RT_ELEM_NONE for others.
    /// @param elem_size  Size in bytes of one logical element in the payload.
    /// @param init_len   Initial logical length; must be <= init_cap.
    /// @param init_cap   Initial capacity in elements; 0 permitted (no payload).
    /// @return Payload pointer on success; NULL on allocation failure or invalid params.
    ///
    /// @pre init_len <= init_cap.
    /// @post refcnt == 1; len == init_len; cap == init_cap.
    /// @errors Returns NULL when size computations overflow or malloc fails.
    /// @thread-safety Not thread-safe; callers must synchronize allocations.
    void *rt_heap_alloc(rt_heap_kind_t kind,
                        rt_elem_kind_t elem_kind,
                        size_t elem_size,
                        size_t init_len,
                        size_t init_cap);

    /// What: Increment the reference count of @p payload.
    /// Why:  Share ownership of a heap object safely across callers.
    /// How:  Increments header refcnt when @p payload != NULL.
    ///
    /// @param payload Payload pointer previously returned by rt_heap_alloc (may be NULL).
    /// @post refcnt is increased by 1 when payload != NULL.
    /// @thread-safety Not atomic; external synchronization required for shared objects.
    void rt_heap_retain(void *payload);

    /// What: Decrement the reference count of @p payload, freeing on zero.
    /// Why:  Release ownership and perform automatic cleanup when no references remain.
    /// How:  Decrements header refcnt and frees header+payload when it reaches zero.
    ///
    /// @param payload Payload pointer or NULL (NULL is ignored).
    /// @return New reference count after decrement (0 when freed). Undefined when payload is NULL.
    /// @errors Behavior is undefined if refcnt underflows (double-free); debug builds may assert.
    /// @thread-safety Not atomic; external synchronization required for shared objects.
    size_t rt_heap_release(void *payload);

    /// What: Decrement refcount without immediate free.
    /// Why:  Allow batched cleanup in contexts where immediate free is unsafe (e.g.,
    ///       re-entrant callbacks) or to avoid deep recursive frees.
    /// How:  Decrements the refcount and records the payload for deferred reclamation
    ///       by a later sweep; does not free the object even when reaching zero.
    ///
    /// @param payload Payload pointer or NULL (NULL is ignored).
    /// @return New reference count after decrement.
    /// @remarks Call rt_heap_free_zero_ref() later to reclaim memory.
    size_t rt_heap_release_deferred(void *payload);

    /// What: Free a payload whose refcount is already zero.
    /// Why:  Provide an explicit free entry point after deferred release or external handoff.
    /// How:  Validates zero refcnt (in debug) and deallocates header+payload.
    ///
    /// @param payload Payload pointer with refcnt == 0; NULL is ignored.
    /// @pre Either a prior rt_heap_release_deferred() to zero or external setting to zero.
    void rt_heap_free_zero_ref(void *payload);

    /// What: Retrieve the header from a payload pointer.
    /// Why:  Access metadata (len/cap/refcnt/kind) associated with the payload.
    /// How:  Computes header address by subtracting sizeof(rt_heap_hdr_t) from the payload.
    ///
    /// @param payload Payload pointer as returned by allocation APIs.
    /// @return Pointer to the associated rt_heap_hdr_t.
    rt_heap_hdr_t *rt_heap_hdr(void *payload);

    /// What: Retrieve the payload address from a header pointer.
    /// Why:  Convert between header/payload views when manipulating metadata.
    /// How:  Returns a pointer immediately after the header structure.
    ///
    /// @param h Header pointer from rt_heap_hdr().
    /// @return Payload pointer.
    void *rt_heap_data(rt_heap_hdr_t *h);

    /// What: Read the current logical length from the header.
    /// Why:  Share size metadata access across runtime components.
    /// How:  Returns header->len for @p payload.
    ///
    /// @param payload Payload pointer.
    /// @return Logical element count.
    size_t rt_heap_len(void *payload);

    /// What: Read the current capacity from the header.
    /// Why:  Share capacity metadata access across runtime components.
    /// How:  Returns header->cap for @p payload.
    ///
    /// @param payload Payload pointer.
    /// @return Capacity in elements.
    size_t rt_heap_cap(void *payload);

    /// What: Update the logical length stored in the header.
    /// Why:  Record changes after append/resize operations.
    /// How:  Writes header->len to @p new_len for @p payload.
    ///
    /// @param payload Payload pointer.
    /// @param new_len New logical length; must be <= current capacity.
    /// @pre 0 <= new_len <= cap.
    /// @post Subsequent rt_heap_len(payload) == new_len.
    void rt_heap_set_len(void *payload, size_t new_len);

    /**
     * @brief Mark an object payload as disposed (debug aid).
     * @details Sets a header bit to guard against double-dispose bugs in higher-level
     *          object lifecycles. Intended for assertions/diagnostics; does not change
     *          the refcount. No-op for NULL payloads.
     * @param payload Object payload pointer (may be NULL).
     * @return 0 when marking for the first time; 1 when already marked disposed.
     * @thread-safety Not synchronized.
     */
    int32_t rt_heap_mark_disposed(void *payload);

#ifdef __cplusplus
}
#endif
