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
#include "rt_error.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef enum { RESULT_OK = 0, RESULT_ERR = 1 } ResultVariant;

typedef enum { VALUE_PTR = 0, VALUE_STR = 1, VALUE_I64 = 2, VALUE_F64 = 3 } ValueType;

typedef struct {
    ResultVariant variant;
    ValueType value_type;

    union {
        void *ptr;
        rt_string str;
        int64_t i64;
        double f64;
    } value;
} Result;

//=============================================================================
// Result Finalizer
//=============================================================================

/// @brief GC finalizer: release the heap-owned payload (PTR via object refcount, STR via
/// `rt_str_release_maybe`). I64/F64 variants own no heap memory and need no cleanup.
static void result_finalizer(void *obj) {
    Result *r = (Result *)obj;
    if (!r)
        return;
    if (r->value_type == VALUE_PTR && r->value.ptr) {
        if (rt_obj_release_check0(r->value.ptr))
            rt_obj_free(r->value.ptr);
        r->value.ptr = NULL;
    } else if (r->value_type == VALUE_STR && r->value.str) {
        rt_str_release_maybe(r->value.str);
        r->value.str = NULL;
    }
}

//=============================================================================
// Result Creation
//=============================================================================

/// @brief Construct `Ok(ptr)` over a generic pointer payload. Retains `value` via the heap path.
void *rt_result_ok(void *value) {
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_PTR;
    r->value.ptr = value;
    rt_obj_retain_maybe(value);
    rt_obj_set_finalizer(r, result_finalizer);
    return r;
}

/// @brief Construct `Ok(string)` — retains the string (heap or literal) via `rt_string_ref`.
void *rt_result_ok_str(rt_string value) {
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_STR;
    r->value.str = value ? rt_string_ref(value) : NULL;
    rt_obj_set_finalizer(r, result_finalizer);
    return r;
}

/// @brief Construct `Ok(i64)` with the value stored inline (no heap allocation for payload).
void *rt_result_ok_i64(int64_t value) {
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_I64;
    r->value.i64 = value;
    rt_obj_set_finalizer(r, result_finalizer);
    return r;
}

/// @brief Construct `Ok(f64)` with the value stored inline.
void *rt_result_ok_f64(double value) {
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_OK;
    r->value_type = VALUE_F64;
    r->value.f64 = value;
    rt_obj_set_finalizer(r, result_finalizer);
    return r;
}

/// @brief Construct `Err(ptr)` carrying an arbitrary heap-managed error value (e.g. an exception
/// object). Retains the error so it survives until the Result is finalized.
void *rt_result_err(void *error) {
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_ERR;
    r->value_type = VALUE_PTR;
    r->value.ptr = error;
    rt_obj_retain_maybe(error);
    rt_obj_set_finalizer(r, result_finalizer);
    return r;
}

/// @brief Construct `Err(message)` with a string error description. The most common Err shape —
/// caller-friendly diagnostic that doesn't require allocating a separate error class.
void *rt_result_err_str(rt_string message) {
    Result *r = (Result *)rt_obj_new_i64(0, (int64_t)sizeof(Result));
    r->variant = RESULT_ERR;
    r->value_type = VALUE_STR;
    r->value.str = message ? rt_string_ref(message) : NULL;
    rt_obj_set_finalizer(r, result_finalizer);
    return r;
}

//=============================================================================
// Result Inspection
//=============================================================================

/// @brief Check whether the Result is the Ok variant (operation succeeded).
int8_t rt_result_is_ok(void *obj) {
    if (!obj)
        return 0;
    Result *r = (Result *)obj;
    return r->variant == RESULT_OK ? 1 : 0;
}

/// @brief Check whether the Result is the Err variant (operation failed).
int8_t rt_result_is_err(void *obj) {
    if (!obj)
        return 0;
    Result *r = (Result *)obj;
    return r->variant == RESULT_ERR ? 1 : 0;
}

//=============================================================================
// Value Extraction
//=============================================================================

#include "rt_trap.h"

/// @brief Convenience wrapper around `rt_trap` so unwrap helpers stay readable.
static void trap_with_message(const char *msg) {
    rt_trap(msg);
}

