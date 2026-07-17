//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_box.c
// Purpose: Implements boxing and unboxing primitives that wrap scalar values
//          (i64, f64, bool, and string) into heap-allocated objects for
//          use in generic collections (Seq, Map, List). Each boxed value
//          carries a type tag for runtime type discrimination.
//
// Key invariants:
//   - Boxed values are heap-allocated via rt_heap_alloc and reference-counted.
//   - Type tags (BOX_TYPE_I64, BOX_TYPE_F64, BOX_TYPE_STR, etc.) uniquely
//     identify the contained type.
//   - Strict unboxing traps on null, invalid boxes, or tag mismatches.
//   - Try-unboxing reports null/invalid/type mismatches without trapping.
//   - The boxed string retains a reference to the rt_string and releases it
//     when the box is freed.
//   - Equality comparison for boxes compares type tags AND values.
//
// Ownership/Lifetime:
//   - Callers receive a fresh reference (refcount=1) from Box constructors.
//   - Boxed strings hold a retained reference to the rt_string.
//   - The GC finalizer releases the contained string reference if applicable.
//
// Links: src/runtime/oop/rt_box.h (public API),
//        src/runtime/rt_heap.h (allocation and refcount),
//        src/runtime/rt_string.h (string retain/release for boxed strings)
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_gc.h"
#include "rt_hash_util.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <sched.h>
#endif

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);

/// @brief Internal payload layout for heap-allocated boxed primitive values.
typedef struct rt_box {
    int64_t tag;

    union {
        int64_t i64_val;
        double f64_val;
        rt_string str_val;
    } data;
} rt_box_t;

typedef struct value_type_field {
    size_t offset;
    int64_t kind;
    struct value_type_field *next;
} value_type_field;

typedef struct value_type_field_desc {
    size_t offset;
    int64_t kind;
} value_type_field_desc;

typedef struct value_type_layout {
    void *obj;
    value_type_field *fields;
    rt_heap_finalizer_t previous_finalizer;
    struct value_type_layout *next;
} value_type_layout;

static value_type_layout *g_value_type_layouts = NULL;
static int g_value_type_layout_lock = 0;

static void value_type_lock(void) {
    if (__atomic_test_and_set(&g_value_type_layout_lock, __ATOMIC_ACQUIRE)) {
        do {
#if RT_PLATFORM_WINDOWS
            SwitchToThread();
#else
            sched_yield();
#endif
        } while (__atomic_test_and_set(&g_value_type_layout_lock, __ATOMIC_ACQUIRE));
    }
}

static void value_type_unlock(void) {
    __atomic_clear(&g_value_type_layout_lock, __ATOMIC_RELEASE);
}

static value_type_layout *value_type_find_locked(void *obj) {
    for (value_type_layout *layout = g_value_type_layouts; layout; layout = layout->next) {
        if (layout->obj == obj)
            return layout;
    }
    return NULL;
}

