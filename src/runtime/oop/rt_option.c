//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_option.c
// Purpose: Implements the Option<T> type (Some/None) for the Viper.Option class.
//          Wraps an optional value as a heap-allocated object with a presence
//          flag, allowing functions to return "no value" without using NULL.
//
// Key invariants:
//   - Option.None() creates an option with no value (has_value == 0).
//   - Option.Some(val) creates an option holding the given void* value.
//   - IsSome() returns 1 if a value is present; IsNone() returns 0.
//   - Get() returns the stored value if present; traps if called on None.
//   - TryGet(out) writes to *out and returns 1 if present; returns 0 if None.
//   - The contained value is retained on creation and released on finalize.
//
// Ownership/Lifetime:
//   - The Option retains a reference to the wrapped value (if any).
//   - The GC finalizer releases the wrapped value reference.
//   - Callers receive a fresh Option reference (refcount=1).
//
// Links: src/runtime/oop/rt_option.h (public API),
//        src/runtime/oop/rt_result.h (Result<T,E> type, related pattern)
//
//===----------------------------------------------------------------------===//

#include "rt_option.h"
#include "rt_error.h"
#include "rt_object.h"
#include "rt_result.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef enum { OPTION_SOME = 0, OPTION_NONE = 1 } OptionVariant;

typedef enum { VALUE_PTR = 0, VALUE_STR = 1, VALUE_I64 = 2, VALUE_F64 = 3 } ValueType;

typedef struct {
    OptionVariant variant;
    ValueType value_type;

    union {
        void *ptr;
        rt_string str;
        int64_t i64;
        double f64;
    } value;
} Option;

//=============================================================================
// Option Finalizer
//=============================================================================

/// @brief GC finalizer: releases the contained reference for SOME variants. PTR variants release
/// via the generic object path; STR variants use `rt_str_release_maybe` (which handles literal
/// vs heap strings). I64/F64 variants own no heap memory and need no cleanup.
static void option_finalizer(void *obj) {
    Option *o = (Option *)obj;
    if (!o || o->variant != OPTION_SOME)
        return;
    if (o->value_type == VALUE_PTR && o->value.ptr) {
        if (rt_obj_release_check0(o->value.ptr))
            rt_obj_free(o->value.ptr);
        o->value.ptr = NULL;
    } else if (o->value_type == VALUE_STR && o->value.str) {
        rt_str_release_maybe(o->value.str);
        o->value.str = NULL;
    }
}

//=============================================================================
// Option Creation
//=============================================================================

/// @brief Construct `Some(value)` over a generic pointer payload. Retains `value` via the heap
/// refcount path (so it survives until the Option is finalized).
void *rt_option_some(void *value) {
    Option *o = (Option *)rt_obj_new_i64(0, (int64_t)sizeof(Option));

    o->variant = OPTION_SOME;
    o->value_type = VALUE_PTR;
    o->value.ptr = value;
    rt_obj_retain_maybe(value);
    rt_obj_set_finalizer(o, option_finalizer);
    return o;
}

/// @brief Construct `Some(string)`. Retains the string via `rt_string_ref` (handles both heap
/// and literal-pool strings); accepts NULL (stored as NULL).
void *rt_option_some_str(rt_string value) {
    Option *o = (Option *)rt_obj_new_i64(0, (int64_t)sizeof(Option));

    o->variant = OPTION_SOME;
    o->value_type = VALUE_STR;
    o->value.str = value ? rt_string_ref(value) : NULL;
    rt_obj_set_finalizer(o, option_finalizer);
    return o;
}

/// @brief Construct `Some(i64)` with the value stored inline in the union (no heap retention).
void *rt_option_some_i64(int64_t value) {
    Option *o = (Option *)rt_obj_new_i64(0, (int64_t)sizeof(Option));

    o->variant = OPTION_SOME;
    o->value_type = VALUE_I64;
    o->value.i64 = value;
    rt_obj_set_finalizer(o, option_finalizer);
    return o;
}

/// @brief Construct `Some(f64)` with the value stored inline (no heap retention).
void *rt_option_some_f64(double value) {
    Option *o = (Option *)rt_obj_new_i64(0, (int64_t)sizeof(Option));

    o->variant = OPTION_SOME;
    o->value_type = VALUE_F64;
    o->value.f64 = value;
    rt_obj_set_finalizer(o, option_finalizer);
    return o;
}

