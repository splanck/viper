//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_result.c
// Purpose: Implements the Result<T,E> type (Ok/Err) for the Viper.Result class.
//          Wraps either a success value or an error value as a heap-allocated
//          object, providing an alternative to exceptions for error propagation.
//
// Key invariants:
//   - Result.Ok(val) stores the value with is_ok=1; Err is not set.
//   - Result.Err(err) stores the error with is_ok=0; value is not set.
//   - IsOk() returns 1 for Ok results; IsErr() returns 1 for Err results.
//   - Value() returns the ok value if IsOk; traps if called on Err.
//   - Error() returns the error if IsErr; traps if called on Ok.
//   - Exactly one of (value, error) is set; the other is NULL.
//
// Ownership/Lifetime:
//   - The Result retains references to both the value and error slots.
//   - The GC finalizer releases whichever reference is set.
//   - Callers receive a fresh Result reference (refcount=1).
//
// Links: src/runtime/oop/rt_result.h (public API),
//        src/runtime/oop/rt_option.h (Option<T>, related present/absent pattern)
//
//===----------------------------------------------------------------------===//

#include "rt_result.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef enum
{
    RESULT_OK = 0,
    RESULT_ERR = 1
} ResultVariant;

typedef enum
{
    VALUE_PTR = 0,
    VALUE_STR = 1,
    VALUE_I64 = 2,
    VALUE_F64 = 3
} ValueType;

typedef struct
{
    ResultVariant variant;
    ValueType value_type;

    union
    {
        void *ptr;
        rt_string str;
        int64_t i64;
        double f64;
    } value;
} Result;

//=============================================================================
// Result Creation
//=============================================================================

void *rt_result_ok(void *value)
{
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_PTR;
    r->value.ptr = value;
    return r;
}

void *rt_result_ok_str(rt_string value)
{
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_STR;
    r->value.str = value;
    return r;
}

void *rt_result_ok_i64(int64_t value)
{
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_I64;
    r->value.i64 = value;
    return r;
}

void *rt_result_ok_f64(double value)
{
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_F64;
    r->value.f64 = value;
    return r;
}

void *rt_result_err(void *error)
{
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_ERR;
    r->value_type = VALUE_PTR;
    r->value.ptr = error;
    return r;
}

void *rt_result_err_str(rt_string message)
{
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_ERR;
    r->value_type = VALUE_STR;
    r->value.str = message;
    return r;
}

//=============================================================================
// Result Inspection
//=============================================================================

int8_t rt_result_is_ok(void *obj)
{
    if (!obj)
        return 0;
    Result *r = (Result *)obj;
    return r->variant == RESULT_OK ? 1 : 0;
}

int8_t rt_result_is_err(void *obj)
{
    if (!obj)
        return 0;
    Result *r = (Result *)obj;
    return r->variant == RESULT_ERR ? 1 : 0;
}

//=============================================================================
// Value Extraction
//=============================================================================

static void trap_with_message(const char *msg)
{
    fprintf(stderr, "Result trap: %s\n", msg);
    abort();
}

void *rt_result_unwrap(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    return r->value.ptr;
}

rt_string rt_result_unwrap_str(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    if (r->value_type != VALUE_STR)
        trap_with_message("Unwrap string called on non-string Result");
    return r->value.str;
}

int64_t rt_result_unwrap_i64(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    if (r->value_type != VALUE_I64)
        trap_with_message("Unwrap i64 called on non-i64 Result");
    return r->value.i64;
}

double rt_result_unwrap_f64(void *obj)
{
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    if (r->value_type != VALUE_F64)
        trap_with_message("Unwrap f64 called on non-f64 Result");
    return r->value.f64;
}

void *rt_result_unwrap_or(void *obj, void *def)
{
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    return r->value.ptr;
}

rt_string rt_result_unwrap_or_str(void *obj, rt_string def)
{
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    if (r->value_type != VALUE_STR)
        return def;
    return r->value.str;
}

int64_t rt_result_unwrap_or_i64(void *obj, int64_t def)
{
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    if (r->value_type != VALUE_I64)
        return def;
    return r->value.i64;
}

double rt_result_unwrap_or_f64(void *obj, double def)
{
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    if (r->value_type != VALUE_F64)
        return def;
    return r->value.f64;
}

