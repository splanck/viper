//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_option.c
// Purpose: Option type implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_option.h"
#include "rt_result.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef enum
{
    OPTION_SOME = 0,
    OPTION_NONE = 1
} OptionVariant;

typedef enum
{
    VALUE_PTR = 0,
    VALUE_STR = 1,
    VALUE_I64 = 2,
    VALUE_F64 = 3
} ValueType;

typedef struct
{
    OptionVariant variant;
    ValueType value_type;
    union
    {
        void *ptr;
        rt_string str;
        int64_t i64;
        double f64;
    } value;
} Option;

//=============================================================================
// Option Creation
//=============================================================================

void *rt_option_some(void *value)
{
    Option *o = (Option *)malloc(sizeof(Option));
    if (!o)
        return NULL;
    o->variant = OPTION_SOME;
    o->value_type = VALUE_PTR;
    o->value.ptr = value;
    return o;
}

void *rt_option_some_str(rt_string value)
{
    Option *o = (Option *)malloc(sizeof(Option));
    if (!o)
        return NULL;
    o->variant = OPTION_SOME;
    o->value_type = VALUE_STR;
    o->value.str = value;
    return o;
}

void *rt_option_some_i64(int64_t value)
{
    Option *o = (Option *)malloc(sizeof(Option));
    if (!o)
        return NULL;
    o->variant = OPTION_SOME;
    o->value_type = VALUE_I64;
    o->value.i64 = value;
    return o;
}

void *rt_option_some_f64(double value)
{
    Option *o = (Option *)malloc(sizeof(Option));
    if (!o)
        return NULL;
    o->variant = OPTION_SOME;
    o->value_type = VALUE_F64;
    o->value.f64 = value;
    return o;
}

void *rt_option_none(void)
{
    Option *o = (Option *)malloc(sizeof(Option));
    if (!o)
        return NULL;
    o->variant = OPTION_NONE;
    o->value_type = VALUE_PTR;
    o->value.ptr = NULL;
    return o;
}

//=============================================================================
// Option Inspection
//=============================================================================

int8_t rt_option_is_some(void *obj)
{
    if (!obj)
        return 0;
    Option *o = (Option *)obj;
    return o->variant == OPTION_SOME ? 1 : 0;
}

int8_t rt_option_is_none(void *obj)
{
    if (!obj)
        return 1; // Treat NULL as None
    Option *o = (Option *)obj;
    return o->variant == OPTION_NONE ? 1 : 0;
}

//=============================================================================
// Value Extraction
//=============================================================================

static void trap_with_message(const char *msg)
{
    fprintf(stderr, "Option trap: %s\n", msg);
    abort();
}

void *rt_option_unwrap(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    return o->value.ptr;
}

rt_string rt_option_unwrap_str(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    if (o->value_type != VALUE_STR)
        trap_with_message("Unwrap string called on non-string Option");
    return o->value.str;
}

int64_t rt_option_unwrap_i64(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    if (o->value_type != VALUE_I64)
        trap_with_message("Unwrap i64 called on non-i64 Option");
    return o->value.i64;
}

double rt_option_unwrap_f64(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    if (o->value_type != VALUE_F64)
        trap_with_message("Unwrap f64 called on non-f64 Option");
    return o->value.f64;
}

void *rt_option_unwrap_or(void *obj, void *def)
{
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    return o->value.ptr;
}

rt_string rt_option_unwrap_or_str(void *obj, rt_string def)
{
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    if (o->value_type != VALUE_STR)
        return def;
    return o->value.str;
}

int64_t rt_option_unwrap_or_i64(void *obj, int64_t def)
{
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    if (o->value_type != VALUE_I64)
        return def;
    return o->value.i64;
}

double rt_option_unwrap_or_f64(void *obj, double def)
{
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    if (o->value_type != VALUE_F64)
        return def;
    return o->value.f64;
}

void *rt_option_value(void *obj)
{
    if (!obj)
        return NULL;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return NULL;
    return o->value.ptr;
}

//=============================================================================
// Expect
//=============================================================================

void *rt_option_expect(void *obj, rt_string msg)
{
    if (!obj)
    {
        fprintf(stderr, "Option expect: %s (NULL Option)\n", rt_string_cstr(msg));
        abort();
    }
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
    {
        fprintf(stderr, "Option expect: %s\n", rt_string_cstr(msg));
        abort();
    }
    return o->value.ptr;
}

