//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: runtime/rt_box.c
// Purpose: Boxing/unboxing primitives for ViperLang generic collections.
// Key invariants: Boxed values are heap-allocated objects with type tags.
// Ownership/Lifetime: Boxed values participate in reference counting.
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_hash_util.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <string.h>

/// Internal structure for boxed values
typedef struct rt_box
{
    int64_t tag;

    union
    {
        int64_t i64_val;
        double f64_val;
        rt_string str_val;
    } data;
} rt_box_t;

/// Allocate a new boxed value
static void *alloc_box(void)
{
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_BOX, 1, sizeof(rt_box_t), sizeof(rt_box_t));
}

void *rt_box_i64(int64_t val)
{
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_I64;
    box->data.i64_val = val;
    return box;
}

void *rt_box_f64(double val)
{
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_F64;
    box->data.f64_val = val;
    return box;
}

void *rt_box_i1(int64_t val)
{
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_I1;
    box->data.i64_val = val ? 1 : 0;
    return box;
}

void *rt_box_str(rt_string val)
{
    rt_box_t *box = (rt_box_t *)alloc_box();
    if (!box)
        return NULL;
    box->tag = RT_BOX_STR;
    box->data.str_val = val;
    // Retain the string since we're storing it
    // Use rt_string_ref to handle both heap and literal strings
    if (val)
    {
        rt_string_ref(val);
    }
    return box;
}

int64_t rt_unbox_i64(void *box)
{
    if (!box)
    {
        rt_trap("rt_unbox_i64: null pointer");
        return 0;
    }
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_I64)
    {
        rt_trap("rt_unbox_i64: type mismatch (expected i64)");
        return 0;
    }
    return b->data.i64_val;
}

double rt_unbox_f64(void *box)
{
    if (!box)
    {
        rt_trap("rt_unbox_f64: null pointer");
        return 0.0;
    }
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_F64)
    {
        rt_trap("rt_unbox_f64: type mismatch (expected f64)");
        return 0.0;
    }
    return b->data.f64_val;
}

int64_t rt_unbox_i1(void *box)
{
    if (!box)
    {
        rt_trap("rt_unbox_i1: null pointer");
        return 0;
    }
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_I1)
    {
        rt_trap("rt_unbox_i1: type mismatch (expected i1)");
        return 0;
    }
    return b->data.i64_val;
}

rt_string rt_unbox_str(void *box)
{
    if (!box)
    {
        rt_trap("rt_unbox_str: null pointer");
        return NULL;
    }
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_STR)
    {
        rt_trap("rt_unbox_str: type mismatch (expected str)");
        return NULL;
    }
    rt_string s = b->data.str_val;
    // Retain before returning - use rt_string_ref for proper handling
    if (s)
    {
        rt_string_ref(s);
    }
    return s;
}

int64_t rt_box_type(void *box)
{
    if (!box)
        return -1;
    rt_box_t *b = (rt_box_t *)box;
    return b->tag;
}

int64_t rt_box_eq_i64(void *box, int64_t val)
{
    if (!box)
        return 0;
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_I64)
        return 0;
    return b->data.i64_val == val ? 1 : 0;
}

int64_t rt_box_eq_f64(void *box, double val)
{
    if (!box)
        return 0;
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_F64)
        return 0;
    return b->data.f64_val == val ? 1 : 0;
}

int64_t rt_box_eq_str(void *box, rt_string val)
{
    if (!box)
        return 0;
    rt_box_t *b = (rt_box_t *)box;
    if (b->tag != RT_BOX_STR)
        return 0;
    return rt_str_eq(b->data.str_val, val);
}

void *rt_box_value_type(int64_t size)
{
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
static int is_boxed(void *elem)
{
    if (!elem)
        return 0;
    // Manually compute header location without asserting magic.
    // This is safe because we check magic before accessing any other fields.
    rt_heap_hdr_t *hdr = (rt_heap_hdr_t *)((uint8_t *)elem - sizeof(rt_heap_hdr_t));
    return hdr->magic == RT_MAGIC && hdr->elem_kind == RT_ELEM_BOX;
}

size_t rt_box_hash(void *elem)
{
    if (is_boxed(elem))
    {
        rt_box_t *box = (rt_box_t *)elem;
        switch (box->tag)
        {
            case RT_BOX_I64:
            case RT_BOX_I1:
                return (size_t)rt_fnv1a(&box->data.i64_val, sizeof(int64_t));
            case RT_BOX_F64:
                return (size_t)rt_fnv1a(&box->data.f64_val, sizeof(double));
            case RT_BOX_STR:
            {
                rt_string s = box->data.str_val;
                if (!s)
                    return 0;
                const char *cstr = rt_string_cstr(s);
                if (!cstr)
                    return 0;
                return (size_t)rt_fnv1a(cstr, strlen(cstr));
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

int rt_box_equal(void *a, void *b)
{
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

    switch (ba->tag)
    {
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
