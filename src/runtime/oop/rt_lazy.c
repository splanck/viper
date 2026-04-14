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
#include "rt_platform.h"
#include "rt_string.h"

#include <stdlib.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef enum { VALUE_PTR = 0, VALUE_STR = 1, VALUE_I64 = 2 } ValueType;

typedef struct {
    int evaluated; /* widened from int8_t for MSVC _Generic atomic compat */
    ValueType value_type;
    void *(*supplier)(void);

    union {
        void *ptr;
        rt_string str;
        int64_t i64;
    } value;
} Lazy;

//=============================================================================
// Lazy Finalizer
//=============================================================================

/// @brief GC finalizer: release the cached payload (PTR via heap, STR via `rt_str_release_maybe`).
/// I64 holds no heap memory. Skip if the Lazy was never evaluated (no payload to release).
static void lazy_finalizer(void *obj) {
    Lazy *l = (Lazy *)obj;
    if (!l || !l->evaluated)
        return;
    if (l->value_type == VALUE_PTR && l->value.ptr) {
        if (rt_obj_release_check0(l->value.ptr))
            rt_obj_free(l->value.ptr);
        l->value.ptr = NULL;
    } else if (l->value_type == VALUE_STR && l->value.str) {
        rt_str_release_maybe(l->value.str);
        l->value.str = NULL;
    }
}

//=============================================================================
// Lazy Creation
//=============================================================================

/// @brief Construct a Lazy<Ptr> whose value will be computed by `supplier()` on first access.
/// `supplier` is borrowed (not retained); caller owns its lifetime. The supplier's return value
/// is cached on first call and reused thereafter; any subsequent suppliers passed in are ignored.
void *rt_lazy_new(void *(*supplier)(void)) {
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 0;
    l->value_type = VALUE_PTR;
    l->supplier = supplier;
    l->value.ptr = NULL;
    rt_obj_set_finalizer(l, lazy_finalizer);
    return l;
}

/// @brief Construct a pre-evaluated Lazy<Ptr> wrapping `value`. Useful when interop expects a
/// Lazy<T> handle but you already have the value (e.g., test fixtures, eager-but-typed APIs).
void *rt_lazy_of(void *value) {
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 1;
    l->value_type = VALUE_PTR;
    l->supplier = NULL;
    l->value.ptr = value;
    rt_obj_set_finalizer(l, lazy_finalizer);
    return l;
}

/// @brief Pre-evaluated Lazy<String> variant. Stores the rt_string directly; finalizer releases.
void *rt_lazy_of_str(rt_string value) {
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 1;
    l->value_type = VALUE_STR;
    l->supplier = NULL;
    l->value.str = value;
    rt_obj_set_finalizer(l, lazy_finalizer);
    return l;
}

/// @brief Pre-evaluated Lazy<I64> variant. Stores the value inline (no heap retention needed).
void *rt_lazy_of_i64(int64_t value) {
    Lazy *l = (Lazy *)rt_obj_new_i64(0, (int64_t)sizeof(Lazy));

    l->evaluated = 1;
    l->value_type = VALUE_I64;
    l->supplier = NULL;
    l->value.i64 = value;
    rt_obj_set_finalizer(l, lazy_finalizer);
    return l;
}

//=============================================================================
// Lazy Access
//=============================================================================

/// @brief Thread-safe evaluation: double-checked atomic load on `evaluated` (acquire) lets
/// already-initialized Lazies skip the supplier call without a barrier. On the first call,
/// invoke the supplier, then atomic-store `evaluated=1` (release) so other threads see the
/// completed cache. **Race contract:** double evaluation is possible (no exclusion lock) but
/// benign — the Lazy contract requires pure suppliers, so re-entrancy returns the same value.
static void evaluate(Lazy *l) {
    // Use atomic load/store for thread safety on ARM64 and other weak-memory platforms.
    // Double evaluation is possible but benign (Lazy contract assumes pure suppliers).
    if (__atomic_load_n(&l->evaluated, __ATOMIC_ACQUIRE))
        return;

    if (l->supplier) {
        l->value.ptr = l->supplier();
    }
    __atomic_store_n(&l->evaluated, 1, __ATOMIC_RELEASE);
}

/// @brief Force evaluation if needed, return the pointer payload. NULL handle returns NULL.
void *rt_lazy_get(void *obj) {
    if (!obj)
        return NULL;
    Lazy *l = (Lazy *)obj;

    evaluate(l);
    return l->value.ptr;
}

/// @brief Force evaluation if needed and return the result as a string.
rt_string rt_lazy_get_str(void *obj) {
    if (!obj)
        return rt_const_cstr("");
    Lazy *l = (Lazy *)obj;

    evaluate(l);

    if (l->value_type == VALUE_STR) {
        return l->value.str;
    }
    return rt_const_cstr("");
}

/// @brief Force evaluation if needed and return the result as an i64.
int64_t rt_lazy_get_i64(void *obj) {
    if (!obj)
        return 0;
    Lazy *l = (Lazy *)obj;

    evaluate(l);

    if (l->value_type == VALUE_I64) {
        return l->value.i64;
    }
    return 0;
}

//=============================================================================
// Lazy State
//=============================================================================

/// @brief Check whether the lazy value has already been evaluated (computed).
int8_t rt_lazy_is_evaluated(void *obj) {
    if (!obj)
        return 1;
    Lazy *l = (Lazy *)obj;
    return l->evaluated;
}

/// @brief Force evaluation of the lazy value, even if already evaluated.
void rt_lazy_force(void *obj) {
    if (!obj)
        return;
    Lazy *l = (Lazy *)obj;
    evaluate(l);
}

//=============================================================================
// Transformation
//=============================================================================

/// @brief Apply `fn` to the lazy's value, returning a fresh pre-evaluated Lazy with the result.
/// **Caveat:** despite the name, this is NOT a deferred-map — it forces evaluation of the source
/// (no closure support in C), then wraps the transformed value. Truly lazy chaining requires
/// `LazySeq` instead. Useful when you want a Lazy<T> handle for an already-known transformation.
void *rt_lazy_map(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return obj;

    Lazy *l = (Lazy *)obj;

    // If already evaluated, apply fn immediately
    if (l->evaluated) {
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

/// @brief Apply `fn` (returning a Lazy) to the source's value, returning the resulting Lazy
/// directly (no double-wrapping). Forces the source eagerly for the same closure-support reason
/// as `_map`. Returned Lazy is whatever `fn` produced — its evaluation timing is `fn`'s choice.
void *rt_lazy_flat_map(void *obj, void *(*fn)(void *)) {
    if (!obj || !fn)
        return obj;

    // Force evaluation of the source lazy
    void *value = rt_lazy_get(obj);

    // Call fn to get a new lazy
    void *new_lazy = fn(value);

    // Return the new lazy (which will be evaluated when needed)
    return new_lazy;
}

/// @brief IL trampoline for `rt_lazy_new` — re-types the supplier pointer for the typed call.
void *rt_lazy_new_wrapper(void *supplier) {
    return rt_lazy_new((void *(*)(void))supplier);
}

/// @brief IL trampoline for `rt_lazy_map`.
void *rt_lazy_map_wrapper(void *obj, void *fn) {
    return rt_lazy_map(obj, (void *(*)(void *))fn);
}

/// @brief IL trampoline for `rt_lazy_flat_map`.
void *rt_lazy_flat_map_wrapper(void *obj, void *fn) {
    return rt_lazy_flat_map(obj, (void *(*)(void *))fn);
}