/// @brief Extract the Ok pointer payload; **traps** if NULL or Err. Use after `is_ok()` check.
void *rt_result_unwrap(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    return r->value.ptr;
}

/// @brief Extract the string value from an Ok result; traps if Err or wrong type.
rt_string rt_result_unwrap_str(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    if (r->value_type != VALUE_STR)
        trap_with_message("Unwrap string called on non-string Result");
    return r->value.str;
}

/// @brief Extract the i64 value from an Ok result; traps if Err or wrong type.
int64_t rt_result_unwrap_i64(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    if (r->value_type != VALUE_I64)
        trap_with_message("Unwrap i64 called on non-i64 Result");
    return r->value.i64;
}

/// @brief Extract the f64 value from an Ok result; traps if Err or wrong type.
double rt_result_unwrap_f64(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        trap_with_message("Unwrap called on Err Result");
    if (r->value_type != VALUE_F64)
        trap_with_message("Unwrap f64 called on non-f64 Result");
    return r->value.f64;
}

/// @brief Return the Ok pointer if present, else `def`. Never traps; NULL handle treated as Err.
void *rt_result_unwrap_or(void *obj, void *def) {
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    return r->value.ptr;
}

/// @brief Unwrap the or str of the result.
rt_string rt_result_unwrap_or_str(void *obj, rt_string def) {
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    if (r->value_type != VALUE_STR)
        return def;
    return r->value.str;
}

/// @brief Unwrap the or i64 of the result.
int64_t rt_result_unwrap_or_i64(void *obj, int64_t def) {
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    if (r->value_type != VALUE_I64)
        return def;
    return r->value.i64;
}

/// @brief Unwrap the or f64 of the result.
double rt_result_unwrap_or_f64(void *obj, double def) {
    if (!obj)
        return def;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return def;
    if (r->value_type != VALUE_F64)
        return def;
    return r->value.f64;
}

/// @brief Extract the Err pointer payload; **traps** if NULL or Ok. Mirror of `unwrap` for the
/// Err side — used to inspect the error after `is_err()` confirms one is present.
void *rt_result_unwrap_err(void *obj) {
    if (!obj)
        trap_with_message("UnwrapErr called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        trap_with_message("UnwrapErr called on Ok Result");
    return r->value.ptr;
}

/// @brief Unwrap the err str of the result.
rt_string rt_result_unwrap_err_str(void *obj) {
    if (!obj)
        trap_with_message("UnwrapErr called on NULL Result");
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        trap_with_message("UnwrapErr called on Ok Result");
    if (r->value_type != VALUE_STR)
        trap_with_message("UnwrapErr string called on non-string Result");
    return r->value.str;
}

/// @brief Return the Ok pointer if Ok, NULL otherwise. Non-trapping accessor — caller must
/// distinguish "stored NULL" from "this is an Err" via `is_ok` / `is_err`.
void *rt_result_ok_value(void *obj) {
    if (!obj)
        return NULL;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return NULL;
    return r->value.ptr;
}

/// @brief Return the Err pointer if Err, NULL otherwise. Non-trapping companion to `ok_value`.
void *rt_result_err_value(void *obj) {
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

/// @brief Like `unwrap` with a caller-supplied diagnostic. Traps with INVALID_OPERATION (catchable
/// distinctly from generic unwrap traps) on Err. Use for "this Result must be Ok at this point"
/// invariant checks where the message helps debug failures.
void *rt_result_expect(void *obj, rt_string msg) {
    const char *msg_str = msg ? rt_string_cstr(msg) : "assertion failed";
    char buffer[256];
    if (!obj) {
        snprintf(buffer, sizeof(buffer), "Result expect: %s (NULL Result)", msg_str);
        rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    }
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK) {
        snprintf(buffer, sizeof(buffer), "Result expect: %s", msg_str);
        rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    }
    return r->value.ptr;
}

/// @brief Mirror of `expect` for the Err side: traps with the diagnostic if the Result is Ok.
/// Useful in tests asserting that a fallible operation must have failed with a specific reason.
void *rt_result_expect_err(void *obj, rt_string msg) {
    const char *msg_str = msg ? rt_string_cstr(msg) : "assertion failed";
    char buffer[256];
    if (!obj) {
        snprintf(buffer, sizeof(buffer), "Result expect_err: %s (NULL Result)", msg_str);
        rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    }
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR) {
        snprintf(buffer, sizeof(buffer), "Result expect_err: %s", msg_str);
        rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    }
    return r->value.ptr;
}

