//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_object.c
/// @brief Object allocation and lifetime management for Viper.
///
/// This file implements the core object allocation and reference counting
/// system for Viper's runtime. All heap-allocated objects (class instances,
/// collections, etc.) use these functions for memory management.
///
/// **What is an Object?**
/// In Viper's runtime, an "object" is any heap-allocated value that:
/// - Has a header with reference count and type metadata
/// - Participates in automatic memory management
/// - May have a vtable pointer for polymorphism (class instances)
///
/// **Memory Layout:**
/// ```
/// ┌─────────────────────────────────────────────────────────────────────────┐
/// │                         Heap Allocation                                 │
/// │                                                                         │
/// │  ┌──────────────────────┐  ┌────────────────────────────────────────┐   │
/// │  │   rt_heap_hdr_t      │  │         Payload (user data)            │   │
/// │  │ ┌──────────────────┐ │  │ ┌────────────────────────────────────┐ │   │
/// │  │ │ refcnt: 1        │ │  │ │ vptr: *vtable (class instances)   │ │   │
/// │  │ │ kind: OBJECT     │ │  │ │ field1: ...                       │ │   │
/// │  │ │ class_id: 42     │ │  │ │ field2: ...                       │ │   │
/// │  │ │ finalizer: fn    │ │  │ │ ...                               │ │   │
/// │  │ └──────────────────┘ │  │ └────────────────────────────────────┘ │   │
/// │  └──────────────────────┘  └────────────────────────────────────────┘   │
/// │           ▲                              ▲                              │
/// │           │                              │                              │
/// │      Hidden from user             Returned to user                      │
/// └─────────────────────────────────────────────────────────────────────────┘
/// ```
///
/// **Reference Counting:**
/// Objects use reference counting for automatic memory management:
/// ```
/// Dim obj = New MyClass()   ' refcnt = 1
/// Dim other = obj           ' refcnt = 2 (retain)
/// other = Nothing           ' refcnt = 1 (release)
/// obj = Nothing             ' refcnt = 0 → finalizer runs, memory freed
/// ```
///
/// **Object Lifecycle:**
/// ```
///   rt_obj_new_i64()
///        │
///        ▼
///   ┌─────────┐     rt_obj_retain_maybe()     ┌─────────┐
///   │ refcnt  │ ◀────────────────────────────▶│ refcnt  │
///   │   = 1   │     rt_obj_release_check0()   │   > 1   │
///   └────┬────┘                               └─────────┘
///        │
///        │ rt_obj_release_check0() when refcnt → 0
///        ▼
///   ┌─────────┐
///   │ refcnt  │ ─── rt_obj_free() ───▶ [freed]
///   │   = 0   │     (runs finalizer)
///   └─────────┘
/// ```
///
/// **Finalizers:**
/// Objects can have custom finalizers that run when the reference count
/// reaches zero. This enables cleanup of external resources:
/// ```
/// rt_obj_set_finalizer(obj, my_cleanup_fn);
/// ```
///
/// **System.Object Methods:**
/// This file also implements the default behavior for System.Object methods:
/// | Method            | Implementation                                    |
/// |-------------------|---------------------------------------------------|
/// | ReferenceEquals   | Pointer comparison                                |
/// | Equals            | Default: pointer comparison (can be overridden)   |
/// | GetHashCode       | Default: pointer value as hash                    |
/// | ToString          | Default: class qualified name from type registry  |
///
/// **Thread Safety:**
/// Reference counting uses atomic operations, making retain/release safe
/// to call from multiple threads. However, the object's fields are not
/// automatically synchronized.
///
/// @see rt_heap.c For the underlying memory allocator
/// @see rt_type_registry.c For class metadata and vtables
/// @see rt_oop.h For OOP type definitions
///
//===----------------------------------------------------------------------===//

#include "rt_object.h"
#include "rt_box.h"
#include "rt_heap.h"
#include "rt_oop.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern void rt_trap(const char *msg);

