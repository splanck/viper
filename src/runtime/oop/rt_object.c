//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_object.c
// Purpose: Implements the core object allocation and reference-counted lifetime
//          management system. All heap objects (class instances, collections,
//          etc.) are allocated through rt_object_alloc and managed by
//          rt_object_retain / rt_object_release.
//
// Key invariants:
//   - Every allocated object has a rt_heap_hdr_t header directly preceding
//     the user-visible payload pointer.
//   - refcnt starts at 1 on allocation; release to 0 invokes the finalizer.
//   - Finalizers are optional; if provided, they are called before freeing.
//   - rt_object_retain on NULL is a safe no-op; same for release.
//   - Class instances store a vtable pointer (vptr) as the first field of
//     their payload; this layout is fixed and must not change.
//
// Ownership/Lifetime:
//   - Callers receive a fresh reference (refcount=1) from rt_object_alloc.
//   - Callers must call rt_object_release when done; the GC does not
//     automatically track objects not reachable via the heap graph.
//
// Links: src/runtime/oop/rt_object.h (public API),
//        src/runtime/rt_heap.h (underlying allocator),
//        src/runtime/oop/rt_type_registry.h (class ID and vtable metadata)
//
//===----------------------------------------------------------------------===//

#include "rt_object.h"
#include "rt_box.h"
#include "rt_gc.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_array_obj.h"
#include "rt_array_str.h"
#include "rt_format.h"
#include "rt_hash_util.h"
#include "rt_msgbus.h"
#include "rt_oop.h"
#include "rt_option.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_threads.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rt_trap.h"

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

// Internal weak-handle helpers used to implement zeroing weak fields. These
// are intentionally kept out of the public runtime surface.
int8_t rt_weakref_is_handle(void *candidate);
void rt_weakref_reset(rt_weakref *ref, void *target);

/// @brief Allocate a zeroed payload tagged as a heap object.
/// @details Requests storage from @ref rt_heap_alloc with the
///          @ref RT_HEAP_OBJECT tag so that reference counting and deallocation
///          semantics match other heap-managed entities.  The helper preserves
///          the caller-supplied payload size without introducing additional
///          metadata.
/// @param bytes Number of payload bytes to allocate.
/// @return Pointer to the freshly zeroed payload, or @c NULL when the heap
///         cannot satisfy the request.
static inline void *alloc_payload(size_t bytes) {
    size_t len = bytes;
    size_t cap = bytes;
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, len, cap);
}

/// @brief Build an rt_string from a NUL-terminated C string. Inline strlen avoids pulling in
/// `<string.h>` for this single use.
static rt_string rt_obj_make_cstr(const char *text) {
    size_t len = 0;
    while (text[len] != '\0')
        ++len;
    return rt_string_from_bytes(text, len);
}

/// @brief Map a built-in runtime class id to its qualified Viper class name.
/// @details Used by `rt_obj_type_name` for objects whose class id is one of the runtime's
///          fixed-known constants (Box / ValueType / Option / MessageBus / Callback) rather
///          than a user-defined class registered in the type registry. Returns NULL for any
///          id that isn't built-in so the caller can fall back to the registry lookup.
static const char *rt_obj_builtin_class_name(int64_t class_id) {
    switch (class_id) {
        case RT_BOX_CLASS_ID:
            return "Viper.Core.Box";
        case RT_VALUE_TYPE_CLASS_ID:
            return "Viper.Core.ValueType";
        case RT_OPTION_CLASS_ID:
            return "Viper.Option";
        case RT_MSGBUS_CLASS_ID:
            return "Viper.Core.MessageBus";
        case RT_MSGBUS_CALLBACK_CLASS_ID:
            return "Viper.Core.MessageBus.Callback";
        case RT_WEAKREF_CLASS_ID:
            return "Viper.Memory.WeakRef";
        default:
            return NULL;
    }
}