//=============================================================================
// Transformation
//=============================================================================

// =============================================================================
// Transformation combinators — all PTR-variant only. Typed-primitive Results
// (STR/I64/F64) pass through unchanged because the function signature
// `void* (*)(void*)` doesn't match the union's other branches. Pair with the
// typed unwrap_*/ok_*_t constructors when you need to transform a typed value.
// =============================================================================

/// @brief Apply `fn` to the Ok value, returning `Ok(fn(val))`. Err passes through unchanged.
void *rt_result_map(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return obj;

    // For pointer values, apply the function
    if (r->value_type == VALUE_PTR) {
        void *new_val = fn(r->value.ptr);
        return rt_result_ok(new_val);
    }
    // For other types, return as-is (can't map non-pointer)
    return obj;
}

/// @brief Apply `fn` to the Err value, returning `Err(fn(err))`. Ok passes through unchanged.
/// Useful for translating low-level errors into higher-level error types.
void *rt_result_map_err(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        return obj;

    if (r->value_type == VALUE_PTR) {
        void *new_val = fn(r->value.ptr);
        return rt_result_err(new_val);
    }
    return obj;
}

/// @brief Monadic bind on the Ok side: apply `fn` (returning a Result) to the Ok value, flattening.
/// Err short-circuits the chain, propagating unchanged. The cornerstone of error pipelines.
void *rt_result_and_then(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_OK)
        return obj;

    if (r->value_type == VALUE_PTR) {
        return fn(r->value.ptr);
    }
    return obj;
}

/// @brief Monadic bind on the Err side: apply `fn` (returning a Result) to the Err value to
/// produce a recovery Result. Ok passes through unchanged. Used for "try recovery" patterns.
void *rt_result_or_else(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return obj;
    Result *r = (Result *)obj;
    if (r->variant != RESULT_ERR)
        return obj;

    if (r->value_type == VALUE_PTR) {
        return fn(r->value.ptr);
    }
    return obj;
}

/// @brief IL trampoline for `rt_result_map` — re-types the user fn pointer for the typed call.
void *rt_result_map_wrapper(void *obj, void *fn) {
    return rt_result_map(obj, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_result_map_err`.
void *rt_result_map_err_wrapper(void *obj, void *fn) {
    return rt_result_map_err(obj, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_result_and_then`.
void *rt_result_and_then_wrapper(void *obj, void *fn) {
    return rt_result_and_then(obj, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_result_or_else`.
void *rt_result_or_else_wrapper(void *obj, void *fn) {
    return rt_result_or_else(obj, (void *(*)(void *))fn);
}

//=============================================================================
// Utility
//=============================================================================

/// @brief Compare two result instances for structural equality.
int8_t rt_result_equals(void *a, void *b) {
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

    switch (ra->value_type) {
        case VALUE_PTR:
            return ra->value.ptr == rb->value.ptr ? 1 : 0;
        case VALUE_STR:
            return rt_str_cmp(ra->value.str, rb->value.str) == 0 ? 1 : 0;
        case VALUE_I64:
            return ra->value.i64 == rb->value.i64 ? 1 : 0;
        case VALUE_F64:
            // IEEE 754: NaN != NaN, so Result(NaN).Equals(Result(NaN)) returns 0.
            return ra->value.f64 == rb->value.f64 ? 1 : 0;
    }
    return 0;
}

/// @note to_string output is truncated to 256 characters for long string values.
rt_string rt_result_to_string(void *obj) {
    if (!obj)
        return rt_const_cstr("Result(null)");

    Result *r = (Result *)obj;
    char buf[256];

    if (r->variant == RESULT_OK) {
        switch (r->value_type) {
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
    } else {
        switch (r->value_type) {
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

    return rt_string_from_bytes(buf, strlen(buf));
}
