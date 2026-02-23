//===----------------------------------------------------------------------===//
//
// File: src/runtime/oop/rt_lazy.h
// Purpose: Lazy type providing deferred computation until first access, evaluating a factory function once and caching the result for subsequent accesses.
//
// Key invariants:
//   - The factory function is called exactly once, on first access.
//   - The computed value is cached; subsequent accesses return the cached value.
//   - Thread-safe initialization uses atomic compare-exchange to ensure single evaluation.
//   - rt_lazy_get returns the cached value after initialization.
//
// Ownership/Lifetime:
//   - Lazy objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/oop/rt_lazy.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Lazy Creation
    //=========================================================================

    /// @brief Create a Lazy with a supplier function.
    /// @param supplier Function that produces the value.
    /// @return Opaque Lazy object pointer.
    void *rt_lazy_new(void *(*supplier)(void));

    /// @brief Create an already-evaluated Lazy with a value.
    /// @param value The already-computed value.
    /// @return Opaque Lazy object pointer.
    void *rt_lazy_of(void *value);

    /// @brief Create an already-evaluated Lazy with a string value.
    /// @param value The string value.
    /// @return Opaque Lazy object pointer.
    void *rt_lazy_of_str(rt_string value);

    /// @brief Create an already-evaluated Lazy with an i64 value.
    /// @param value The integer value.
    /// @return Opaque Lazy object pointer.
    void *rt_lazy_of_i64(int64_t value);

    //=========================================================================
    // Lazy Access
    //=========================================================================

    /// @brief Get the value, computing if necessary.
    /// @param obj Opaque Lazy object pointer.
    /// @return The computed value.
    void *rt_lazy_get(void *obj);

    /// @brief Get the string value, computing if necessary.
    /// @param obj Opaque Lazy object pointer.
    /// @return The computed string value.
    rt_string rt_lazy_get_str(void *obj);

    /// @brief Get the i64 value, computing if necessary.
    /// @param obj Opaque Lazy object pointer.
    /// @return The computed integer value.
    int64_t rt_lazy_get_i64(void *obj);

    //=========================================================================
    // Lazy State
    //=========================================================================

    /// @brief Check if the value has been computed.
    /// @param obj Opaque Lazy object pointer.
    /// @return 1 if already evaluated, 0 if pending.
    int8_t rt_lazy_is_evaluated(void *obj);

    /// @brief Force evaluation without returning value.
    /// @param obj Opaque Lazy object pointer.
    void rt_lazy_force(void *obj);

    //=========================================================================
    // Transformation
    //=========================================================================

    /// @brief Create a new Lazy by transforming this one.
    /// @param obj Opaque Lazy object pointer.
    /// @param fn Function to apply to the value.
    /// @return New Lazy that will apply fn when accessed.
    void *rt_lazy_map(void *obj, void *(*fn)(void *));

    /// @brief Chain Lazy operations (flatMap).
    /// @param obj Opaque Lazy object pointer.
    /// @param fn Function that returns a new Lazy.
    /// @return New Lazy that will unwrap the result.
    void *rt_lazy_flat_map(void *obj, void *(*fn)(void *));

#ifdef __cplusplus
}
#endif
