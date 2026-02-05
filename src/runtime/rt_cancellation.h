//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_cancellation.h
// Purpose: Cooperative cancellation token for async operations.
// Key invariants: Thread-safe via atomic operations. Once cancelled, cannot
//                 be uncancelled. Multiple tokens can share a source.
// Ownership/Lifetime: Caller manages token lifetime.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new cancellation token (not cancelled).
    /// @return New cancellation token object.
    void *rt_cancellation_new(void);

    /// @brief Check if cancellation has been requested.
    /// @param token Cancellation token.
    /// @return 1 if cancelled, 0 otherwise.
    int8_t rt_cancellation_is_cancelled(void *token);

    /// @brief Request cancellation.
    /// @param token Cancellation token.
    void rt_cancellation_cancel(void *token);

    /// @brief Reset a cancellation token (allow reuse).
    /// @param token Cancellation token.
    void rt_cancellation_reset(void *token);

    /// @brief Create a linked token that cancels when the parent cancels.
    /// @param parent Parent token.
    /// @return New linked token. Cancels if parent is cancelled.
    void *rt_cancellation_linked(void *parent);

    /// @brief Check if a linked token's parent has been cancelled.
    /// @param token Linked cancellation token.
    /// @return 1 if parent or self is cancelled, 0 otherwise.
    int8_t rt_cancellation_check(void *token);

    /// @brief Throw/trap if the token has been cancelled.
    /// @param token Cancellation token.
    void rt_cancellation_throw_if_cancelled(void *token);

#ifdef __cplusplus
}
#endif