static value_type_layout *value_type_detach_locked(void *obj) {
    value_type_layout **pp = &g_value_type_layouts;
    while (*pp) {
        if ((*pp)->obj == obj) {
            value_type_layout *layout = *pp;
            *pp = layout->next;
            layout->next = NULL;
            return layout;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

static void value_type_release_slot(void *obj, const value_type_field *field) {
    if (!obj || !field)
        return;
    void **slot = (void **)((unsigned char *)obj + field->offset);
    if (field->kind == RT_VALUE_FIELD_STR) {
        rt_str_release_maybe((rt_string)*slot);
        *slot = NULL;
    } else if (field->kind == RT_VALUE_FIELD_OBJ) {
        void *child = *slot;
        *slot = NULL;
        if (rt_obj_release_check0(child))
            rt_obj_free(child);
    }
}

static void value_type_release_retained_slot(void *obj, const value_type_field *field) {
    if (!obj || !field)
        return;
    void **slot = (void **)((unsigned char *)obj + field->offset);
    if (field->kind == RT_VALUE_FIELD_STR) {
        rt_str_release_maybe((rt_string)*slot);
    } else if (field->kind == RT_VALUE_FIELD_OBJ) {
        void *child = *slot;
        if (rt_obj_release_check0(child))
            rt_obj_free(child);
    }
}

static void value_type_retain_slot(void *obj, const value_type_field *field) {
    if (!obj || !field)
        return;
    void **slot = (void **)((unsigned char *)obj + field->offset);
    if (field->kind == RT_VALUE_FIELD_STR) {
        rt_string_ref((rt_string)*slot);
    } else if (field->kind == RT_VALUE_FIELD_OBJ) {
        rt_obj_retain_maybe(*slot);
    }
}

static int value_type_slot_is_valid(void *obj, const value_type_field *field) {
    if (!obj || !field)
        return 0;
    void **slot = (void **)((unsigned char *)obj + field->offset);
    void *value = *slot;
    if (!value)
        return 1;
    if (field->kind == RT_VALUE_FIELD_STR)
        return rt_string_is_handle(value) != 0;
    if (field->kind == RT_VALUE_FIELD_OBJ)
        return rt_string_is_handle(value) || rt_heap_is_payload(value);
    return 0;
}

static void value_type_free_layout(value_type_layout *layout) {
    if (!layout)
        return;
    value_type_field *field = layout->fields;
    while (field) {
        value_type_field *next = field->next;
        free(field);
        field = next;
    }
    free(layout);
}

static int value_type_release_layout_slots(void *obj,
                                           value_type_layout *layout,
                                           char *error,
                                           size_t error_size) {
    if (!obj || !layout)
        return 0;

    int trapped = 0;
    value_type_field *volatile cursor = layout->fields;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);

    for (;;) {
        if (setjmp(recovery) != 0) {
            if (!trapped && error && error_size > 0) {
                const char *err = rt_trap_get_error();
                snprintf(error,
                         error_size,
                         "%s",
                         err && err[0] ? err : "rt_box_value_type: field cleanup trap");
            }
            trapped = 1;
        }

        if (!cursor)
            break;
        value_type_field *field = cursor;
        cursor = cursor->next;
        value_type_release_slot(obj, field);
    }

    rt_trap_clear_recovery();
    return trapped;
}

static int box_tag_is_valid(int64_t tag) {
    return tag == RT_BOX_I64 || tag == RT_BOX_F64 || tag == RT_BOX_I1 || tag == RT_BOX_STR;
}

static void value_type_finalizer(void *obj) {
    rt_heap_finalizer_t previous = NULL;
    value_type_lock();
    value_type_layout *layout = value_type_find_locked(obj);
    previous = layout ? layout->previous_finalizer : NULL;
    value_type_unlock();

    if (!layout)
        return;
    int previous_trapped = 0;
    char previous_error[512] = {0};
    if (previous && previous != value_type_finalizer) {
        jmp_buf previous_recovery;
        rt_trap_set_recovery(&previous_recovery);
        if (setjmp(previous_recovery) != 0) {
            const char *err = rt_trap_get_error();
            snprintf(previous_error,
                     sizeof(previous_error),
                     "%s",
                     err && err[0] ? err : "rt_box_value_type: chained finalizer trap");
            previous_trapped = 1;
        } else {
            previous(obj);
        }
        rt_trap_clear_recovery();
    }

    rt_heap_hdr_t *hdr = NULL;
    if (rt_heap_try_get_header(obj, &hdr) && hdr &&
        __atomic_load_n(&hdr->refcnt, __ATOMIC_ACQUIRE) != 0) {
        rt_heap_finalizer_t reinstalled = hdr->finalizer;
        value_type_lock();
        value_type_layout *live_layout = value_type_find_locked(obj);
        if (live_layout && reinstalled != value_type_finalizer)
            live_layout->previous_finalizer = reinstalled;
        value_type_unlock();
        rt_obj_set_finalizer(obj, value_type_finalizer);
        if (previous_trapped)
            rt_trap(previous_error);
        return;
    }

    value_type_lock();
    layout = value_type_detach_locked(obj);
    value_type_unlock();
    if (!layout)
        return;
    char field_error[512] = {0};
    int field_trapped =
        value_type_release_layout_slots(obj, layout, field_error, sizeof(field_error));
    value_type_free_layout(layout);
    if (previous_trapped)
        rt_trap(previous_error);
    if (field_trapped)
        rt_trap(field_error[0] ? field_error : "rt_box_value_type: field cleanup trap");
}

static void value_type_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    if (!obj || !visitor)
        return;

    int64_t count = 0;
    value_type_lock();
    value_type_layout *layout = value_type_find_locked(obj);
    for (value_type_field *field = layout ? layout->fields : NULL; field; field = field->next) {
        if (field->kind == RT_VALUE_FIELD_OBJ)
            count++;
    }
    if (count <= 0) {
        value_type_unlock();
        return;
    }

    if ((uint64_t)count > (uint64_t)SIZE_MAX / sizeof(value_type_field_desc)) {
        value_type_unlock();
        rt_trap("rt_box_value_type: traversal allocation too large");
        return;
    }
    value_type_field_desc *volatile fields =
        (value_type_field_desc *)calloc((size_t)count, sizeof(value_type_field_desc));
    if (!fields) {
        value_type_unlock();
        rt_trap("rt_box_value_type: traversal allocation failed");
        return;
    }

    int64_t copied = 0;
    for (value_type_field *field = layout ? layout->fields : NULL; field; field = field->next) {
        if (field->kind == RT_VALUE_FIELD_OBJ) {
            ((value_type_field_desc *)fields)[copied].offset = field->offset;
            ((value_type_field_desc *)fields)[copied].kind = field->kind;
            copied++;
        }
    }
    value_type_unlock();

    void **volatile children = (void **)calloc((size_t)copied, sizeof(void *));
    if (!children) {
        free((void *)fields);
        rt_trap("rt_box_value_type: traversal allocation failed");
        return;
    }

    int64_t volatile retained_count = 0;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "rt_box_value_type: traversal trap");
        rt_trap_clear_recovery();
        for (int64_t i = 0; i < retained_count; ++i) {
            void *child = ((void **)children)[i];
            ((void **)children)[i] = NULL;
            if (rt_obj_release_check0(child))
                rt_obj_free(child);
        }
        free((void *)fields);
        free((void *)children);
        rt_trap(saved_error);
        return;
    }

    for (int64_t i = 0; i < copied; ++i) {
        void *child =
            *(void **)((unsigned char *)obj + ((value_type_field_desc *)fields)[i].offset);
        if (child)
            rt_obj_retain_maybe(child);
        ((void **)children)[retained_count++] = child;
    }
    free((void *)fields);
    fields = NULL;

    for (int64_t i = 0; i < copied; ++i) {
        void *child = ((void **)children)[i];
        if (child)
            visitor(child, ctx);
        ((void **)children)[i] = NULL;
        if (rt_obj_release_check0(child))
            rt_obj_free(child);
    }
    rt_trap_clear_recovery();
    free((void *)children);
}

