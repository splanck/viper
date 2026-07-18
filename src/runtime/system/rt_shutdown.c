//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_shutdown.c
// Purpose: Implements Zanna.System.Shutdown's poll-based graceful shutdown
//          request bitmask.
// Key invariants: Polling is cooperative and signal-safe work is kept in the
//                 VM/platform layer; this module only stores ordinary atomic
//                 process state.
// Ownership/Lifetime: No heap ownership; state lasts for the process lifetime.
// Links: src/runtime/system/rt_shutdown.h
//
//===----------------------------------------------------------------------===//

#include "rt_shutdown.h"
#include "rt_platform.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#endif

static atomic_llong g_shutdown_pending;
static atomic_bool g_shutdown_polling_enabled;
static _Atomic(rt_shutdown_clear_interrupt_fn) g_clear_interrupt_callback;

static void mark_polling_enabled(void) {
    atomic_store_explicit(&g_shutdown_polling_enabled, true, memory_order_release);
}

void rt_shutdown_set_interrupt_clear_callback(rt_shutdown_clear_interrupt_fn callback) {
    atomic_store_explicit(&g_clear_interrupt_callback, callback, memory_order_release);
}

void rt_shutdown_request(int64_t reason) {
    const int64_t mask = reason & RT_SHUTDOWN_REASON_MASK;
    if (mask == RT_SHUTDOWN_REASON_NONE)
        return;
    atomic_fetch_or_explicit(&g_shutdown_pending, (long long)mask, memory_order_acq_rel);
}

int64_t rt_shutdown_poll(void) {
    mark_polling_enabled();
    const int64_t mask =
        (int64_t)atomic_exchange_explicit(&g_shutdown_pending, 0, memory_order_acq_rel);
    if (mask != RT_SHUTDOWN_REASON_NONE) {
        atomic_store_explicit(&g_shutdown_polling_enabled, false, memory_order_release);
        rt_shutdown_clear_interrupt_fn callback =
            atomic_load_explicit(&g_clear_interrupt_callback, memory_order_acquire);
        if (callback)
            callback();
    }
    return mask;
}

int8_t rt_shutdown_pending(void) {
    mark_polling_enabled();
    return atomic_load_explicit(&g_shutdown_pending, memory_order_acquire) != 0 ? 1 : 0;
}

void rt_shutdown_clear_pending_only(void) {
    atomic_store_explicit(&g_shutdown_pending, 0, memory_order_release);
}

void rt_shutdown_clear(void) {
    rt_shutdown_clear_pending_only();
    atomic_store_explicit(&g_shutdown_polling_enabled, false, memory_order_release);
    rt_shutdown_clear_interrupt_fn callback =
        atomic_load_explicit(&g_clear_interrupt_callback, memory_order_acquire);
    if (callback)
        callback();
}

int8_t rt_shutdown_polling_enabled(void) {
    return atomic_exchange_explicit(&g_shutdown_polling_enabled, false, memory_order_acq_rel) ? 1
                                                                                              : 0;
}

int8_t rt_shutdown_has_pending(void) {
    return atomic_load_explicit(&g_shutdown_pending, memory_order_acquire) != 0 ? 1 : 0;
}

#if RT_PLATFORM_WINDOWS
static BOOL WINAPI shutdown_win_ctrl_handler(DWORD ctrl_type) {
    int64_t reason = RT_SHUTDOWN_REASON_INTERRUPT;
    if (ctrl_type == CTRL_CLOSE_EVENT || ctrl_type == CTRL_LOGOFF_EVENT ||
        ctrl_type == CTRL_SHUTDOWN_EVENT) {
        reason = RT_SHUTDOWN_REASON_TERMINATE;
    }
    rt_shutdown_request(reason);
    return TRUE;
}
#else
// rt_shutdown_request only performs a lock-free atomic OR, so it is
// async-signal-safe to call directly from these handlers.
static void shutdown_posix_sigint(int sig) {
    (void)sig;
    rt_shutdown_request(RT_SHUTDOWN_REASON_INTERRUPT);
}

static void shutdown_posix_sigterm(int sig) {
    (void)sig;
    rt_shutdown_request(RT_SHUTDOWN_REASON_TERMINATE);
}
#endif

void rt_shutdown_install_signal_handlers(void) {
    static atomic_bool installed;
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &installed, &expected, true, memory_order_acq_rel, memory_order_acquire)) {
        return; // already installed
    }
#if RT_PLATFORM_WINDOWS
    SetConsoleCtrlHandler(shutdown_win_ctrl_handler, TRUE);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = shutdown_posix_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // resume interrupted syscalls where possible
    sigaction(SIGINT, &sa, NULL);

    struct sigaction ta;
    memset(&ta, 0, sizeof(ta));
    ta.sa_handler = shutdown_posix_sigterm;
    sigemptyset(&ta.sa_mask);
    ta.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &ta, NULL);
#endif
}

int64_t rt_shutdown_const_none(void) {
    return RT_SHUTDOWN_REASON_NONE;
}

int64_t rt_shutdown_const_interrupt(void) {
    return RT_SHUTDOWN_REASON_INTERRUPT;
}

int64_t rt_shutdown_const_terminate(void) {
    return RT_SHUTDOWN_REASON_TERMINATE;
}