/// @brief Look up an object's qualified type name from its class-info record. Falls back to
/// "Object" if class info is missing or has no qname (e.g. for raw heap blobs).
static rt_string rt_obj_type_name_from_class_info(const rt_class_info *ci) {
    if (!ci || !ci->qname)
        return rt_obj_make_cstr("Object");
    return rt_obj_make_cstr(ci->qname);
}

/// @brief Allocate a new object payload with runtime heap bookkeeping.
/// @details Ignores the BASIC class identifier (reserved for future use) and
///          delegates to @ref alloc_payload so the resulting pointer participates
///          in the shared retain/release discipline.
/// @param class_id Runtime class identifier supplied by the caller (unused).
/// @param byte_size Requested payload size in bytes.
/// @return Pointer to zeroed storage when successful; otherwise @c NULL.
void *rt_obj_new_i64(int64_t class_id, int64_t byte_size) {
    if (byte_size < 0) {
        rt_trap("rt_obj_new_i64: negative object size");
        return NULL;
    }
    if ((uint64_t)byte_size > SIZE_MAX) {
        rt_trap("rt_obj_new_i64: object size too large");
        return NULL;
    }
    void *payload = alloc_payload((size_t)byte_size);
    if (!payload) {
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "rt_obj_new_i64: allocation failed (class_id=%lld, size=%lld bytes)",
                 (long long)class_id,
                 (long long)byte_size);
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
int64_t rt_obj_class_id(void *p) {
    if (!p)
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr))
        return 0;
    return hdr ? hdr->class_id : 0;
}

/// @brief Validate a runtime-managed object handle before implementation-specific casts.
int8_t rt_obj_is_instance(void *p, int64_t class_id, size_t min_payload_bytes) {
    if (!p)
        return 0;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr) || !hdr)
        return 0;
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT)
        return 0;
    if (hdr->class_id != class_id)
        return 0;
    if (hdr->cap < min_payload_bytes)
        return 0;
    return 1;
}

/// @brief Resurrect an object from inside its finalizer to return it to a pool.
///
/// Sets the reference count from 0 to 1.  Must be called only from within a
/// finalizer installed via @ref rt_obj_set_finalizer.  After this call @ref
/// rt_heap_free_zero_ref will observe a non-zero count and skip the free,
/// keeping the allocation alive for reuse.
///
/// @param p Object payload pointer whose refcount is currently zero.
void rt_obj_resurrect(void *p) {
    if (!p)
        return;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr))
        return;
    if (!hdr)
        return;
    size_t expected = 0;
    if (!__atomic_compare_exchange_n(&hdr->refcnt,
                                     &expected,
                                     1,
                                     /*weak=*/0,
                                     __ATOMIC_RELEASE,
                                     __ATOMIC_RELAXED)) {
        rt_trap("rt_obj_resurrect: object refcount is not zero");
    }
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
void rt_obj_set_finalizer(void *p, rt_obj_finalizer_t fn) {
    if (!p)
        return;
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr))
        return;
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
void rt_obj_retain_maybe(void *p) {
    if (!p)
        return;
    if (rt_string_is_handle(p)) {
        rt_str_retain_maybe((rt_string)p);
        return;
    }
    if (!rt_heap_is_payload(p))
        return;
    rt_heap_retain(p);
}

/// @brief Retain a non-null runtime heap object without dynamic kind checks.
/// @details Native optimized code calls this only when IL analysis has proven
///          the value comes from an object allocation helper. The NULL guard
///          preserves the broad retain contract while removing string and raw
///          pointer probes from hot exact-object paths.
void rt_obj_retain_known(void *p) {
    if (!p)
        return;
    rt_heap_retain(p);
}

/// @brief Public Viper.Memory retain entry point with runtime handle validation.
void rt_memory_retain(void *p) {
    if (!p)
        return;
    if (rt_string_is_handle(p)) {
        rt_str_retain_maybe((rt_string)p);
        return;
    }
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr) || !hdr) {
        rt_trap("Viper.Memory.Retain: invalid or freed heap object");
        return;
    }
    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_STRING) {
        rt_trap("Viper.Memory.Retain: invalid string payload; pass the string handle");
        return;
    }
    rt_heap_retain(p);
}