/// @brief Allocate a fresh boxed-value object via the heap (refcount=1, tagged RT_ELEM_BOX so
/// `box_maybe` can later identify it). Caller fills the tag and union fields.
static void *alloc_box(void) {
    void *box = rt_obj_new_i64(RT_BOX_CLASS_ID, (int64_t)sizeof(rt_box_t));
    rt_heap_hdr_t *hdr = rt_heap_hdr(box);
    if (hdr)
        hdr->elem_kind = RT_ELEM_BOX;
    return box;
}

/// @brief Safe down-cast: returns the `rt_box_t *` only if `box` is a heap-allocated object whose
/// element-kind is RT_ELEM_BOX. Returns NULL for null pointers, non-heap pointers, or heap objects
/// of a different kind. Used to make `rt_box_eq_*`, `rt_box_hash`, and `rt_box_equal` safe when
/// passed arbitrary collection elements.
static rt_box_t *box_maybe(void *box) {
    rt_heap_hdr_t *hdr = NULL;
    if (!box || !rt_heap_try_get_header(box, &hdr))
        return NULL;
    if (!hdr || (rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT || hdr->class_id != RT_BOX_CLASS_ID ||
        hdr->elem_kind != RT_ELEM_BOX || hdr->cap < sizeof(rt_box_t))
        return NULL;
    rt_box_t *b = (rt_box_t *)box;
    return box_tag_is_valid(b->tag) ? b : NULL;
}

/// @brief Strict accessor used by the unbox-* primitives: traps with a formatted message if `box`
/// is null, isn't actually a boxed value, or has a tag that doesn't match `expected_tag`. Pass
/// `expected_tag = -1` to skip the type check (accept any tag).
static rt_box_t *box_require(void *box, const char *fn_name, int64_t expected_tag) {
    if (!box) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s: null pointer", fn_name);
        rt_trap(buf);
        return NULL;
    }

    rt_box_t *b = box_maybe(box);
    if (!b) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s: invalid boxed value", fn_name);
        rt_trap(buf);
        return NULL;
    }

    if (expected_tag >= 0 && b->tag != expected_tag) {
        char buf[96];
        const char *type_name = "unknown";
        switch (expected_tag) {
            case RT_BOX_I64:
                type_name = "i64";
                break;
            case RT_BOX_F64:
                type_name = "f64";
                break;
            case RT_BOX_I1:
                type_name = "i1";
                break;
            case RT_BOX_STR:
                type_name = "str";
                break;
        }
        snprintf(buf, sizeof(buf), "%s: type mismatch (expected %s)", fn_name, type_name);
        rt_trap(buf);
        return NULL;
    }

    return b;
}