void *rt_result_unwrap_err(void *obj)
{
    if (!obj)
        trap_with_message("UnwrapErr called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        trap_with_message("UnwrapErr called on Ok Result");
    return r->value.ptr;
}

rt_string rt_result_unwrap_err_str(void *obj)
{
    if (!obj)
        trap_with_message("UnwrapErr called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        trap_with_message("UnwrapErr called on Ok Result");
    if (r->value_type != VALUE_STR)
        trap_with_message("UnwrapErr string called on non-string Result");
    return r->value.str;
}

void *rt_result_ok_value(void *obj)
{
    if (!obj)
        return NULL;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return NULL;
    return r->value.ptr;
}

void *rt_result_err_value(void *obj)
{
    if (!obj)
        return NULL;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        return NULL;
    return r->value.ptr;
}

//=============================================================================
// Expect
//=============================================================================

void *rt_result_expect(void *obj, rt_string msg)
{
    const char *msg_str = msg ? rt_string_cstr(msg) : "assertion failed";
    if (!obj)
    {
        fprintf(stderr, "Result expect: %s (NULL Result)\n", msg_str);
        abort();
    }
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
    {
        fprintf(stderr, "Result expect: %s\n", msg_str);
        abort();
    }
    return r->value.ptr;
}

void *rt_result_expect_err(void *obj, rt_string msg)
{
    const char *msg_str = msg ? rt_string_cstr(msg) : "assertion failed";
    if (!obj)
    {
        fprintf(stderr, "Result expect_err: %s (NULL Result)\n", msg_str);
        abort();
    }
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
    {
        fprintf(stderr, "Result expect_err: %s\n", msg_str);
        abort();
    }
    return r->value.ptr;
}

//=============================================================================
// Transformation
//=============================================================================

void *rt_result_map(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return obj;

    // For pointer values, apply the function
    if (r->value_type == VALUE_PTR)
    {
        void *new_val = fn(r->value.ptr);
        return rt_result_ok(new_val);
    }
    // For other types, return as-is (can't map non-pointer)
    return obj;
}

void *rt_result_map_err(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        return obj;

    if (r->value_type == VALUE_PTR)
    {
        void *new_val = fn(r->value.ptr);
        return rt_result_err(new_val);
    }
    return obj;
}

void *rt_result_and_then(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return obj;

    if (r->value_type == VALUE_PTR)
    {
        return fn(r->value.ptr);
    }
    return obj;
}

void *rt_result_or_else(void *obj, void *(*fn)(void *))
{
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        return obj;

    if (r->value_type == VALUE_PTR)
    {
        return fn(r->value.ptr);
    }
    return obj;
}

//=============================================================================
// Utility
//=============================================================================

int8_t rt_result_equals(void *a, void *b)
{
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;

    Result *ra = (Result *)a;
    Result *rb = (Result *)b;

    if (ra->variant != rb->variant)
        return 0;
    if (ra->value_type != rb->value_type)
        return 0;

    switch (ra->value_type)
    {
        case VALUE_PTR:
            return ra->value.ptr == rb->value.ptr ? 1 : 0;
        case VALUE_STR:
            return rt_str_cmp(ra->value.str, rb->value.str) == 0 ? 1 : 0;
        case VALUE_I64:
            return ra->value.i64 == rb->value.i64 ? 1 : 0;
        case VALUE_F64:
            return ra->value.f64 == rb->value.f64 ? 1 : 0;
    }
    return 0;
}

rt_string rt_result_to_string(void *obj)
{
    if (!obj)
        return rt_const_cstr("Result(null)");

    Result *r = (Result *)obj;
    char buf[256];

    if (r->variant == RESULT_OK)
    {
        switch (r->value_type)
        {
            case VALUE_PTR:
                snprintf(buf, sizeof(buf), "Ok(%p)", r->value.ptr);
                break;
            case VALUE_STR:
                snprintf(buf, sizeof(buf), "Ok(\"%s\")", rt_string_cstr(r->value.str));
                break;
            case VALUE_I64:
                snprintf(buf, sizeof(buf), "Ok(%lld)", (long long)r->value.i64);
                break;
            case VALUE_F64:
                snprintf(buf, sizeof(buf), "Ok(%g)", r->value.f64);
                break;
        }
    }
    else
    {
        switch (r->value_type)
        {
            case VALUE_PTR:
                snprintf(buf, sizeof(buf), "Err(%p)", r->value.ptr);
                break;
            case VALUE_STR:
                snprintf(buf, sizeof(buf), "Err(\"%s\")", rt_string_cstr(r->value.str));
                break;
            case VALUE_I64:
                snprintf(buf, sizeof(buf), "Err(%lld)", (long long)r->value.i64);
                break;
            case VALUE_F64:
                snprintf(buf, sizeof(buf), "Err(%g)", r->value.f64);
                break;
        }
    }

    return rt_string_from_bytes(buf, (int64_t)strlen(buf));
}