/// @brief Public string-typed wrapper for `Viper.Memory.RetainStr`.
/// @details Validates that @p s is a runtime string handle before retaining it.
///          The typed wrapper exists so Zia and BASIC code can call it without a `void *`
///          cast at the IL boundary.
void rt_memory_retain_str(rt_string s) {
    if (!s)
        return;
    if (!rt_string_is_handle(s)) {
        rt_trap("Viper.Memory.RetainStr: invalid string handle");
        return;
    }
    rt_str_retain_maybe(s);
}

/// @brief Decrement the reference count and report last-user semantics.
/// @details Mirrors BASIC string behaviour by returning non-zero when the
///          underlying retain count reaches zero.  Null payloads are ignored to
///          simplify call sites that blindly forward optional objects.
/// @param p Object payload pointer managed by the runtime heap.
/// @return Non-zero when the retain count dropped to zero; otherwise zero.
int32_t rt_obj_release_check0(void *p) {
    if (!p)
        return 0;
    if (rt_string_is_handle(p)) {
        rt_str_release_maybe((rt_string)p);
        return 0;
    }
    if (!rt_heap_is_payload(p))
        return 0;
    return (int32_t)(rt_heap_release_deferred(p) == 0);
}

/// @brief Release a non-null runtime heap object without dynamic kind checks.
/// @details The caller must free the object through @ref rt_obj_free when this
///          returns non-zero, matching @ref rt_obj_release_check0 semantics.
int32_t rt_obj_release_known_check0(void *p) {
    if (!p)
        return 0;
    return (int32_t)(rt_heap_release_deferred(p) == 0);
}

/// @brief Convert a size_t reference count to int64_t, trapping on overflow.
static int64_t rt_memory_refcount_to_i64_named(size_t refcount, const char *api_name) {
    if (refcount > (size_t)INT64_MAX) {
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "%s: refcount exceeds Integer range",
                 api_name ? api_name : "Viper.Memory.Release");
        rt_trap(buf);
        return INT64_MAX;
    }
    return (int64_t)refcount;
}

static int64_t rt_memory_refcount_to_i64(size_t refcount) {
    return rt_memory_refcount_to_i64_named(refcount, "Viper.Memory.Release");
}

/// @brief Validate and release a runtime string, returning the post-release refcount.
/// @details Backs the string-handle path of `rt_memory_release` / `rt_memory_release_str`.
///          Validates @p s through `rt_string_is_handle` (a non-null but invalid handle
///          traps); decrements the refcount via `rt_string_unref_count`. Saturates the
///          immortal sentinel (`SIZE_MAX`) at `INT64_MAX` so callers can publish the
///          observed refcount via the typed Zia API without surprising overflow traps.
static int64_t rt_memory_release_string(rt_string s, const char *api_name) {
    if (!s)
        return 0;
    if (!rt_string_is_handle(s)) {
        char buf[128];
        snprintf(buf,
                 sizeof(buf),
                 "%s: invalid string handle",
                 api_name ? api_name : "Viper.Memory.Release");
        rt_trap(buf);
        return 0;
    }

    size_t next = rt_string_unref_count(s);
    if (next == SIZE_MAX)
        return INT64_MAX;
    return rt_memory_refcount_to_i64_named(next, api_name);
}

static int rt_memory_array_payload_is_releasable(rt_heap_hdr_t *hdr) {
    if (!hdr)
        return 0;
    if (hdr->len > hdr->cap) {
        rt_trap("Viper.Memory.Release: array length exceeds capacity");
        return 0;
    }
    switch ((rt_elem_kind_t)hdr->elem_kind) {
        case RT_ELEM_STR:
        case RT_ELEM_NONE:
        case RT_ELEM_I32:
        case RT_ELEM_I64:
        case RT_ELEM_F64:
        case RT_ELEM_U8:
        case RT_ELEM_BOX:
        case RT_ELEM_OBJ:
            return 1;
        default:
            rt_trap("Viper.Memory.Release: unsupported array element kind");
            return 0;
    }
}