/// @brief Wrap an Int64 into a heap-allocated Box. Refcount=1; release as any other heap object.
void *rt_box_i64(int64_t val) {
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_I64;
    box->data.i64_val = val;
    return box;
}

/// @brief Wrap a Float64 into a heap-allocated Box. NaN is stored as-is (round-trip safe).
void *rt_box_f64(double val) {
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_F64;
    box->data.f64_val = val;
    return box;
}

/// @brief Wrap a Boolean into a heap-allocated Box. Normalizes to {0, 1} so two true booleans
/// from different sources compare equal.
void *rt_box_i1(int64_t val) {
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_I1;
    box->data.i64_val = val ? 1 : 0;
    return box;
}

void *rt_box_i1_bool(int8_t val) {
    return rt_box_i1(val ? 1 : 0);
}

/// @brief GC finalizer for boxed strings — releases the contained rt_string reference. Other
/// box variants (i64/f64/i1) hold no managed references so don't need a finalizer.
static void box_str_finalizer(void *obj) {
    rt_box_t *box = (rt_box_t *)obj;
    if (box && box->tag == RT_BOX_STR && box->data.str_val) {
        rt_str_release_maybe(box->data.str_val);
        box->data.str_val = NULL;
    }
}

/// @brief Wrap an rt_string into a heap-allocated Box, retaining the string (via `rt_string_ref`,
/// which handles both heap and literal-pool strings) and registering `box_str_finalizer` to
/// release it on collection. Stores NULL string as-is.
void *rt_box_str(rt_string val) {
    if (val && !rt_string_is_handle(val)) {
        rt_trap("rt_box_str: invalid string handle");
        return NULL;
    }
    rt_string retained = val ? rt_string_ref(val) : NULL;

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "rt_box_str: allocation failed");
        rt_trap_clear_recovery();
        rt_str_release_maybe(retained);
        rt_trap(saved_error);
        return NULL;
    }

    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box) {
        rt_trap_clear_recovery();
        rt_str_release_maybe(retained);
        return NULL;
    }
    box->tag = RT_BOX_STR;
    box->data.str_val = retained;
    rt_obj_set_finalizer(box, box_str_finalizer);
    rt_trap_clear_recovery();
    return box;
}

/// @brief Extract the i64 contents. **Traps** if `box` isn't a Box or its tag isn't RT_BOX_I64.
int64_t rt_unbox_i64(void *box) {
    rt_box_t *b = box_require(box, "rt_unbox_i64", RT_BOX_I64);
    if (!b)
        return 0;
    return b->data.i64_val;
}

/// @brief Extract the f64 contents. **Traps** if `box` isn't a Box or its tag isn't RT_BOX_F64.
double rt_unbox_f64(void *box) {
    rt_box_t *b = box_require(box, "rt_unbox_f64", RT_BOX_F64);
    if (!b)
        return 0.0;
    return b->data.f64_val;
}

/// @brief Extract the bool contents (returned as 0/1). **Traps** on tag mismatch.
int8_t rt_unbox_i1(void *box) {
    rt_box_t *b = box_require(box, "rt_unbox_i1", RT_BOX_I1);
    if (!b)
        return 0;
    return b->data.i64_val ? 1 : 0;
}

/// @brief Extract the rt_string contents, **retaining a fresh reference** for the caller (the box
/// retains its own; the returned ref must be released independently). Traps on tag mismatch.
rt_string rt_unbox_str(void *box) {
    rt_box_t *b = box_require(box, "rt_unbox_str", RT_BOX_STR);
    if (!b)
        return NULL;
    rt_string s = b->data.str_val;
    // Retain before returning - use rt_string_ref for proper handling
    if (s) {
        rt_string_ref(s);
    }
    return s;
}

/// @brief Try to extract an `i64` value from @p box, never trapping. Returns 1 on success.
/// @details Option-style accessor backing `Viper.Core.Box.ToI64Option`. On success writes the
///          unboxed `int64_t` to @p out and returns 1. Returns 0 (with @p out zeroed) when
///          @p box is NULL, isn't a Box, has the wrong tag, or @p out itself is NULL.
int8_t rt_box_try_to_i64(void *box, int64_t *out) {
    if (out)
        *out = 0;
    if (!out)
        return 0;
    rt_box_t *b = box_maybe(box);
    if (!b || b->tag != RT_BOX_I64)
        return 0;
    *out = b->data.i64_val;
    return 1;
}

