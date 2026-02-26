//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_future.h
// Purpose: Future/Promise primitives for asynchronous result passing between threads, with blocking
// get, timed get, non-blocking try-get, and error propagation.
//
// Key invariants:
//   - A Promise can be completed exactly once (value or error); subsequent completions trap.
//   - Each Promise has exactly one associated Future.
//   - Future.get blocks until the Promise is resolved.
//   - Future.get traps if the Promise was completed with an error.
//
// Ownership/Lifetime:
//   - Opaque Promise and Future objects are managed by the runtime object system.
//   - The Future is obtained from its Promise and shares its lifetime.
//
// Links: src/runtime/threads/rt_future.c (implementation)
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_FUTURE_H
#define VIPER_RT_FUTURE_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // Viper.Threads.Promise
    //=============================================================================

    /// @brief Create a new Promise.
    /// @details A Promise is used to set a value that will be received by a Future.
    /// @return Opaque Promise object pointer.
    void *rt_promise_new(void);

    /// @brief Get the Future associated with this Promise.
    /// @details The Future can be passed to another thread to receive the result.
    ///          Multiple calls return the same Future object.
    /// @param promise Promise object pointer.
    /// @return Associated Future object pointer.
    void *rt_promise_get_future(void *promise);

    /// @brief Complete the Promise with a value.
    /// @details The associated Future is resolved with this value.
    ///          Can only be called once; subsequent calls trap.
    /// @param promise Promise object pointer.
    /// @param value The result value.
    void rt_promise_set(void *promise, void *value);

    /// @brief Complete the Promise with an error.
    /// @details The associated Future is resolved with an error state.
    ///          Can only be called once; subsequent calls trap.
    /// @param promise Promise object pointer.
    /// @param error Error message string.
    void rt_promise_set_error(void *promise, rt_string error);

    /// @brief Check if the Promise is already completed.
    /// @param promise Promise object pointer.
    /// @return 1 if completed, 0 otherwise.
    int8_t rt_promise_is_done(void *promise);

    //=============================================================================
    // Viper.Threads.Future
    //=============================================================================

    /// @brief Get the value from the Future, blocking until resolved.
    /// @details Blocks until the associated Promise is completed.
    ///          Traps if the Promise was completed with an error.
    /// @param future Future object pointer.
    /// @return The result value.
    void *rt_future_get(void *future);

    /// @brief Get the value with a timeout.
    /// @details Blocks up to @p ms milliseconds for the result.
    /// @param future Future object pointer.
    /// @param ms Timeout in milliseconds.
    /// @param out Pointer to store the result value.
    /// @return 1 if resolved with value, 0 if timed out or error.
    int8_t rt_future_get_for(void *future, int64_t ms, void **out);

    /// @brief Check if the Future is resolved.
    /// @details Returns immediately without blocking.
    /// @param future Future object pointer.
    /// @return 1 if resolved (value or error), 0 if still pending.
    int8_t rt_future_is_done(void *future);

    /// @brief Check if the Future resolved with an error.
    /// @param future Future object pointer.
    /// @return 1 if resolved with error, 0 otherwise.
    int8_t rt_future_is_error(void *future);

    /// @brief Get the error message if the Future resolved with an error.
    /// @param future Future object pointer.
    /// @return Error message, or empty string if no error.
    rt_string rt_future_get_error(void *future);

    /// @brief Try to get the value without blocking (IL-friendly).
    /// @details Returns the value if resolved, or NULL if pending or error.
    /// @param future Future object pointer.
    /// @return The value, or NULL if not yet resolved or resolved with error.
    void *rt_future_try_get_val(void *future);

    /// @brief Get the value with a timeout (IL-friendly).
    /// @details Blocks up to @p ms milliseconds. Returns the value if resolved,
    ///          or NULL if timed out or resolved with error.
    /// @param future Future object pointer.
    /// @param ms Timeout in milliseconds.
    /// @return The value, or NULL on timeout/error.
    void *rt_future_get_for_val(void *future, int64_t ms);

    /// @brief Try to get the value without blocking.
    /// @details Returns immediately if resolved, NULL if pending or error.
    /// @param future Future object pointer.
    /// @param out Pointer to store the result value (can be NULL to just check).
    /// @return 1 if resolved with value, 0 if pending or error.
    int8_t rt_future_try_get(void *future, void **out);

    /// @brief Wait for the Future to be resolved.
    /// @details Blocks until resolved (value or error).
    /// @param future Future object pointer.
    void rt_future_wait(void *future);

    /// @brief Wait for the Future with a timeout.
    /// @details Blocks up to @p ms milliseconds.
    /// @param future Future object pointer.
    /// @param ms Timeout in milliseconds.
    /// @return 1 if resolved, 0 if timed out.
    int8_t rt_future_wait_for(void *future, int64_t ms);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_FUTURE_H