/// @brief Allocate a zeroed payload tagged as a heap object.
/// @details Requests storage from @ref rt_heap_alloc with the
///          @ref RT_HEAP_OBJECT tag so that reference counting and deallocation
///          semantics match other heap-managed entities.  The helper preserves
///          the caller-supplied payload size without introducing additional
///          metadata.
/// @param bytes Number of payload bytes to allocate.
/// @return Pointer to the freshly zeroed payload, or @c NULL when the heap
///         cannot satisfy the request.
static inline void *alloc_payload(size_t bytes)
{
    size_t len = bytes;
    size_t cap = bytes;
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, len, cap);
}

/// @brief Allocate a new object payload with runtime heap bookkeeping.
/// @details Ignores the BASIC class identifier (reserved for future use) and
///          delegates to @ref alloc_payload so the resulting pointer participates
///          in the shared retain/release discipline.
/// @param class_id Runtime class identifier supplied by the caller (unused).
/// @param byte_size Requested payload size in bytes.
/// @return Pointer to zeroed storage when successful; otherwise @c NULL.
void *rt_obj_new_i64(int64_t class_id, int64_t byte_size)
{
    void *payload = alloc_payload((size_t)byte_size);
    if (!payload)
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "rt_obj_new_i64: allocation failed (class_id=%lld, size=%lld bytes)",
                 (long long)class_id, (long long)byte_size);
        rt_trap(buf);
        return NULL; /* unreachable — rt_trap terminates */
    }
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    if (hdr)
        hdr->class_id = class_id;
    return payload;
}

/// @brief Get the class ID of an object.
///
/// Retrieves the runtime class identifier that was set during object creation.
/// Used for virtual dispatch and runtime type identification (RTTI).
///
/// @param p Object payload pointer (may be NULL).
/// @return The class ID, or 0 if p is NULL or not a valid object.
int64_t rt_obj_class_id(void *p)
{
    if (!p)
        return 0;
    rt_heap_hdr_t *hdr = rt_heap_hdr(p);
    return hdr ? hdr->class_id : 0;
}

/// @brief Set a custom finalizer for an object.
///
/// Registers a callback function to be invoked when the object's reference
/// count reaches zero. This allows objects to clean up external resources
/// (file handles, network connections, native memory, etc.) before the
/// object memory is freed.
///
/// **Usage example:**
/// ```c
/// void my_cleanup(void *obj) {
///     MyResource *r = (MyResource *)obj;
///     close_handle(r->handle);
/// }
///
/// void *obj = rt_obj_new_i64(0, sizeof(MyResource));
/// rt_obj_set_finalizer(obj, my_cleanup);
/// ```
///
/// @param p Object to attach finalizer to (may be NULL).
/// @param fn Finalizer function, or NULL to clear any existing finalizer.
///
/// @note Ignored for NULL pointers or non-object heap allocations.
/// @note Finalizer runs before the object memory is freed.
/// @note Only one finalizer per object; setting replaces any previous.
void rt_obj_set_finalizer(void *p, rt_obj_finalizer_t fn)
{
    if (!p)
        return;
    rt_heap_hdr_t *hdr = rt_heap_hdr(p);
    if (!hdr)
        return;
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT)
        return;
    hdr->finalizer = (rt_heap_finalizer_t)fn;
}

/// @brief Increment the reference count for a runtime-managed object.
/// @details Defensively ignores null pointers so callers can unconditionally
///          forward potential object references.  Non-null payloads delegate to
///          @ref rt_heap_retain, keeping the retain logic centralised in the
///          heap subsystem.
/// @param p Object payload pointer returned by @ref rt_obj_new_i64 or another
///          heap-managed API.
void rt_obj_retain_maybe(void *p)
{
    if (!p)
        return;
    if (rt_string_is_handle(p))
    {
        rt_str_retain_maybe((rt_string)p);
        return;
    }
    rt_heap_retain(p);
}

/// @brief Decrement the reference count and report last-user semantics.
/// @details Mirrors BASIC string behaviour by returning non-zero when the
///          underlying retain count reaches zero.  Null payloads are ignored to
///          simplify call sites that blindly forward optional objects.
/// @param p Object payload pointer managed by the runtime heap.
/// @return Non-zero when the retain count dropped to zero; otherwise zero.
int32_t rt_obj_release_check0(void *p)
{
    if (!p)
        return 0;
    if (rt_string_is_handle(p))
    {
        rt_str_release_maybe((rt_string)p);
        return 0;
    }
    return (int32_t)(rt_heap_release_deferred(p) == 0);
}