/// @brief Try to extract an `f64` value from @p box, never trapping. Returns 1 on success.
/// @details Mirror of `rt_box_try_to_i64` for `RT_BOX_F64`. Failure paths zero @p out.
int8_t rt_box_try_to_f64(void *box, double *out) {
    if (out)
        *out = 0.0;
    if (!out)
        return 0;
    rt_box_t *b = box_maybe(box);
    if (!b || b->tag != RT_BOX_F64)
        return 0;
    *out = b->data.f64_val;
    return 1;
}

/// @brief Try to extract a bool value from @p box, never trapping. Returns 1 on success.
/// @details Mirror of `rt_box_try_to_i64` for `RT_BOX_I1`. The contained `int64_t` is
///          normalised to `0`/`1` via the ternary so callers always observe a canonical
///          boolean even if the box was constructed with a non-canonical truthy integer.
int8_t rt_box_try_to_i1(void *box, int8_t *out) {
    if (out)
        *out = 0;
    if (!out)
        return 0;
    rt_box_t *b = box_maybe(box);
    if (!b || b->tag != RT_BOX_I1)
        return 0;
    *out = b->data.i64_val ? 1 : 0;
    return 1;
}

/// @brief Try to extract a runtime string from @p box, never trapping. Returns 1 on success.
/// @details On success writes a *retained* string handle to @p out — caller owns the new
///          reference and must release it. This raw C helper is runtime-internal; the public
///          `Viper.Core.Box.ToStrOption` surface returns an owned `Option<String>`.
///          Failure paths NULL out @p out.
int8_t rt_box_try_to_str(void *box, rt_string *out) {
    if (out)
        *out = NULL;
    if (!out)
        return 0;
    rt_box_t *b = box_maybe(box);
    if (!b || b->tag != RT_BOX_STR)
        return 0;
    if (b->data.str_val)
        rt_string_ref(b->data.str_val);
    *out = b->data.str_val;
    return 1;
}

void *rt_box_to_i64_option(void *box) {
    int64_t value = 0;
    return rt_box_try_to_i64(box, &value) ? rt_option_some_i64(value) : rt_option_none();
}

void *rt_box_to_f64_option(void *box) {
    double value = 0.0;
    return rt_box_try_to_f64(box, &value) ? rt_option_some_f64(value) : rt_option_none();
}

void *rt_box_to_i1_option(void *box) {
    int8_t value = 0;
    return rt_box_try_to_i1(box, &value) ? rt_option_some_i1(value) : rt_option_none();
}

void *rt_box_to_str_option(void *box) {
    rt_string value = NULL;
    if (!rt_box_try_to_str(box, &value))
        return rt_option_none();

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "rt_box_to_str_option: option allocation failed");
        rt_trap_clear_recovery();
        rt_str_release_maybe(value);
        rt_trap(saved_error);
        return NULL;
    }
    void *option = rt_option_some_str(value);
    rt_trap_clear_recovery();
    rt_str_release_maybe(value);
    return option;
}

/// @brief Read the type tag of a box (`RT_BOX_I64`, `RT_BOX_F64`, `RT_BOX_I1`, `RT_BOX_STR`),
/// or -1 if the pointer isn't a Box. Used to dispatch on contained type without unboxing.
int64_t rt_box_type(void *box) {
    rt_box_t *b = box_maybe(box);
    if (!b)
        return -1;
    return b->tag;
}

/// @brief Compare a box to a raw i64. Returns 0 (not 1) for non-i64 boxes — never traps, so
/// safe for heterogeneous collection scans (e.g. `Seq.contains(boxedValue)`).
int64_t rt_box_eq_i64(void *box, int64_t val) {
    rt_box_t *b = box_maybe(box);
    if (!b)
        return 0;
    if (b->tag != RT_BOX_I64)
        return 0;
    return b->data.i64_val == val ? 1 : 0;
}

/// @brief Compare a box to a raw f64. Uses IEEE-754 `==`, so `Box(NaN).eq(NaN) == 0` (intentional).
int64_t rt_box_eq_f64(void *box, double val) {
    rt_box_t *b = box_maybe(box);
    if (!b)
        return 0;
    if (b->tag != RT_BOX_F64)
        return 0;
    // IEEE 754: NaN != NaN, so Box(NaN).Eq(NaN) returns 0. This is intentional.
    return b->data.f64_val == val ? 1 : 0;
}

