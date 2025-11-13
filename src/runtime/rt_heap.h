//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
    size_t refcnt;
    size_t len;
    size_t cap;
} rt_heap_hdr_t;

#define RT_MAGIC 0x52504956u /* 'VIPR' little-endian */

#ifdef __cplusplus
extern "C"
{
#endif

    void *rt_heap_alloc(rt_heap_kind_t kind,
                        rt_elem_kind_t elem_kind,
                        size_t elem_size,
                        size_t init_len,
                        size_t init_cap);
    void rt_heap_retain(void *payload);
    size_t rt_heap_release(void *payload);
    size_t rt_heap_release_deferred(void *payload);
    void rt_heap_free_zero_ref(void *payload);
    rt_heap_hdr_t *rt_heap_hdr(void *payload);
    void *rt_heap_data(rt_heap_hdr_t *h);
    size_t rt_heap_len(void *payload);
    size_t rt_heap_cap(void *payload);
    void rt_heap_set_len(void *payload, size_t new_len);

#ifdef __cplusplus
}
#endif