/// @brief Compatibility shim matching the string free entry point.
/// @details Releases storage for objects whose reference count already dropped
///          to zero.  The runtime heap performs the actual deallocation once
///          @ref rt_heap_free_zero_ref observes the zero count, mirroring the
///          BASIC string API while keeping the payload valid for user-defined
///          destructors until this helper runs.
/// @param p Object payload pointer; ignored when @c NULL.
void rt_obj_free(void *p)
{
    if (!p)
        return;
    if (rt_string_is_handle(p))
    {
        rt_str_release_maybe((rt_string)p);
        return;
    }
    rt_heap_hdr_t *hdr = rt_heap_hdr(p);
    if (hdr && (rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT &&
        __atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED) == 0 && hdr->finalizer)
    {
        rt_heap_finalizer_t fin = hdr->finalizer;
        hdr->finalizer = NULL;
        fin(p);
    }
    rt_heap_free_zero_ref(p);
}

// ============================================================================
// System.Object Method Implementations
// ============================================================================

/// @brief Check if two object references point to the same instance.
///
/// Implements the System.Object.ReferenceEquals static method. This always
/// performs pointer comparison, ignoring any overridden Equals method.
///
/// **Usage example:**
/// ```vb
/// If Object.ReferenceEquals(a, b) Then
///     Print "a and b are the same instance"
/// End If
/// ```
///
/// @param a First object reference (may be NULL).
/// @param b Second object reference (may be NULL).
///
/// @return 1 if a and b point to the same memory address, 0 otherwise.
///
/// @note NULL == NULL returns 1.
int64_t rt_obj_reference_equals(void *a, void *b)
{
    return a == b ? 1 : 0;
}

/// @brief Default implementation of Object.Equals.
///
/// Returns true if two references point to the same instance. This default
/// behavior can be overridden by derived classes to implement value equality.
///
/// **Usage example:**
/// ```vb
/// If obj1.Equals(obj2) Then
///     Print "Objects are equal"
/// End If
/// ```
///
/// @param self The object to compare from.
/// @param other The object to compare against.
///
/// @return 1 if equal, 0 if not equal.
///
/// @note Default implementation is reference equality.
/// @note Derived classes may override to provide value-based equality.
int64_t rt_obj_equals(void *self, void *other)
{
    return self == other ? 1 : 0;
}

/// @brief Default implementation of Object.GetHashCode.
///
/// Returns a hash code derived from the object's memory address. This provides
/// a stable hash for the object's lifetime but is not suitable for value-based
/// hashing.
///
/// **Usage example:**
/// ```vb
/// Dim hash = obj.GetHashCode()
/// ```
///
/// @param self The object to hash.
///
/// @return 64-bit hash code based on the object's address.
///
/// @note Derived classes should override if overriding Equals.
/// @note Two equal objects (by Equals) must return the same hash code.
int64_t rt_obj_get_hash_code(void *self)
{
    uintptr_t v = (uintptr_t)self;
    return (int64_t)v;
}