/// @brief Compare a box to a raw rt_string. Delegates to `rt_str_eq` so encoding is handled
/// canonically; returns 0 if `box` isn't a string box.
int64_t rt_box_eq_str(void *box, rt_string val) {
    if (val && !rt_string_is_handle(val)) {
        rt_trap("rt_box_eq_str: invalid string handle");
        return 0;
    }
    rt_box_t *b = box_maybe(box);
    if (!b)
        return 0;
    if (b->tag != RT_BOX_STR)
        return 0;
    if (!b->data.str_val || !val)
        return b->data.str_val == val ? 1 : 0;
    return rt_str_eq(b->data.str_val, val);
}

/// @brief Allocate a raw heap region of `size` bytes for a Zia value-type instance (struct).
/// Distinct from the tagged Box family — this isn't a Box at all (RT_ELEM_NONE), the compiler
/// emits direct field copies into the returned memory. Zero-sized value types are
/// valid and allocate a managed header with an empty payload.
void *rt_box_value_type(int64_t size) {
    if (size < 0) {
        rt_trap("rt_box_value_type: negative size");
        return NULL;
    }
    if ((uint64_t)size > (uint64_t)SIZE_MAX) {
        rt_trap("rt_box_value_type: size too large");
        return NULL;
    }
    return rt_obj_new_i64(RT_VALUE_TYPE_CLASS_ID, size);
}

/// @brief Register a managed-field offset on a value-type instance for GC traversal and finalize.
/// @details Backs `Viper.Core.Box.ValueType.AddField`. Validates that:
///            - @p obj is a live value-type heap object (class id `RT_VALUE_TYPE_CLASS_ID`),
///            - @p offset is non-negative, pointer-aligned, and within `hdr->cap` with room
///              for a `void *` slot,
///            - @p kind is `RT_VALUE_FIELD_OBJ` or `RT_VALUE_FIELD_STR`.
///          Each precondition violation traps with a descriptive message.
///
///          On success allocates a `value_type_field` node and links it into the layout
///          for @p obj (creating the layout entry on first call). When @p retain_now is
///          non-zero the runtime takes its own retain on whatever value already lives in
///          the slot — used at construction time when the caller transfers an owned
///          reference into a freshly-allocated value type.
void rt_box_value_type_add_field(void *obj, int64_t offset, int64_t kind, int8_t retain_now) {
    if (!obj) {
        rt_trap("rt_box_value_type_add_field: null value type");
        return;
    }
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(obj, &hdr) || !hdr || (rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT ||
        hdr->class_id != RT_VALUE_TYPE_CLASS_ID) {
        rt_trap("rt_box_value_type_add_field: invalid value type object");
        return;
    }
    if (offset < 0 || (uint64_t)offset > (uint64_t)SIZE_MAX || (size_t)offset > hdr->cap ||
        hdr->cap - (size_t)offset < sizeof(void *)) {
        rt_trap("rt_box_value_type_add_field: field offset out of range");
        return;
    }
    if (((size_t)offset % sizeof(void *)) != 0) {
        rt_trap("rt_box_value_type_add_field: field offset is not pointer-aligned");
        return;
    }
    if (kind != RT_VALUE_FIELD_OBJ && kind != RT_VALUE_FIELD_STR) {
        rt_trap("rt_box_value_type_add_field: invalid field kind");
        return;
    }

    size_t field_offset = (size_t)offset;
    value_type_lock();
    value_type_layout *existing_layout = value_type_find_locked(obj);
    if (existing_layout) {
        for (value_type_field *existing = existing_layout->fields; existing;
             existing = existing->next) {
            if (existing->offset == field_offset) {
                int same_kind = existing->kind == kind;
                value_type_unlock();
                if (same_kind)
                    return;
                rt_trap("rt_box_value_type_add_field: field offset already registered");
                return;
            }
        }
    }
    value_type_unlock();

    value_type_field *field = (value_type_field *)calloc(1, sizeof(value_type_field));
    value_type_layout *new_layout = (value_type_layout *)calloc(1, sizeof(value_type_layout));
    if (!field || !new_layout) {
        free(field);
        free(new_layout);
        rt_trap("rt_box_value_type_add_field: memory allocation failed");
        return;
    }
    field->offset = field_offset;
    field->kind = kind;
    value_type_field retain_field = {field_offset, kind, NULL};
    int retained_slot = 0;
    if (retain_now) {
        if (!value_type_slot_is_valid(obj, &retain_field)) {
            free(field);
            free(new_layout);
            rt_trap("rt_box_value_type_add_field: invalid managed field value");
            return;
        }
        jmp_buf retain_recovery;
        rt_trap_set_recovery(&retain_recovery);
        if (setjmp(retain_recovery) != 0) {
            char saved_error[256];
            const char *err = rt_trap_get_error();
            snprintf(saved_error,
                     sizeof(saved_error),
                     "%s",
                     err && err[0] ? err : "rt_box_value_type_add_field: retain failed");
            rt_trap_clear_recovery();
            free(field);
            free(new_layout);
            rt_trap(saved_error);
            return;
        }
        value_type_retain_slot(obj, &retain_field);
        rt_trap_clear_recovery();
        retained_slot = 1;
    }

    int installed_layout = 0;
    int inserted_field = 0;
    value_type_lock();
    value_type_layout *layout = value_type_find_locked(obj);
    if (!layout) {
        new_layout->obj = obj;
        new_layout->previous_finalizer = hdr->finalizer;
        new_layout->next = g_value_type_layouts;
        g_value_type_layouts = new_layout;
        layout = new_layout;
        new_layout = NULL;
        installed_layout = 1;
    }
    int duplicate = 0;
    int conflicting = 0;
    for (value_type_field *existing = layout->fields; existing; existing = existing->next) {
        if (existing->offset == field->offset) {
            if (existing->kind == field->kind)
                duplicate = 1;
            else
                conflicting = 1;
            break;
        }
    }
    if (!duplicate && !conflicting) {
        field->next = layout->fields;
        layout->fields = field;
        field = NULL;
        inserted_field = 1;
    }
    value_type_unlock();

    free(field);
    free(new_layout);
    if (!inserted_field && retained_slot)
        value_type_release_retained_slot(obj, &retain_field);

    if (conflicting) {
        rt_trap("rt_box_value_type_add_field: field offset already registered");
        return;
    }

    if (installed_layout) {
        rt_obj_set_finalizer(obj, value_type_finalizer);
        jmp_buf track_recovery;
        rt_trap_set_recovery(&track_recovery);
        if (setjmp(track_recovery) != 0) {
            char saved_error[256];
            const char *err = rt_trap_get_error();
            snprintf(saved_error,
                     sizeof(saved_error),
                     "%s",
                     err && err[0] ? err : "rt_box_value_type_add_field: GC track failed");
            rt_trap_clear_recovery();
            value_type_lock();
            value_type_layout *removed = value_type_detach_locked(obj);
            value_type_unlock();
            rt_heap_finalizer_t previous = removed ? removed->previous_finalizer : NULL;
            if (retained_slot)
                value_type_release_retained_slot(obj, &retain_field);
            value_type_free_layout(removed);
            rt_obj_set_finalizer(obj, previous);
            rt_trap(saved_error);
            return;
        }
        rt_gc_track(obj, value_type_traverse);
        rt_trap_clear_recovery();
    }
    (void)inserted_field;
}

