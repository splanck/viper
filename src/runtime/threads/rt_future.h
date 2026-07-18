//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
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
//   - Internal no-throw rejection still publishes an error state if diagnostic
//     string allocation fails, so worker Futures never remain pending on OOM.
//
// Ownership/Lifetime:
//   - Opaque Promise and Future objects are managed by the runtime object system.
//   - The Future is obtained from its Promise and shares its lifetime.
//
// Links: src/runtime/threads/rt_future.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Zanna.Threads.Promise
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

/// @brief Complete the Promise with a retained runtime-managed value.
/// @details Retains @p value before resolving the promise and releases it from
///          the promise finalizer. Only use this when @p value is a runtime-
///          managed object or string handle.
/// @param promise Promise object pointer.
/// @param value Runtime-managed result object.
void rt_promise_set_owned(void *promise, void *value);

/// @brief Complete the Promise by transferring one existing value reference.
/// @details The promise owns @p value after this call and releases it from the
///          promise finalizer. Unlike rt_promise_set_owned, this does not
///          retain first; callers must only use it for callback results whose
///          reference is being handed to the Future.
/// @param promise Promise object pointer.
/// @param value Runtime-managed result object or raw pointer.
void rt_promise_set_transferred(void *promise, void *value);

/// @brief Try to complete a producer-owned Promise by transferring one value reference.
/// @details This internal worker primitive never traps for duplicate completion:
///          when another producer has already settled the Promise, it releases
///          the transferred @p value and returns zero. On success the Promise
///          owns that reference until Future retrieval/finalization. The caller
///          must own a live Promise reference throughout the call; this function
///          does not consume it. Invalid handles are rejected without trapping.
/// @param promise Producer-owned Promise object pointer.
/// @param value Runtime-managed result reference being transferred, or NULL.
/// @return One when this call completed the Promise, otherwise zero; @p value is
///         consumed in either case and no invalid/duplicate-completion trap is
///         propagated.
int8_t rt_promise_try_set_transferred(void *promise, void *value);

/// @brief Complete the Promise with an error.
/// @details The associated Future is resolved with an error state.
///          Can only be called once; subsequent calls trap.
/// @param promise Promise object pointer.
/// @param error Error message string.
void rt_promise_set_error(void *promise, rt_string error);

/// @brief Try to reject a producer-owned Promise from a native C string without propagating OOM.
/// @details This internal worker primitive copies @p error when allocation is
///          available. If the copy traps or returns NULL, it still atomically
///          completes the Promise as an error with no stored message; Future.Get
///          then uses its generic error diagnostic and Future.IsError remains
///          true. Unlike @ref rt_promise_set_error, an already-completed Promise
///          returns zero instead of trapping. The caller must own a live Promise
///          reference for the entire call, and the function does not consume it.
/// @param promise Producer-owned Promise object pointer.
/// @param error NUL-terminated diagnostic text, or NULL for a message-less error.
/// @return One when this call completed the Promise, zero when the handle was
///         invalid or another producer had already completed it; neither case
///         invokes the trap dispatcher.
int8_t rt_promise_try_set_error_cstr(void *promise, const char *error);

/// @brief Check if the Promise is already completed.
/// @param promise Promise object pointer.
/// @return 1 if completed, 0 otherwise.
int8_t rt_promise_is_done(void *promise);

//=============================================================================
// Zanna.Threads.Future
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

/// @brief Try to get the value without blocking as an Option.
/// @details Returns `Some(value)` when the future has resolved successfully and
///          `None` when it is still pending or resolved with an error. A
///          successful NULL value is represented as `Some(NULL)`.
/// @param future Future object pointer.
/// @return Opaque Zanna.Option object.
void *rt_future_try_get_option(void *future);

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

/// @brief Internal extended listener hook with cancellation cleanup.
/// @details Like rt_future_on_complete, but also records an optional cleanup
///          callback that runs if the listener is removed before completion.
int8_t rt_future_on_complete_ex(void *future,
                                void (*callback)(void *future, void *ctx),
                                void *ctx,
                                void (*cancel)(void *ctx));

/// @brief Internal completion-listener hook used by async combinators.
/// @details Registers @p callback to run exactly once when @p future completes.
///          If the future is already complete, the callback runs synchronously
///          before this function returns.
/// @param future Future object pointer.
/// @param callback Listener callback receiving the completed future and @p ctx.
/// @param ctx Opaque listener context.
/// @return 1 on success, 0 on allocation failure or invalid input.
int8_t rt_future_on_complete(void *future, void (*callback)(void *future, void *ctx), void *ctx);

/// @brief Internal listener removal hook used by async combinators.
/// @details Removes the matching completion listener if it is still pending.
///          When removed, any cancellation cleanup registered with
///          rt_future_on_complete_ex is invoked exactly once.
/// @return 1 if a listener was removed, 0 otherwise.
int8_t rt_future_cancel_listener(void *future,
                                 void (*callback)(void *future, void *ctx),
                                 void *ctx);

/// @brief Internal raw-value accessor for completed non-error futures.
/// @details Returns the stored value. If the promise owns that value, the
///          returned pointer is retained for the caller and must be released
///          after use. Borrowed values are returned without a retain. Intended
///          for runtime combinators that forward or inspect a value while
///          another future/promise may still own its lifetime.
/// @param future Future object pointer.
/// @return Stored value when the future is done and not errored; otherwise NULL.
void *rt_future_peek_value(void *future);

/// @brief Internal query for whether the stored value is promise-owned.
/// @details Returns true for results completed via rt_promise_set_owned.
/// @param future Future object pointer.
/// @return 1 when the stored value is promise-owned, otherwise 0.
int8_t rt_future_value_is_owned(void *future);

#ifdef __cplusplus
}
#endif