/// @brief Default implementation of Object.ToString.
///
/// Returns the class's qualified name as a string. For example, a Dog class
/// would return "Dog", and a class in a namespace would return "MyApp.Dog".
///
/// **Usage example:**
/// ```vb
/// Dim str = obj.ToString()
/// Print str  ' Prints the class name
/// ```
///
/// @param self The object to convert to string (may be NULL).
///
/// @return A new string containing the class name, or "<null>" if self is NULL.
///
/// @note Returns "Object" if type metadata is unavailable.
/// @note Does not include memory address for deterministic test output.
rt_string rt_obj_to_string(void *self)
{
    if (!self)
        return rt_string_from_bytes("<null>", 6);

    // Check if the pointer is a string handle (rt_string passed as obj).
    if (rt_string_is_handle(self))
        return (rt_string)self;

    // Check if the object is a boxed value and auto-unbox for display.
    rt_heap_hdr_t *hdr = (rt_heap_hdr_t *)((uint8_t *)self - sizeof(rt_heap_hdr_t));
    if (hdr->magic == RT_MAGIC && hdr->elem_kind == RT_ELEM_BOX)
    {
        int64_t tag = rt_box_type(self);
        if (tag == RT_BOX_STR)
        {
            return rt_unbox_str(self);
        }
        if (tag == RT_BOX_I64)
        {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%lld", (long long)rt_unbox_i64(self));
            return rt_string_from_bytes(buf, (size_t)n);
        }
        if (tag == RT_BOX_F64)
        {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%.15g", rt_unbox_f64(self));
            return rt_string_from_bytes(buf, (size_t)n);
        }
        if (tag == RT_BOX_I1)
        {
            int64_t v = rt_unbox_i1(self);
            return v ? rt_string_from_bytes("True", 4) : rt_string_from_bytes("False", 5);
        }
    }

    rt_object *obj = (rt_object *)self;
    const rt_class_info *ci = rt_get_class_info_from_vptr(obj->vptr);
    if (!ci || !ci->qname)
        return rt_string_from_bytes("Object", 6);
    const char *name = ci->qname;
    size_t len = 0;
    while (name[len] != '\0')
        ++len;
    return rt_string_from_bytes(name, len);
}

// ============================================================================
// Object Introspection
// ============================================================================

/// @brief Get the qualified type name of an object.
/// @param self Object to query (may be NULL).
/// @return A string containing the class qualified name, or "<null>".
rt_string rt_obj_type_name(void *self)
{
    if (!self)
        return rt_string_from_bytes("<null>", 6);
    rt_object *obj = (rt_object *)self;
    const rt_class_info *ci = rt_get_class_info_from_vptr(obj->vptr);
    if (!ci || !ci->qname)
        return rt_string_from_bytes("Object", 6);
    const char *name = ci->qname;
    size_t len = 0;
    while (name[len] != '\0')
        ++len;
    return rt_string_from_bytes(name, len);
}

/// @brief Get the numeric type ID of an object.
/// @param self Object to query (may be NULL).
/// @return The type ID, or 0 if self is NULL.
int64_t rt_obj_type_id(void *self)
{
    if (!self)
        return 0;
    return rt_obj_class_id(self);
}

/// @brief Check if an object reference is null.
/// @param self Object to test.
/// @return 1 if self is NULL, 0 otherwise.
int64_t rt_obj_is_null(void *self)
{
    return self == NULL ? 1 : 0;
}

// ============================================================================
// Weak Reference Support
// ============================================================================

/// @brief Store a weak reference without incrementing reference count.
///
/// Used for weak reference fields to break reference cycles. The stored
/// pointer does not keep the target object alive - if the target's reference
/// count reaches zero through other paths, it will be freed.
///
/// **Usage example:**
/// ```
/// class Node
///   weak parent: Node  ' Does not keep parent alive
///   children: List
/// end class
/// ```
///
/// @param addr Address of the field to store to.
/// @param value Object pointer to store (may be NULL).
///
/// @note The caller is responsible for ensuring the target remains valid
///       while the weak reference is in use.
/// @note Future versions may track weak references for automatic zeroing.
void rt_weak_store(void **addr, void *value)
{
    if (!addr)
        return;
    // Store without incrementing reference count
    *addr = value;
}

/// @brief Load a weak reference.
///
/// Retrieves the stored pointer value. Currently returns the raw pointer
/// without validation. Future versions may check if the target object is
/// still alive and return NULL if it has been freed.
///
/// **Usage example:**
/// ```
/// dim node as Node
/// if node.parent <> nothing then
///   ' Use parent - but be careful, it may have been freed!
/// ```
///
/// @param addr Address of the field to load from.
///
/// @return The stored pointer value, or NULL if the field is nil.
///
/// @warning The returned pointer may be dangling if the target object has
///          been freed. The caller must ensure the target is still valid
///          through other means (e.g., knowing the object lifetime).
/// @note Future versions may validate the object is still alive.
void *rt_weak_load(void **addr)
{
    if (!addr)
        return NULL;
    // For now, just return the value
    // Future: validate object still exists using zeroing weak refs
    return *addr;
}