//===----------------------------------------------------------------------===//
// Content-aware hashing and equality for boxed values
//===----------------------------------------------------------------------===//


/// @brief Check if a heap-allocated element is a boxed value.
/// Safe for non-heap pointers: checks magic before accessing header fields.
static int is_boxed(void *elem) {
    return box_maybe(elem) != NULL;
}

/// @brief Content-based hash for hashtable storage. For boxed values: FNV-1a over the contained
/// scalar bytes (or string content). For non-box pointers: Knuth-multiplicative pointer hash so
/// raw heap pointers in mixed collections still distribute reasonably. **Caller-side note:**
/// strings hash by content, so two boxed-string instances with equal text hash equally — required
/// for `Map[Box, ...]` lookup correctness.
size_t rt_box_hash(void *elem) {
    if (is_boxed(elem)) {
        rt_box_t *box = (rt_box_t *)elem;
        switch (box->tag) {
            case RT_BOX_I64:
            case RT_BOX_I1:
                return (size_t)rt_fnv1a(&box->data.i64_val, sizeof(int64_t));
            case RT_BOX_F64: {
                double value = box->data.f64_val;
                if (isnan(value)) {
                    uint64_t canonical_nan = UINT64_C(0x7ff8000000000000);
                    return (size_t)rt_fnv1a(&canonical_nan, sizeof(canonical_nan));
                }
                if (value == 0.0)
                    value = 0.0;
                return (size_t)rt_fnv1a(&value, sizeof(double));
            }
            case RT_BOX_STR: {
                rt_string s = box->data.str_val;
                if (!s)
                    return 0;
                const char *cstr = rt_string_cstr(s);
                if (!cstr)
                    return 0;
                return (size_t)rt_fnv1a(cstr, (size_t)rt_str_len(s));
            }
            default:
                break;
        }
    }
    // Fallback: pointer identity hash
    const uint64_t KNUTH_MULT = 0x9e3779b97f4a7c15ULL;
    uint64_t val = (uint64_t)(uintptr_t)elem;
    return (size_t)((val * KNUTH_MULT) >> 16);
}

