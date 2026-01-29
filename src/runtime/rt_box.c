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
    return rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, sizeof(rt_box_t), sizeof(rt_box_t));
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