/// @brief Construct the empty Option (`None`). The variant is OPTION_NONE; payload is unused.
void *rt_option_none(void) {
    Option *o = (Option *)rt_obj_new_i64(0, (int64_t)sizeof(Option));

    o->variant = OPTION_NONE;
    o->value_type = VALUE_PTR;
    o->value.ptr = NULL;
    rt_obj_set_finalizer(o, option_finalizer);
    return o;
}

//=============================================================================
// Option Inspection
//=============================================================================

/// @brief Check whether the Option contains a value (is the Some variant).
int8_t rt_option_is_some(void *obj) {
    if (!obj)
        return 0;
    Option *o = (Option *)obj;
    return o->variant == OPTION_SOME ? 1 : 0;
}

/// @brief Check whether the Option is empty (is the None variant). NULL is treated as None.
int8_t rt_option_is_none(void *obj) {
    if (!obj)
        return 1; // Treat NULL as None
    Option *o = (Option *)obj;
    return o->variant == OPTION_NONE ? 1 : 0;
}

//=============================================================================
// Value Extraction
//=============================================================================

#include "rt_trap.h"

/// @brief Convenience wrapper around `rt_trap` so the unwrap helpers stay readable.
static void trap_with_message(const char *msg) {
    rt_trap(msg);
}

/// @brief Extract the pointer payload from a Some option; **traps** if NULL or None. Use this
/// when you've already proven (via `is_some`) that the option holds a value.
void *rt_option_unwrap(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    return o->value.ptr;
}

/// @brief Extract the string value from a Some option; traps if None or wrong type.
rt_string rt_option_unwrap_str(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    if (o->value_type != VALUE_STR)
        trap_with_message("Unwrap string called on non-string Option");
    return o->value.str;
}

/// @brief Extract the i64 value from a Some option; traps if None or wrong type.
int64_t rt_option_unwrap_i64(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    if (o->value_type != VALUE_I64)
        trap_with_message("Unwrap i64 called on non-i64 Option");
    return o->value.i64;
}

/// @brief Extract the f64 value from a Some option; traps if None or wrong type.
double rt_option_unwrap_f64(void *obj) {
    if (!obj)
        trap_with_message("Unwrap called on NULL Option");
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        trap_with_message("Unwrap called on None Option");
    if (o->value_type != VALUE_F64)
        trap_with_message("Unwrap f64 called on non-f64 Option");
    return o->value.f64;
}

/// @brief Return the wrapped pointer if present, otherwise `def`. Never traps. NULL handle treated
/// as None.
void *rt_option_unwrap_or(void *obj, void *def) {
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    return o->value.ptr;
}

/// @brief Unwrap the or str of the option.
rt_string rt_option_unwrap_or_str(void *obj, rt_string def) {
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    if (o->value_type != VALUE_STR)
        return def;
    return o->value.str;
}

/// @brief Unwrap the or i64 of the option.
int64_t rt_option_unwrap_or_i64(void *obj, int64_t def) {
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    if (o->value_type != VALUE_I64)
        return def;
    return o->value.i64;
}

/// @brief Unwrap the or f64 of the option.
double rt_option_unwrap_or_f64(void *obj, double def) {
    if (!obj)
        return def;
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return def;
    if (o->value_type != VALUE_F64)
        return def;
    return o->value.f64;
}

/// @brief Return the wrapped pointer if Some, NULL otherwise. Like `unwrap` but non-trapping —
/// the caller must distinguish "stored NULL" from "no value" via `is_some` / `is_none`.
void *rt_option_value(void *obj) {
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

/// @brief Format the message for `expect()` failures — substitutes "assertion failed" for NULL.
static const char *rt_option_expect_message(rt_string msg) {
    return msg ? rt_string_cstr(msg) : "assertion failed";
}

/// @brief Like `unwrap` but with a caller-supplied diagnostic message. Traps with kind
/// INVALID_OPERATION (more specific than the generic unwrap trap) so callers can catch
/// expectation violations distinctly. Use when you want a meaningful failure mode.
void *rt_option_expect(void *obj, rt_string msg) {
    const char *msg_str = rt_option_expect_message(msg);
    char buffer[256];
    if (!obj) {
        snprintf(buffer, sizeof(buffer), "Option expect: %s (NULL Option)", msg_str);
        rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    }
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME) {
        snprintf(buffer, sizeof(buffer), "Option expect: %s", msg_str);
        rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    }
    return o->value.ptr;
}