/// @brief Drop element references owned by a heap array before its backing buffer is freed.
/// @details Dispatches on `hdr->elem_kind`:
///            - `RT_ELEM_STR` releases each retained `rt_string` slot.
///            - `RT_ELEM_BOX` releases each retained boxed-value slot, freeing on last
///              reference.
///            - `RT_ELEM_OBJ` releases each retained object pointer the same way.
///            - `RT_ELEM_NONE` and primitive kinds (`I32` / `I64` / `F64` / `U8`) are
///              no-ops — those payloads carry no managed references.
///          Every visited slot is NULLed after release so a re-entry from a finalizer that
///          observes the array can't re-release a stale pointer.
static void rt_memory_release_array_payload(void *p, rt_heap_hdr_t *hdr) {
    if (!p || !hdr)
        return;
    if (hdr->len > hdr->cap) {
        rt_trap("Viper.Memory.Release: array length exceeds capacity");
        return;
    }

    volatile size_t next_index = 0;
    volatile int trapped = 0;
    char saved_error[512] = {0};
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);

    for (;;) {
        if (setjmp(recovery) != 0) {
            if (!trapped) {
                const char *err = rt_trap_get_error();
                snprintf(saved_error,
                         sizeof(saved_error),
                         "%s",
                         err && err[0] ? err : "Viper.Memory.Release: array element cleanup trap");
            }
            trapped = 1;
        }

        if (next_index >= hdr->len)
            break;

        size_t n = hdr->len;
        switch ((rt_elem_kind_t)hdr->elem_kind) {
            case RT_ELEM_STR: {
                rt_string *items = (rt_string *)p;
                for (size_t i = (size_t)next_index; i < n; ++i) {
                    rt_string item = items[i];
                    items[i] = NULL;
                    next_index = i + 1;
                    rt_str_release_maybe(item);
                }
                break;
            }
            case RT_ELEM_NONE:
            case RT_ELEM_I32:
            case RT_ELEM_I64:
            case RT_ELEM_F64:
            case RT_ELEM_U8:
                next_index = n;
                break;
            case RT_ELEM_BOX:
            case RT_ELEM_OBJ: {
                void **items = (void **)p;
                for (size_t i = (size_t)next_index; i < n; ++i) {
                    void *item = items[i];
                    items[i] = NULL;
                    next_index = i + 1;
                    if (item && rt_obj_release_check0(item))
                        rt_obj_free(item);
                }
                break;
            }
            default:
                next_index = hdr->len;
                rt_trap("Viper.Memory.Release: unsupported array element kind");
                break;
        }
        break;
    }

    rt_trap_clear_recovery();
    if (trapped)
        rt_trap(saved_error);
}

static void rt_memory_free_zero_ref_array(void *p, rt_heap_hdr_t *hdr) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[512];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "Viper.Memory.Release: array cleanup trap");
        rt_trap_clear_recovery();
        rt_gc_clear_weak_refs(p);
        rt_heap_free_zero_ref(p);
        rt_trap(saved_error);
        return;
    }

    rt_memory_release_array_payload(p, hdr);
    rt_trap_clear_recovery();
    rt_gc_clear_weak_refs(p);
    rt_heap_free_zero_ref(p);
}

