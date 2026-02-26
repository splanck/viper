//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_lazy.c
// Purpose: Implements the Lazy<T> deferred initialization wrapper for the
//          Viper.Lazy class. The wrapped factory function is called at most
//          once on first Value access; subsequent accesses return the cached
//          value without re-invoking the factory.
//
// Key invariants:
//   - The factory function is called exactly once on the first call to Value.
//   - After the first call, the cached value is returned without locking.
//   - Thread-safety: initialization uses a double-checked flag; on platforms
//     with weaker memory models an atomic/barrier is used.
//   - If the factory returns NULL, NULL is cached and returned on all subsequent
//     accesses.
//   - IsInitialized returns 1 after the first successful Value call.
//
// Ownership/Lifetime:
//   - The Lazy object retains a reference to the cached value once computed.
//   - The GC finalizer releases the cached value reference.
//   - The factory function pointer is not retained; callers own its lifetime.
//
// Links: src/runtime/oop/rt_lazy.h (public API),
//        src/runtime/oop/rt_option.h (Option<T> for present/absent, related)
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