//=============================================================================
// Transformation
//=============================================================================

// =============================================================================
// Transformation combinators — all PTR-variant only. Typed-primitive Options
// (STR/I64/F64) pass through unchanged because the function signature `void*
// (*)(void*)` doesn't match the union; callers must unwrap-transform-rewrap.
// All combinators on NULL/None inputs return a fresh `None`.
// =============================================================================

/// @brief Apply `fn` to the wrapped value, returning `Some(fn(val))`. None passes through.
void *rt_option_map(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return rt_option_none();

    if (o->value_type == VALUE_PTR) {
        void *new_val = fn(o->value.ptr);
        return rt_option_some(new_val);
    }
    return obj;
}

/// @brief Monadic bind: apply `fn` to the wrapped value where `fn` itself returns an Option,
/// flattening the result. Used to chain fallible operations without nested Options.
void *rt_option_and_then(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return rt_option_none();

    if (o->value_type == VALUE_PTR) {
        return fn(o->value.ptr);
    }
    return obj;
}

/// @brief If the option is Some, return it unchanged; otherwise call `fn()` to compute a fallback
/// Option. Used for "try this default lookup if the primary failed" patterns.
void *rt_option_or_else(void *obj, void *(*fn)(void)) {
    if (!obj)
        return fn ? fn() : rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant == OPTION_SOME)
        return obj;
    return fn ? fn() : rt_option_none();
}

/// @brief Return the option if Some AND `pred(value)` is true; otherwise None. Cheap way to
/// turn unconditional Some values into Some-or-None based on a predicate.
void *rt_option_filter(void *obj, int8_t (*pred)(void *)) {
    if (!obj || !pred)
        return rt_option_none();
    Option *o = (Option *)obj;
    if (o->variant != OPTION_SOME)
        return rt_option_none();

    if (o->value_type == VALUE_PTR && pred(o->value.ptr)) {
        return obj;
    }
    return rt_option_none();
}

/// @brief IL trampoline for `rt_option_map` — re-types the user fn pointer for the typed call.
void *rt_option_map_wrapper(void *obj, void *fn) {
    return rt_option_map(obj, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_option_and_then`.
void *rt_option_and_then_wrapper(void *obj, void *fn) {
    return rt_option_and_then(obj, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_option_or_else`.
void *rt_option_or_else_wrapper(void *obj, void *fn) {
    return rt_option_or_else(obj, (void *(*)(void))fn);
}

/// @brief IL trampoline for `rt_option_filter`.
void *rt_option_filter_wrapper(void *obj, void *pred) {
    return rt_option_filter(obj, (int8_t (*)(void *))pred);
}

//=============================================================================
// Conversion
//=============================================================================

/// @brief Convert Option → Result by supplying an error value: `Some(v) → Ok(v)`, `None → Err(err)`.
/// Preserves the value-type variant so e.g. `Some(i64) → Ok_i64`. Used to bridge missing-data
/// failures into the Result error-handling pipeline.
void *rt_option_ok_or(void *obj, void *err) {
    if (!obj)
        return rt_result_err(err);
    Option *o = (Option *)obj;
    if (o->variant == OPTION_SOME) {
        switch (o->value_type) {
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

/// @brief String-error variant of `ok_or`. None becomes `Err_str(err)` so the resulting Result
/// holds an rt_string error message rather than an opaque pointer.
void *rt_option_ok_or_str(void *obj, rt_string err) {
    if (!obj)
        return rt_result_err_str(err);
    Option *o = (Option *)obj;
    if (o->variant == OPTION_SOME) {
        switch (o->value_type) {
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

/// @brief Compare two option instances for structural equality.
int8_t rt_option_equals(void *a, void *b) {
    if (a == b)
        return 1;

    // Both NULL = equal (both "None-like")
    if (!a && !b)
        return 1;
    if (!a || !b) {
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

    switch (oa->value_type) {
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

/// @brief Convert the option to a human-readable string representation.
rt_string rt_option_to_string(void *obj) {
    if (!obj)
        return rt_const_cstr("None");

    Option *o = (Option *)obj;
    char buf[256];

    if (o->variant == OPTION_NONE) {
        return rt_const_cstr("None");
    }

    switch (o->value_type) {
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

    return rt_string_from_bytes(buf, strlen(buf));
}