/// @brief Common finalize-and-free helper for managed object payloads at refcount zero.
/// @details Validates that @p p is a live heap object (traps if it's pointing at a string,
///          array, or arbitrary memory), runs the registered finalizer outside the heap
///          lock, frees the object, and writes the observed post-release refcount to
///          @p post_refcount when non-NULL. Used by both `rt_memory_release` and the
///          internal release-then-free shortcut paths.
static int32_t rt_obj_free_zero_ref_object(void *p, int64_t *post_refcount) {
    if (post_refcount)
        *post_refcount = 0;
    if (!p)
        return 0;

    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr) || !hdr)
        return 0;
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT) {
        rt_trap("rt_obj_free: expected object payload");
        return 0;
    }
    if (__atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE) != 0) {
        rt_trap("rt_obj_free: object refcount is not zero");
        return 0;
    }

    if (hdr->finalizer) {
        rt_heap_finalizer_t fin = hdr->finalizer;
        hdr->finalizer = NULL;
        jmp_buf recovery;
        rt_trap_set_recovery(&recovery);
        if (setjmp(recovery) != 0) {
            char saved_error[512];
            const char *err = rt_trap_get_error();
            snprintf(saved_error,
                     sizeof(saved_error),
                     "%s",
                     err && err[0] ? err : "rt_obj_free: finalizer trap");
            rt_trap_clear_recovery();

            size_t after_trap = __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE);
            if (after_trap != 0) {
                if (post_refcount)
                    *post_refcount = rt_memory_refcount_to_i64(after_trap);
                rt_trap(saved_error);
                return 0;
            }

            rt_gc_clear_weak_refs(p);
            rt_gc_untrack(p);
            rt_monitor_forget(p);
            rt_heap_free_zero_ref(p);
            rt_trap(saved_error);
            return 1;
        }
        fin(p);
        rt_trap_clear_recovery();
    }

    size_t after_finalizer = __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE);
    if (after_finalizer != 0) {
        if (post_refcount)
            *post_refcount = rt_memory_refcount_to_i64(after_finalizer);
        return 0;
    }

    rt_gc_clear_weak_refs(p);
    rt_gc_untrack(p);
    rt_monitor_forget(p);
    rt_heap_free_zero_ref(p);
    return 1;
}

/// @brief Public Viper.Memory release entry point with managed object finalization.
int64_t rt_memory_release(void *p) {
    if (!p)
        return 0;
    if (rt_string_is_handle(p)) {
        return rt_memory_release_string((rt_string)p, "Viper.Memory.Release");
    }
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr) || !hdr) {
        rt_trap("Viper.Memory.Release: invalid or freed heap object");
        return 0;
    }

    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT) {
        size_t next = rt_heap_release_deferred(p);
        if (next == 0) {
            int64_t post_refcount = 0;
            (void)rt_obj_free_zero_ref_object(p, &post_refcount);
            return post_refcount;
        }
        return rt_memory_refcount_to_i64(next);
    }

    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_ARRAY) {
        if (!rt_memory_array_payload_is_releasable(hdr))
            return 0;
        size_t next = rt_heap_release_deferred(p);
        if (next == 0) {
            rt_memory_free_zero_ref_array(p, hdr);
        }
        return rt_memory_refcount_to_i64(next);
    }

    rt_trap("Viper.Memory.Release: unsupported heap payload kind");
    return 0;
}

/// @brief Public string-typed wrapper for `Viper.Memory.ReleaseStr`.
/// @details Forwards to `rt_memory_release_string` so Zia and BASIC code can release a
///          runtime string without an `rt_string` ↔ `void *` cast at the IL boundary.
///          Returns the post-release refcount (saturated at `INT64_MAX` for immortal
///          strings) so callers can publish the observed value through the typed API.
int64_t rt_memory_release_str(rt_string s) {
    if (!s)
        return 0;
    return rt_memory_release_string(s, "Viper.Memory.ReleaseStr");
}

