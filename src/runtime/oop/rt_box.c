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
#include "rt_hash_util.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <stdio.h>
#include <string.h>

/// Internal structure for boxed values
typedef struct rt_box {
    int64_t tag;

    union {
        int64_t i64_val;
        double f64_val;
        rt_string str_val;
    } data;
} rt_box_t;

/// @brief Allocate a fresh boxed-value object via the heap (refcount=1, tagged RT_ELEM_BOX so
/// `box_maybe` can later identify it). Caller fills the tag and union fields.
static void *alloc_box(void) {
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_BOX, 1, sizeof(rt_box_t), sizeof(rt_box_t));
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
    // Allocate raw memory for value type - the compiler will copy fields
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, (size_t)size, (size_t)size);
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
                return (size_t)rt_fnv1a(&box->data.f64_val, sizeof(double));
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
