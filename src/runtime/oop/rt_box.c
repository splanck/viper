//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_box.c
// Purpose: Implements boxing and unboxing primitives that wrap scalar values
//          (i64, f64, bool, string, pointer) into heap-allocated objects for
//          use in generic collections (Seq, Map, List). Each boxed value
//          carries a type tag for runtime type discrimination.
//
// Key invariants:
//   - Boxed values are heap-allocated via rt_heap_alloc and reference-counted.
//   - Type tags (BOX_TYPE_I64, BOX_TYPE_F64, BOX_TYPE_STR, etc.) uniquely
//     identify the contained type.
//   - Unboxing to the wrong type returns a safe default (0/NULL) not a trap.
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
#include "rt_platform.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#include <windows.h>
#elif !RT_PLATFORM_VIPERDOS
#include <sched.h>
#endif

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

typedef struct value_type_layout {
    void *obj;
    value_type_field *fields;
    struct value_type_layout *next;
} value_type_layout;

static value_type_layout *g_value_type_layouts = NULL;
static int g_value_type_layout_lock = 0;

static void value_type_lock(void) {
    if (__atomic_test_and_set(&g_value_type_layout_lock, __ATOMIC_ACQUIRE)) {
        do {
#if RT_PLATFORM_WINDOWS
            SwitchToThread();
#elif !RT_PLATFORM_VIPERDOS
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
        if (rt_obj_release_check0(child))
            rt_obj_free(child);
        *slot = NULL;
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

static void value_type_finalizer(void *obj) {
    value_type_lock();
    value_type_layout *layout = value_type_detach_locked(obj);
    value_type_unlock();
    if (!layout)
        return;
    for (value_type_field *field = layout->fields; field; field = field->next)
        value_type_release_slot(obj, field);
    value_type_free_layout(layout);
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
    value_type_unlock();
    if (count <= 0)
        return;

    size_t *offsets = (size_t *)malloc((size_t)count * sizeof(size_t));
    if (!offsets)
        return;

    int64_t copied = 0;
    value_type_lock();
    layout = value_type_find_locked(obj);
    for (value_type_field *field = layout ? layout->fields : NULL; field && copied < count;
         field = field->next) {
        if (field->kind == RT_VALUE_FIELD_OBJ)
            offsets[copied++] = field->offset;
    }
    value_type_unlock();

    for (int64_t i = 0; i < copied; ++i) {
        void *child = *(void **)((unsigned char *)obj + offsets[i]);
        if (child)
            visitor(child, ctx);
    }
    free(offsets);
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
    if (!hdr || hdr->elem_kind != RT_ELEM_BOX)
        return NULL;
    return (rt_box_t *)box;
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
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_STR;
    box->data.str_val = val;
    // Retain the string since we're storing it
    // Use rt_string_ref to handle both heap and literal strings
    if (val) {
        rt_string_ref(val);
    }
    rt_obj_set_finalizer(box, box_str_finalizer);
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

/// @brief Extract the bool contents (returned as 0/1 in i64). **Traps** on tag mismatch.
int64_t rt_unbox_i1(void *box) {
    rt_box_t *b = box_require(box, "rt_unbox_i1", RT_BOX_I1);
    if (!b)
        return 0;
    return b->data.i64_val;
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
    rt_box_t *b = box_maybe(box);
    if (!b)
        return 0;
    if (b->tag != RT_BOX_STR)
        return 0;
    return rt_str_eq(b->data.str_val, val);
}

/// @brief Allocate a raw heap region of `size` bytes for a Zia value-type instance (struct).
/// Distinct from the tagged Box family — this isn't a Box at all (RT_ELEM_NONE), the compiler
/// emits direct field copies into the returned memory. Returns NULL if `size <= 0`.
void *rt_box_value_type(int64_t size) {
    if (size <= 0)
        return NULL;
    if ((uint64_t)size > (uint64_t)SIZE_MAX) {
        rt_trap("rt_box_value_type: size too large");
        return NULL;
    }
    return rt_obj_new_i64(RT_VALUE_TYPE_CLASS_ID, size);
}

void rt_box_value_type_add_field(void *obj, int64_t offset, int64_t kind, int8_t retain_now) {
    if (!obj) {
        rt_trap("rt_box_value_type_add_field: null value type");
        return;
    }
    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(obj, &hdr) || !hdr ||
        (rt_heap_kind_t)hdr->kind != RT_HEAP_OBJECT || hdr->class_id != RT_VALUE_TYPE_CLASS_ID) {
        rt_trap("rt_box_value_type_add_field: invalid value type object");
        return;
    }
    if (offset < 0 || (uint64_t)offset > (uint64_t)SIZE_MAX ||
        (size_t)offset > hdr->cap || hdr->cap - (size_t)offset < sizeof(void *)) {
        rt_trap("rt_box_value_type_add_field: field offset out of range");
        return;
    }
    if (kind != RT_VALUE_FIELD_OBJ && kind != RT_VALUE_FIELD_STR) {
        rt_trap("rt_box_value_type_add_field: invalid field kind");
        return;
    }

    value_type_field *field = (value_type_field *)calloc(1, sizeof(value_type_field));
    value_type_layout *new_layout = (value_type_layout *)calloc(1, sizeof(value_type_layout));
    if (!field || !new_layout) {
        free(field);
        free(new_layout);
        rt_trap("rt_box_value_type_add_field: memory allocation failed");
        return;
    }
    field->offset = (size_t)offset;
    field->kind = kind;
    value_type_field retain_field = {field->offset, field->kind, NULL};

    int installed_layout = 0;
    int inserted_field = 0;
    value_type_lock();
    value_type_layout *layout = value_type_find_locked(obj);
    if (!layout) {
        new_layout->obj = obj;
        new_layout->next = g_value_type_layouts;
        g_value_type_layouts = new_layout;
        layout = new_layout;
        new_layout = NULL;
        installed_layout = 1;
    }
    int duplicate = 0;
    for (value_type_field *existing = layout->fields; existing; existing = existing->next) {
        if (existing->offset == field->offset && existing->kind == field->kind) {
            duplicate = 1;
            break;
        }
    }
    if (!duplicate) {
        field->next = layout->fields;
        layout->fields = field;
        field = NULL;
        inserted_field = 1;
    }
    value_type_unlock();

    free(field);
    free(new_layout);

    if (installed_layout) {
        rt_obj_set_finalizer(obj, value_type_finalizer);
        rt_gc_track(obj, value_type_traverse);
    }
    if (inserted_field && retain_now)
        value_type_retain_slot(obj, &retain_field);
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
            case RT_BOX_F64:
            {
                double value = box->data.f64_val;
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
            return ba->data.f64_val == bb->data.f64_val;
        case RT_BOX_STR:
            return rt_str_eq(ba->data.str_val, bb->data.str_val) != 0;
        default:
            return 0;
    }
}
