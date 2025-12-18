//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_threads.h
// Purpose: Runtime thread/monitor primitives backing Viper.Threads.
// Key invariants: Monitor operations are re-entrant and FIFO-fair.
// Ownership/Lifetime: Thread handles and Safe* values are runtime-managed objects.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // =========================================================================
    // Viper.Threads.Monitor
    // =========================================================================

    void rt_monitor_enter(void *obj);
    int8_t rt_monitor_try_enter(void *obj);
    int8_t rt_monitor_try_enter_for(void *obj, int64_t ms);
    void rt_monitor_exit(void *obj);
    void rt_monitor_wait(void *obj);
    int8_t rt_monitor_wait_for(void *obj, int64_t ms);
    void rt_monitor_pause(void *obj);
    void rt_monitor_pause_all(void *obj);

    // =========================================================================
    // Viper.Threads.Thread
    // =========================================================================

    void *rt_thread_start(void *entry, void *arg);
    void rt_thread_join(void *thread);
    int8_t rt_thread_try_join(void *thread);
    int8_t rt_thread_join_for(void *thread, int64_t ms);
    int64_t rt_thread_get_id(void *thread);
    int8_t rt_thread_get_is_alive(void *thread);
    void rt_thread_sleep(int64_t ms);
    void rt_thread_yield(void);

    // =========================================================================
    // Viper.Threads.SafeI64
    // =========================================================================

    void *rt_safe_i64_new(int64_t initial);
    int64_t rt_safe_i64_get(void *obj);
    void rt_safe_i64_set(void *obj, int64_t value);
    int64_t rt_safe_i64_add(void *obj, int64_t delta);
    int64_t rt_safe_i64_compare_exchange(void *obj, int64_t expected, int64_t desired);

#ifdef __cplusplus
}
#endif