//=============================================================================
// Transformation
//=============================================================================

void *rt_option_map(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return rt_option_none();

    if (o->value_type == VALUE_PTR)
    {
        void *new_val = fn(o->value.ptr);
        return rt_option_some(new_val);
    }
    return obj;
}

void *rt_option_and_then(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return rt_option_none();

    if (o->value_type == VALUE_PTR)
    {
        return fn(o->value.ptr);
    }
    return obj;
}

void *rt_option_or_else(void *obj, void *(*fn)(void))
{
    if (!obj)
        return fn ? fn() : rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant == OPTION_SOME)
        return obj;
    return fn ? fn() : rt_option_none();
}

void *rt_option_filter(void *obj, int8_t (*pred)(void *))
{
    if (!obj || !pred)
        return rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return rt_option_none();

    if (o->value_type == VALUE_PTR && pred(o->value.ptr))
    {
        return obj;
    }
    return rt_option_none();
}

//=============================================================================
// Conversion
//=============================================================================

void *rt_option_ok_or(void *obj, void *err)
{
    if (!obj)
        return rt_result_err(err);
    Option *o = (Option *)obj;
    if (o->variant == OPTION_SOME)
    {
        switch (o->value_type)
        {
        case VALUE_PTR:
            return rt_result_ok(o->value.ptr);
        case VALUE_STR:
            return rt_result_ok_str(o->value.str);
        case VALUE_I64:
            return rt_result_ok_i64(o->value.i64);
        case VALUE_F64:
            return rt_result_ok_f64(o->value.f64);
        }
    }
    return rt_result_err(err);
}

void *rt_option_ok_or_str(void *obj, rt_string err)
{
    if (!obj)
        return rt_result_err_str(err);
    Option *o = (Option *)obj;
    if (o->variant == OPTION_SOME)
    {
        switch (o->value_type)
        {
        case VALUE_PTR:
            return rt_result_ok(o->value.ptr);
        case VALUE_STR:
            return rt_result_ok_str(o->value.str);
        case VALUE_I64:
            return rt_result_ok_i64(o->value.i64);
        case VALUE_F64:
            return rt_result_ok_f64(o->value.f64);
        }
    }
    return rt_result_err_str(err);
}

//=============================================================================
// Utility
//=============================================================================

int8_t rt_option_equals(void *a, void *b)
{
    if (a == b)
        return 1;

    // Both NULL = equal (both "None-like")
    if (!a && !b)
        return 1;
    if (!a || !b)
    {
        // One NULL, one not - check if the non-NULL is None
        Option *non_null = a ? (Option *)a : (Option *)b;
        return non_null->variant == OPTION_NONE ? 1 : 0;
    }

    Option *oa = (Option *)a;
    Option *ob = (Option *)b;

    if (oa->variant != ob->variant)
        return 0;
    if (oa->variant == OPTION_NONE)
        return 1; // Both None

    if (oa->value_type != ob->value_type)
        return 0;

    switch (oa->value_type)
    {
    case VALUE_PTR:
        return oa->value.ptr == ob->value.ptr ? 1 : 0;
    case VALUE_STR:
        return rt_str_cmp(oa->value.str, ob->value.str) == 0 ? 1 : 0;
    case VALUE_I64:
        return oa->value.i64 == ob->value.i64 ? 1 : 0;
    case VALUE_F64:
        return oa->value.f64 == ob->value.f64 ? 1 : 0;
    }
    return 0;
}

rt_string rt_option_to_string(void *obj)
{
    if (!obj)
        return rt_const_cstr("None");

    Option *o = (Option *)obj;
    char buf[256];

    if (o->variant == OPTION_NONE)
    {
        return rt_const_cstr("None");
    }

    switch (o->value_type)
    {
    case VALUE_PTR:
        snprintf(buf, sizeof(buf), "Some(%p)", o->value.ptr);
        break;
    case VALUE_STR:
        snprintf(buf, sizeof(buf), "Some(\"%s\")", rt_string_cstr(o->value.str));
        break;
    case VALUE_I64:
        snprintf(buf, sizeof(buf), "Some(%lld)", (long long)o->value.i64);
        break;
    case VALUE_F64:
        snprintf(buf, sizeof(buf), "Some(%g)", o->value.f64);
        break;
    }

    return rt_string_from_bytes(buf, (int64_t)strlen(buf));
}