/// @brief Compatibility shim matching the string free entry point.
/// @details Releases storage for objects whose reference count already dropped
///          to zero.  The runtime heap performs the actual deallocation once
///          @ref rt_heap_free_zero_ref observes the zero count, mirroring the
///          BASIC string API while keeping the payload valid for user-defined
///          destructors until this helper runs.
/// @param p Object payload pointer; ignored when @c NULL.
void rt_obj_free(void *p) {
    if (!p)
        return;
    if (rt_string_is_handle(p)) {
        rt_str_release_maybe((rt_string)p);
        return;
    }
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(p, &hdr) || !hdr)
        return;
    if (__atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE) != 0) {
        rt_trap("rt_obj_free: payload refcount is not zero");
        return;
    }
    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_OBJECT) {
        (void)rt_obj_free_zero_ref_object(p, NULL);
        return;
    }
    if ((rt_heap_kind_t)hdr->kind == RT_HEAP_ARRAY) {
        if (!rt_memory_array_payload_is_releasable(hdr))
            return;
        rt_memory_free_zero_ref_array(p, hdr);
        return;
    }
    rt_trap("rt_obj_free: unsupported heap payload kind");
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
int64_t rt_obj_reference_equals(void *a, void *b) {
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
int64_t rt_obj_equals(void *self, void *other) {
    int self_is_string = rt_string_is_handle(self);
    int other_is_string = rt_string_is_handle(other);
    if (self_is_string || other_is_string)
        return (self_is_string && other_is_string && rt_str_eq((rt_string)self, (rt_string)other))
                   ? 1
                   : 0;
    if (rt_box_type(self) >= 0 || rt_box_type(other) >= 0)
        return rt_box_equal(self, other) ? 1 : 0;
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
int64_t rt_obj_get_hash_code(void *self) {
    if (!self)
        return 0;
    if (rt_string_is_handle(self)) {
        rt_string s = (rt_string)self;
        const char *bytes = rt_string_cstr(s);
        if (!bytes)
            return 0;
        return (int64_t)rt_fnv1a(bytes, (size_t)rt_str_len(s));
    }
    if (rt_box_type(self) >= 0)
        return (int64_t)rt_box_hash(self);
    uint64_t v = (uint64_t)(uintptr_t)self;
    v ^= v >> 33;
    v *= UINT64_C(0xff51afd7ed558ccd);
    v ^= v >> 33;
    v *= UINT64_C(0xc4ceb9fe1a85ec53);
    v ^= v >> 33;
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
rt_string rt_obj_to_string(void *self) {
    if (!self)
        return rt_string_from_bytes("<null>", 6);

    // Check if the pointer is a string handle (rt_string passed as obj).
    if (rt_string_is_handle(self))
        return rt_string_ref((rt_string)self);

    rt_heap_hdr_t *validated = NULL;
    int has_header = rt_heap_try_get_header(self, &validated);

    // Check if the object is a boxed value and auto-unbox for display.
    if (has_header && validated && validated->elem_kind == RT_ELEM_BOX) {
        int64_t tag = rt_box_type(self);
        if (tag == RT_BOX_STR) {
            return rt_unbox_str(self);
        }
        if (tag == RT_BOX_I64) {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%lld", (long long)rt_unbox_i64(self));
            if (n < 0)
                return rt_obj_make_cstr("Object");
            return rt_string_from_bytes(buf, (size_t)n);
        }
        if (tag == RT_BOX_F64) {
            char buf[64];
            rt_format_f64(rt_unbox_f64(self), buf, sizeof(buf));
            size_t n = strlen(buf);
            return rt_string_from_bytes(buf, (size_t)n);
        }
        if (tag == RT_BOX_I1) {
            int64_t v = rt_unbox_i1(self);
            return v ? rt_string_from_bytes("True", 4) : rt_string_from_bytes("False", 5);
        }
    }

    if (!has_header || !validated)
        return rt_obj_make_cstr("Object");
    if ((rt_heap_kind_t)validated->kind != RT_HEAP_OBJECT)
        return rt_obj_make_cstr("Object");
    const char *builtin_name = rt_obj_builtin_class_name(validated->class_id);
    if (validated->elem_kind == RT_ELEM_BOX && !builtin_name)
        builtin_name = "Viper.Core.Box";
    if (builtin_name)
        return rt_obj_make_cstr(builtin_name);
    if (validated->cap < sizeof(rt_object))
        return rt_obj_make_cstr("Object");

    rt_object *obj = (rt_object *)self;
    return rt_obj_type_name_from_class_info(rt_get_class_info_from_vptr(obj->vptr));
}

// ============================================================================
// Object Introspection
// ============================================================================

/// @brief Get the qualified type name of an object.
/// @param self Object to query (may be NULL).
/// @return A string containing the class qualified name, or "<null>".
rt_string rt_obj_type_name(void *self) {
    if (!self)
        return rt_string_from_bytes("<null>", 6);
    if (rt_string_is_handle(self))
        return rt_obj_make_cstr("Viper.String");

    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(self, &hdr) || !hdr)
        return rt_obj_make_cstr("Object");
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT)
        return rt_obj_make_cstr("Object");
    const char *builtin_name = rt_obj_builtin_class_name(hdr->class_id);
    if (hdr->elem_kind == RT_ELEM_BOX && !builtin_name)
        builtin_name = "Viper.Core.Box";
    if (builtin_name)
        return rt_obj_make_cstr(builtin_name);
    if (hdr->cap < sizeof(rt_object))
        return rt_obj_make_cstr("Object");

    rt_object *obj = (rt_object *)self;
    if (!obj->vptr)
        return rt_obj_make_cstr("Object");
    return rt_obj_type_name_from_class_info(rt_get_class_info_from_vptr(obj->vptr));
}

/// @brief Get the numeric type ID of an object.
/// @param self Object to query (may be NULL).
/// @return The type ID, or 0 if self is NULL.
int64_t rt_obj_type_id(void *self) {
    if (!self)
        return 0;
    if (rt_string_is_handle(self))
        return RT_STRING_CLASS_ID;
    return rt_obj_class_id(self);
}

/// @brief Check if an object reference is null.
/// @param self Object to test.
/// @return 1 if self is NULL, 0 otherwise.
int64_t rt_obj_is_null(void *self) {
    return self == NULL ? 1 : 0;
}

// ============================================================================
// Weak Reference Support
// ============================================================================

static int rt_weak_value_is_managed(void *value) {
    return value && (rt_string_is_handle(value) || rt_heap_is_payload(value));
}

/// @brief Store a weak reference without incrementing reference count.
///
/// Used for weak reference fields to break reference cycles. The stored
/// pointer does not keep the managed target alive - if the target's reference
/// count reaches zero through other paths, it will be freed and the weak handle
/// will be zeroed.
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
/// @param value Managed object, array, or string handle to store (may be NULL).
///              Non-runtime raw pointers are stored borrowed for compatibility.
void rt_weak_store(void **addr, void *value) {
    if (!addr)
        return;
    void *current = *addr;
    if (rt_weakref_is_handle(current)) {
        if (rt_weak_value_is_managed(value)) {
            rt_weakref_reset((rt_weakref *)current, value);
            return;
        }
        rt_weakref_free((rt_weakref *)current);
        current = NULL;
    }

    if (rt_weak_value_is_managed(value)) {
        *addr = rt_weakref_new(value);
        return;
    }

    // Fallback for legacy/raw pointers that are not runtime-managed heap objects.
    *addr = value;
}

/// @brief Load a weak reference.
///
/// Retrieves the stored pointer value. Runtime-managed weak handles are
/// promoted to a retained strong reference before returning so callers can
/// safely use the target after the GC lock is released. Raw legacy pointers
/// are still returned as borrowed values.
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
/// @return The stored pointer value, or NULL if the field is nil. Managed
///         weak handles return an owned reference that must be released.
///
/// @warning Legacy/raw pointer slots are not lifetime-checked and remain
///          borrowed for compatibility.
void *rt_weak_load(void **addr) {
    if (!addr)
        return NULL;
    void *slot = *addr;
    if (rt_weakref_is_handle(slot))
        return rt_weakref_get((rt_weakref *)slot);
    return slot;
}