/// @brief Content-based equality for hashtable buckets. Handles pointer-identity fast path,
/// rejects mixed box vs non-box, then dispatches by tag. Companion to `rt_box_hash` — together
/// they let `Set[Box]` and `Map[Box, ...]` deduplicate by VALUE, not by pointer identity.
/// @brief Sort rank for the default collection comparator (VDOC-089).
/// @details Ranking by type class first makes the order total and transitive:
///          NULL < numeric (boxed i64/i1/f64) < string (raw or boxed) < other.
static int box_sort_rank(void *p) {
    if (!p)
        return 0;
    if (rt_string_is_handle(p))
        return 2;
    switch (rt_box_type(p)) {
        case RT_BOX_I64:
        case RT_BOX_I1:
        case RT_BOX_F64:
            return 1;
        case RT_BOX_STR:
            return 2;
        default:
            return 3;
    }
}

int64_t rt_box_default_sort_compare(void *a, void *b) {
    int ra = box_sort_rank(a);
    int rb = box_sort_rank(b);
    if (ra != rb)
        return ra < rb ? -1 : 1;

    switch (ra) {
        case 0:
            return 0; // both NULL
        case 1: {
            // Numerics: exact when both are integers, double otherwise.
            int64_t ta = rt_box_type(a);
            int64_t tb = rt_box_type(b);
            if (ta != RT_BOX_F64 && tb != RT_BOX_F64) {
                int64_t ia = ta == RT_BOX_I1 ? (int64_t)rt_unbox_i1(a) : rt_unbox_i64(a);
                int64_t ib = tb == RT_BOX_I1 ? (int64_t)rt_unbox_i1(b) : rt_unbox_i64(b);
                return ia < ib ? -1 : (ia > ib ? 1 : 0);
            }
            double da = ta == RT_BOX_F64  ? rt_unbox_f64(a)
                        : ta == RT_BOX_I1 ? (double)rt_unbox_i1(a)
                                          : (double)rt_unbox_i64(a);
            double db = tb == RT_BOX_F64  ? rt_unbox_f64(b)
                        : tb == RT_BOX_I1 ? (double)rt_unbox_i1(b)
                                          : (double)rt_unbox_i64(b);
            // NaN sorts after every number and equal to another NaN.
            int na = isnan(da), nb = isnan(db);
            if (na || nb)
                return na == nb ? 0 : (na ? 1 : -1);
            return da < db ? -1 : (da > db ? 1 : 0);
        }
        case 2: {
            rt_string sa = rt_string_is_handle(a) ? (rt_string)a : rt_unbox_str(a);
            rt_string sb = rt_string_is_handle(b) ? (rt_string)b : rt_unbox_str(b);
            int64_t cmp = rt_str_cmp(sa, sb);
            if (!rt_string_is_handle(a))
                rt_str_release_maybe(sa);
            if (!rt_string_is_handle(b))
                rt_str_release_maybe(sb);
            return cmp;
        }
        default: {
            // Arbitrary-but-consistent order for other objects: compare the
            // pointers after conversion to uintptr_t, which is well defined.
            uintptr_t ua = (uintptr_t)a;
            uintptr_t ub = (uintptr_t)b;
            return ua < ub ? -1 : (ua > ub ? 1 : 0);
        }
    }
}

int8_t rt_box_equal(void *a, void *b) {
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    if (!is_boxed(a) || !is_boxed(b))
        return 0;

    rt_box_t *ba = (rt_box_t *)a;
    rt_box_t *bb = (rt_box_t *)b;
    if (ba->tag != bb->tag)
        return 0;

    switch (ba->tag) {
        case RT_BOX_I64:
        case RT_BOX_I1:
            return ba->data.i64_val == bb->data.i64_val;
        case RT_BOX_F64:
            if (isnan(ba->data.f64_val) && isnan(bb->data.f64_val))
                return 1;
            return ba->data.f64_val == bb->data.f64_val;
        case RT_BOX_STR:
            if (!ba->data.str_val || !bb->data.str_val)
                return ba->data.str_val == bb->data.str_val;
            return rt_str_eq(ba->data.str_val, bb->data.str_val) != 0;
        default:
            return 0;
    }
}
