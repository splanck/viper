//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_shutdown.h
// Purpose: Poll-based graceful shutdown requests for Viper.System.Shutdown.
// Key invariants: Requests are process-wide bitmasks and are safe to publish
//                 from cooperative host/runtime code.
// Ownership/Lifetime: The module owns only atomic process state; no heap data.
// Links: docs/viperlib/system.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_SHUTDOWN_REASON_NONE INT64_C(0)
#define RT_SHUTDOWN_REASON_INTERRUPT INT64_C(1)
#define RT_SHUTDOWN_REASON_TERMINATE INT64_C(2)
#define RT_SHUTDOWN_REASON_MASK (RT_SHUTDOWN_REASON_INTERRUPT | RT_SHUTDOWN_REASON_TERMINATE)

typedef void (*rt_shutdown_clear_interrupt_fn)(void);

/// @brief Install the VM callback used to consume a pending interrupt epoch.
void rt_shutdown_set_interrupt_clear_callback(rt_shutdown_clear_interrupt_fn callback);

/// @brief Request cooperative graceful shutdown for one or more reason bits.
void rt_shutdown_request(int64_t reason);

/// @brief Return and clear the current shutdown reason bitmask.
int64_t rt_shutdown_poll(void);

/// @brief Return non-zero when any shutdown reason is pending.
int8_t rt_shutdown_pending(void);

/// @brief Clear pending shutdown reasons and the active VM interrupt epoch.
void rt_shutdown_clear(void);

/// @brief Clear pending shutdown reasons without invoking the VM callback.
void rt_shutdown_clear_pending_only(void);

/// @brief Consume and return the one-shot graceful-polling arm flag.
int8_t rt_shutdown_polling_enabled(void);

/// @brief Non-enabling internal peek used by the VM interrupt path.
int8_t rt_shutdown_has_pending(void);

/// @brief Install OS signal/console handlers that publish shutdown requests.
/// @details Wires SIGINT/SIGTERM (POSIX) or the console control handler
///          (Windows) so an OS interrupt/termination becomes a
///          `rt_shutdown_request`, letting `Shutdown.Poll` observe Ctrl-C and
///          termination. Idempotent. The VM installs its own richer handler
///          automatically; native/AOT programs call this to opt in to the same
///          OS integration (they do not get it for free, since the compiler and
///          tools also link the runtime and must keep default signal behavior)
///          (VDOC-210). Exposed as `Viper.System.Shutdown.InstallSignalHandlers`.
void rt_shutdown_install_signal_handlers(void);

/// @brief Constant getters for runtime class properties.
int64_t rt_shutdown_const_none(void);
int64_t rt_shutdown_const_interrupt(void);
int64_t rt_shutdown_const_terminate(void);

#ifdef __cplusplus
}
#endif
