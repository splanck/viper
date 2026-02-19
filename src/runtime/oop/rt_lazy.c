//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_lazy.c
// Purpose: Lazy type implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_lazy.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef enum
{
    VALUE_PTR = 0,
    VALUE_STR = 1,
    VALUE_I64 = 2
} ValueType;

typedef struct
{
    int8_t evaluated;
    ValueType value_type;
    void *(*supplier)(void);

    union
    {
        void *ptr;
        rt_string str;
        int64_t i64;
    } value;
} Lazy;

//=============================================================================
// Lazy Creation
//=============================================================================

void *rt_lazy_new(void *(*supplier)(void))
{
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 0;
    l->value_type = VALUE_PTR;
    l->supplier = supplier;
    l->value.ptr = NULL;
    return l;
}

void *rt_lazy_of(void *value)
{
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 1;
    l->value_type = VALUE_PTR;
    l->supplier = NULL;
    l->value.ptr = value;
    return l;
}

void *rt_lazy_of_str(rt_string value)
{
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 1;
    l->value_type = VALUE_STR;
    l->supplier = NULL;
    l->value.str = value;
    return l;
}

void *rt_lazy_of_i64(int64_t value)
{
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 1;
    l->value_type = VALUE_I64;
    l->supplier = NULL;
    l->value.i64 = value;
    return l;
}

//=============================================================================
// Lazy Access
//=============================================================================

static void evaluate(Lazy *l)
{
    if (l->evaluated)
        return;

    if (l->supplier)
    {
        l->value.ptr = l->supplier();
    }
    l->evaluated = 1;
}

void *rt_lazy_get(void *obj)
{
    if (!obj)
        return NULL;
    Lazy *l = (Lazy *)obj;

    evaluate(l);
    return l->value.ptr;
}

rt_string rt_lazy_get_str(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    Lazy *l = (Lazy *)obj;

    evaluate(l);

    if (l->value_type == VALUE_STR)
    {
        return l->value.str;
    }
    return rt_const_cstr("");
}

int64_t rt_lazy_get_i64(void *obj)
{
    if (!obj)
        return 0;
    Lazy *l = (Lazy *)obj;

    evaluate(l);

    if (l->value_type == VALUE_I64)
    {
        return l->value.i64;
    }
    return 0;
}

//=============================================================================
// Lazy State
//=============================================================================

int8_t rt_lazy_is_evaluated(void *obj)
{
    if (!obj)
        return 1;
    Lazy *l = (Lazy *)obj;
    return l->evaluated;
}

void rt_lazy_force(void *obj)
{
    if (!obj)
        return;
    Lazy *l = (Lazy *)obj;
    evaluate(l);
}

//=============================================================================
// Transformation
//=============================================================================

// Helper struct for map operation
typedef struct
{
    void *source_lazy;
    void *(*fn)(void *);
} MapContext;

static void *map_supplier(void *ctx)
{
    MapContext *mc = (MapContext *)ctx;
    void *source_value = rt_lazy_get(mc->source_lazy);
    return mc->fn(source_value);
}

void *rt_lazy_map(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return obj;

    Lazy *l = (Lazy *)obj;

    // If already evaluated, apply fn immediately
    if (l->evaluated)
    {
        void *new_value = fn(l->value.ptr);
        return rt_lazy_of(new_value);
    }

    // Create a new lazy that will apply fn when evaluated
    // Note: In a real implementation, we'd need proper closure support
    // For now, we'll force evaluation and apply
    void *value = rt_lazy_get(obj);
    void *new_value = fn(value);
    return rt_lazy_of(new_value);
}

void *rt_lazy_flat_map(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return obj;

    // Force evaluation of the source lazy
    void *value = rt_lazy_get(obj);

    // Call fn to get a new lazy
    void *new_lazy = fn(value);

    // Return the new lazy (which will be evaluated when needed)
    return new_lazy;
}
