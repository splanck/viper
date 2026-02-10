//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_async.h
// Purpose: Async task combinators built on Future/Promise + threads.
// Key invariants: All functions return Futures. Thread-safe.
// Ownership/Lifetime: Returned Futures are GC-managed.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Viper.Threads.Async — High-level async combinators
    //=========================================================================

    /// @brief Run a callback asynchronously on a new thread.
    /// @details Spawns a thread that executes callback(arg) and resolves the
    ///          returned Future with the callback's return value.
    /// @param callback Function pointer: void*(*)(void*).
    /// @param arg Argument passed to the callback.
    /// @return Future that resolves with the callback's return value.
    void *rt_async_run(void *callback, void *arg);

    /// @brief Wait for all Futures to complete and collect results.
    /// @details Returns a Future that resolves with a Seq of results once all
    ///          input futures have resolved. If any future has an error, the
    ///          combined Future resolves with an error.
    /// @param futures Seq of Future objects.
    /// @return Future resolving to Seq of results.
    void *rt_async_all(void *futures);

    /// @brief Wait for the first Future to complete.
    /// @details Returns a Future that resolves with the value of whichever
    ///          input future completes first.
    /// @param futures Seq of Future objects.
    /// @return Future resolving to the first completed value.
    void *rt_async_any(void *futures);

    /// @brief Return a Future that resolves after a delay.
    /// @details The returned Future resolves with NULL after the delay.
    /// @param ms Delay in milliseconds.
    /// @return Future that resolves after the delay.
    void *rt_async_delay(int64_t ms);

    /// @brief Chain a transformation onto a Future.
    /// @details When the source future resolves, applies mapper(result, arg)
    ///          and resolves the returned Future with the mapper's result.
    /// @param future Source Future.
    /// @param mapper Function pointer: void*(*)(void*, void*).
    /// @param arg Extra argument passed to mapper.
    /// @return New Future resolving to the mapped value.
    void *rt_async_map(void *future, void *mapper, void *arg);

    /// @brief Run a callback asynchronously with cancellation support.
    /// @details Like rt_async_run but the callback receives the cancellation
    ///          token as a second argument: void*(*)(void* arg, void* token).
    /// @param callback Function pointer: void*(*)(void*, void*).
    /// @param arg Argument passed to the callback.
    /// @param token Cancellation token (may be NULL).
    /// @return Future that resolves with the callback's return value.
    void *rt_async_run_cancellable(void *callback, void *arg, void *token);

#ifdef __cplusplus
}
#endif
