//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_monitor_win.c
// Purpose: Windows implementation of the monitor (mutual-exclusion) runtime, built on
//   SRWLOCK + CONDITION_VARIABLE. Compiled only on _WIN32; the POSIX build
//   uses rt_monitor_posix.c. Shared helpers live in rt_monitor_internal.h.
//
// Links: rt_monitor_internal.h (shared), rt_monitor_posix.c (POSIX impl), rt_threads.h
//
//===----------------------------------------------------------------------===//

#include "rt_monitor_internal.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include "rt_win32_wait.h"
#include <windows.h>

/// @brief Waiter state enumeration for Windows.
enum {
    RT_MON_WAITER_WAITING_PAUSE = 0, ///< Thread called Wait(), waiting for Pause signal.
    RT_MON_WAITER_WAITING_LOCK = 1,  ///< Thread waiting to acquire the lock.
    RT_MON_WAITER_ACQUIRED = 2,      ///< Thread has been granted ownership.
    RT_MON_WAITER_CANCELLED = 3,     ///< Monitor was retired while the thread was waiting.
};

/// @brief Represents a thread waiting on a monitor (Windows version).
typedef struct RtMonitorWaiter {
    struct RtMonitorWaiter *next; ///< Next waiter in queue (singly linked).
    CONDITION_VARIABLE cv;        ///< Per-waiter condition variable.
    DWORD threadId;               ///< The waiting thread's ID.
    int state;                    ///< Current state (from enum above).
    size_t desired_recursion;     ///< Recursion count to restore on acquisition.
} RtMonitorWaiter;

/// @brief The monitor state associated with an object (Windows version).
typedef struct RtMonitor {
    CRITICAL_SECTION cs;        ///< Critical section protecting all monitor state.
    DWORD owner;                ///< Current owner thread ID.
    int owner_valid;            ///< Non-zero if monitor is currently owned.
    size_t recursion;           ///< Re-entry count for owner.
    RtMonitorWaiter *acq_head;  ///< Head of acquisition queue.
    RtMonitorWaiter *acq_tail;  ///< Tail of acquisition queue.
    RtMonitorWaiter *wait_head; ///< Head of wait queue.
    RtMonitorWaiter *wait_tail; ///< Tail of wait queue.
    int retired;                ///< Monitor owner object is being finalized.
} RtMonitor;

/// @brief Hash table entry mapping object address to monitor.
typedef struct RtMonitorEntry {
    void *key;                   ///< Object address (hash key).
    int retired;                 ///< Object was finalized while monitor was busy.
    struct RtMonitorEntry *next; ///< Next entry in hash chain.
    RtMonitor monitor;           ///< The monitor state.
} RtMonitorEntry;

static void monitor_cancel_queue(RtMonitorWaiter *w);

#define RT_MONITOR_BUCKETS 4096u

static CRITICAL_SECTION g_monitor_table_cs;
static INIT_ONCE g_monitor_table_cs_once = INIT_ONCE_STATIC_INIT;
static RtMonitorEntry *g_monitor_table[RT_MONITOR_BUCKETS];

/// @brief One-shot initialiser for the global monitor-table critical section.
///
/// Win32 lacks a static `CRITICAL_SECTION` initialiser, so we
/// route through `InitOnceExecuteOnce` for thread-safe lazy init.
static BOOL WINAPI init_table_cs_once(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_monitor_table_cs);
    return TRUE;
}

/// @brief Lazy guard around `init_table_cs_once`; cheap on subsequent calls.
static void ensure_table_cs_init(void) {
    /* InitOnceExecuteOnce is thread-safe: exactly one thread runs the
       callback and all concurrent callers block until it completes. */
    InitOnceExecuteOnce(&g_monitor_table_cs_once, init_table_cs_once, NULL, NULL);
}

/// @brief Hash a pointer to a monitor-table bucket index.
///
/// Drops the low 4 bits (object pointers are typically aligned),
/// folds the upper half into the lower half, and multiplies by the
/// fractional golden-ratio prime — a Knuth-style mixing function
/// that gives a uniform distribution across the table.
static size_t hash_ptr(void *p) {
    uintptr_t x = (uintptr_t)p;
    x >>= 4;
    x ^= x >> 16;
    x *= 0x9E3779B97F4A7C15ull;
    return (size_t)(x & (RT_MONITOR_BUCKETS - 1u));
}

/// @brief Locate (or lazily create) the monitor associated with `obj`.
///
/// Walks the hash chain at `hash_ptr(obj)`. If no entry exists, a
/// new RtMonitor is allocated, initialised, and prepended to the
/// chain — all under the table critical section so concurrent
/// callers see at most one node per object.
/// @return Pointer to the monitor (never NULL — traps on alloc failure).
static RtMonitor *get_monitor_for(void *obj) {
    ensure_table_cs_init();
    size_t idx = hash_ptr(obj);

    EnterCriticalSection(&g_monitor_table_cs);
    RtMonitorEntry *it = g_monitor_table[idx];
    while (it) {
        if (it->key == obj && !it->retired) {
            LeaveCriticalSection(&g_monitor_table_cs);
            return &it->monitor;
        }
        it = it->next;
    }

    RtMonitorEntry *node = (RtMonitorEntry *)calloc(1, sizeof(*node));
    if (!node) {
        LeaveCriticalSection(&g_monitor_table_cs);
        rt_trap("rt_monitor: alloc failed");
        return NULL;
    }
    node->key = obj;
    node->next = g_monitor_table[idx];
    g_monitor_table[idx] = node;

    InitializeCriticalSection(&node->monitor.cs);

    LeaveCriticalSection(&g_monitor_table_cs);
    return &node->monitor;
}

/// @brief Release the monitor associated with an object (removes it from the monitor table).
void rt_monitor_forget(void *obj) {
    if (!obj)
        return;
    ensure_table_cs_init();
    size_t idx = hash_ptr(obj);

    EnterCriticalSection(&g_monitor_table_cs);
    RtMonitorEntry **link = &g_monitor_table[idx];
    RtMonitorEntry *node = *link;
    while (node && (node->key != obj || node->retired)) {
        link = &node->next;
        node = node->next;
    }
    if (!node) {
        LeaveCriticalSection(&g_monitor_table_cs);
        return;
    }
    EnterCriticalSection(&node->monitor.cs);
    if (node->monitor.owner_valid || node->monitor.acq_head || node->monitor.wait_head) {
        node->retired = 1;
        node->monitor.retired = 1;
        RtMonitorWaiter *acq = node->monitor.acq_head;
        RtMonitorWaiter *wait = node->monitor.wait_head;
        node->monitor.acq_head = NULL;
        node->monitor.acq_tail = NULL;
        node->monitor.wait_head = NULL;
        node->monitor.wait_tail = NULL;
        monitor_cancel_queue(acq);
        monitor_cancel_queue(wait);
        LeaveCriticalSection(&node->monitor.cs);
        LeaveCriticalSection(&g_monitor_table_cs);
        return;
    }

    *link = node->next;
    LeaveCriticalSection(&node->monitor.cs);
    LeaveCriticalSection(&g_monitor_table_cs);

    DeleteCriticalSection(&node->monitor.cs);
    free(node);
}

/// @brief Free a retired monitor table entry once no thread is still waiting on it.
/// @details Monitor entries enter the "retired" state when the owning
///          object is finalized but a thread is still inside Wait/Enter
///          (potentially blocked on the underlying primitive). The
///          retired entry is kept alive until ref/wait counts drain to
///          zero, at which point this helper unlinks it from the bucket
///          chain, destroys the OS-level mutex/condvar, and frees the
///          node memory. Two near-identical implementations exist
///          (Windows CS-based and POSIX pthread-based) to match the
///          per-platform synchronization primitive.
static void monitor_cleanup_retired_if_idle(void *obj, RtMonitor *monitor) {
    if (!obj || !monitor)
        return;
    ensure_table_cs_init();
    size_t idx = hash_ptr(obj);

    EnterCriticalSection(&g_monitor_table_cs);
    RtMonitorEntry **link = &g_monitor_table[idx];
    RtMonitorEntry *node = *link;
    while (node && (node->key != obj || &node->monitor != monitor)) {
        link = &node->next;
        node = node->next;
    }
    if (!node) {
        LeaveCriticalSection(&g_monitor_table_cs);
        return;
    }

    EnterCriticalSection(&node->monitor.cs);
    if (!node->retired || node->monitor.owner_valid || node->monitor.acq_head ||
        node->monitor.wait_head) {
        LeaveCriticalSection(&node->monitor.cs);
        LeaveCriticalSection(&g_monitor_table_cs);
        return;
    }

    *link = node->next;
    LeaveCriticalSection(&node->monitor.cs);
    LeaveCriticalSection(&g_monitor_table_cs);

    DeleteCriticalSection(&node->monitor.cs);
    free(node);
}

/// @brief True if the calling thread (`self`) currently owns monitor `m`.
static int monitor_is_owner(const RtMonitor *m, DWORD self) {
    return m->owner_valid && m->owner == self;
}

/// @brief Hand the lock to the head of the FIFO acquisition queue and wake it.
///
/// Called by `rt_monitor_exit` once the recursion count drops to
/// zero. Restores the new owner's prior recursion depth (used by
/// `Wait`, which releases an arbitrarily deep nesting and reclaims
/// it on wakeup) and signals their per-waiter condvar.
static void monitor_grant_next_waiter(RtMonitor *m) {
    RtMonitorWaiter *w = m->acq_head;
    if (!w)
        return;
    m->acq_head = w->next;
    if (!m->acq_head)
        m->acq_tail = NULL;

    m->owner = w->threadId;
    m->owner_valid = 1;
    m->recursion = w->desired_recursion;

    w->state = RT_MON_WAITER_ACQUIRED;
    WakeConditionVariable(&w->cv);
}

// ---------------------------------------------------------------------------
// FIFO queue helpers — append/remove on the singly-linked acquisition
// (lock-contention) and wait (condition-variable) queues. All callers
// already hold the monitor's critical section, so no extra locking is
// needed inside these helpers.
// ---------------------------------------------------------------------------

/// @brief Append `w` to the FIFO acquisition queue (lock contention).
static void monitor_enqueue_acq(RtMonitor *m, RtMonitorWaiter *w) {
    w->next = NULL;
    if (m->acq_tail) {
        m->acq_tail->next = w;
        m->acq_tail = w;
    } else {
        m->acq_head = w;
        m->acq_tail = w;
    }
}

/// @brief Splice `w` out of the acquisition queue (used on timeout/abort).
static void monitor_remove_acq(RtMonitor *m, RtMonitorWaiter *w) {
    RtMonitorWaiter *prev = NULL;
    RtMonitorWaiter *cur = m->acq_head;
    while (cur) {
        if (cur == w) {
            if (prev)
                prev->next = cur->next;
            else
                m->acq_head = cur->next;
            if (m->acq_tail == cur)
                m->acq_tail = prev;
            cur->next = NULL;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/// @brief Append `w` to the FIFO condition-wait queue (Wait / WaitFor).
static void monitor_enqueue_wait(RtMonitor *m, RtMonitorWaiter *w) {
    w->next = NULL;
    if (m->wait_tail) {
        m->wait_tail->next = w;
        m->wait_tail = w;
    } else {
        m->wait_head = w;
        m->wait_tail = w;
    }
}

/// @brief Splice `w` out of the wait queue (timeout, signal-then-cancel).
static void monitor_remove_wait(RtMonitor *m, RtMonitorWaiter *w) {
    RtMonitorWaiter *prev = NULL;
    RtMonitorWaiter *cur = m->wait_head;
    while (cur) {
        if (cur == w) {
            if (prev)
                prev->next = cur->next;
            else
                m->wait_head = cur->next;
            if (m->wait_tail == cur)
                m->wait_tail = prev;
            cur->next = NULL;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/// @brief Wake and detach every waiter in a monitor's acquire/wait queue with
///        a cancellation result (used when the monitor is destroyed or the
///        queue is torn down) so no thread is left blocked. Walks the whole
///        chain, freeing each waiter node.
/// @note Defined once per platform backend branch; both copies are identical.
static void monitor_cancel_queue(RtMonitorWaiter *w) {
    while (w) {
        RtMonitorWaiter *next = w->next;
        w->next = NULL;
        w->state = RT_MON_WAITER_CANCELLED;
        WakeConditionVariable(&w->cv);
        w = next;
    }
}

/// @brief Acquire monitor `m` for thread `self`, blocking up to `timeout_ms` if `timed`.
///
/// Three fast paths run before any blocking occurs:
///   1. Re-entry (`self` already owns) — bumps recursion and returns.
///   2. Uncontended (no owner, empty queue) — claims the lock immediately.
///   3. Contended — appends a `RtMonitorWaiter` to the FIFO acquisition
///      queue and sleeps on its per-waiter `CONDITION_VARIABLE` until
///      `monitor_grant_next_waiter` flips its state.
/// On timeout the waiter is spliced out and we return without owning
/// the lock. Traps with a "null monitor" message if `m` is NULL.
static int monitor_enter_blocking(RtMonitor *m, DWORD self, DWORD timeout_ms, int timed) {
    if (!m) {
        rt_trap("rt_monitor: null monitor");
        return 0;
    }
    if (m->retired) {
        rt_trap("Monitor.Enter: object finalized");
        return 0;
    }

    if (monitor_is_owner(m, self)) {
        if (m->recursion == SIZE_MAX) {
            rt_trap("Monitor.Enter: recursion overflow");
            return 0;
        }
        m->recursion += 1;
        return 1;
    }

    if (!m->owner_valid && m->acq_head == NULL) {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        return 1;
    }

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    ULONGLONG start = GetTickCount64();
    while (w.state != RT_MON_WAITER_ACQUIRED) {
        DWORD wait_time = INFINITE;
        if (timed) {
            ULONGLONG elapsed = GetTickCount64() - start;
            if (elapsed >= timeout_ms) {
                // Timeout
                if (w.state != RT_MON_WAITER_ACQUIRED) {
                    monitor_remove_acq(m, &w);
                    return 0;
                }
                break;
            }
            wait_time = (DWORD)(timeout_ms - elapsed);
        }

        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && w.state != RT_MON_WAITER_ACQUIRED) {
            DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT) {
                monitor_remove_acq(m, &w);
                return 0;
            }
            monitor_remove_acq(m, &w);
            rt_trap("Monitor.Enter: condition wait failed");
            return 0;
        }
        if (w.state == RT_MON_WAITER_CANCELLED) {
            rt_trap("Monitor.Enter: object finalized while waiting");
            return 0;
        }
    }
    return w.state == RT_MON_WAITER_ACQUIRED ? 1 : 0;
}

/// @brief Acquire the monitor lock on an object (blocks until available, supports reentrancy).
void rt_monitor_enter(void *obj) {
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return;
    rt_obj_retain_maybe(obj);
    RtMonitor *m = get_monitor_for(obj);
    if (!m) {
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        return;
    }
    EnterCriticalSection(&m->cs);
    char saved_error[256];
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        monitor_save_trap_error(saved_error, sizeof(saved_error), "Monitor.Enter: failed");
        rt_trap_clear_recovery();
        LeaveCriticalSection(&m->cs);
        monitor_release_enter_ref(obj);
        rt_trap(saved_error);
        return;
    }
    int acquired = monitor_enter_blocking(m, GetCurrentThreadId(), 0, 0);
    rt_trap_clear_recovery();
    LeaveCriticalSection(&m->cs);
    if (!acquired)
        monitor_release_enter_ref(obj);
}

/// @brief Try to acquire the monitor lock without blocking. Returns 1 if acquired, 0 if busy.
int8_t rt_monitor_try_enter(void *obj) {
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return 0;
    rt_obj_retain_maybe(obj);
    RtMonitor *m = get_monitor_for(obj);
    if (!m) {
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        return 0;
    }
    DWORD self = GetCurrentThreadId();

    EnterCriticalSection(&m->cs);
    if (m->retired) {
        LeaveCriticalSection(&m->cs);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        rt_trap("Monitor.Enter: object finalized");
        return 0;
    }
    if (monitor_is_owner(m, self)) {
        if (m->recursion == SIZE_MAX) {
            LeaveCriticalSection(&m->cs);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: recursion overflow");
            return 0;
        }
        m->recursion += 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL) {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }
    LeaveCriticalSection(&m->cs);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
    return 0;
}

/// @brief Try to acquire the monitor lock with a timeout. Returns 1 if acquired, 0 on timeout.
int8_t rt_monitor_try_enter_for(void *obj, int64_t ms) {
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return 0;
    if (ms < 0)
        ms = 0;
    rt_obj_retain_maybe(obj);
    RtMonitor *m = get_monitor_for(obj);
    if (!m) {
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        return 0;
    }
    DWORD self = GetCurrentThreadId();

    EnterCriticalSection(&m->cs);
    if (m->retired) {
        LeaveCriticalSection(&m->cs);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        rt_trap("Monitor.Enter: object finalized");
        return 0;
    }
    if (monitor_is_owner(m, self)) {
        if (m->recursion == SIZE_MAX) {
            LeaveCriticalSection(&m->cs);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: recursion overflow");
            return 0;
        }
        m->recursion += 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL) {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    ULONGLONG deadline = rt_win32_deadline_from_now_ms(ms);
    while (w.state != RT_MON_WAITER_ACQUIRED) {
        DWORD wait_time = rt_win32_wait_slice_until(deadline);
        if (wait_time == 0) {
            monitor_remove_acq(m, &w);
            LeaveCriticalSection(&m->cs);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            return 0;
        }

        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && w.state != RT_MON_WAITER_ACQUIRED) {
            DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT)
                continue;
            monitor_remove_acq(m, &w);
            LeaveCriticalSection(&m->cs);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: condition wait failed");
            return 0;
        }
        if (w.state == RT_MON_WAITER_CANCELLED) {
            LeaveCriticalSection(&m->cs);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: object finalized while waiting");
            return 0;
        }
    }

    LeaveCriticalSection(&m->cs);
    return 1;
}

/// @brief Release the monitor lock (must be called once for each enter, respects reentrancy).
void rt_monitor_exit(void *obj) {
    if (!obj)
        rt_trap("Monitor.Exit: null object");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();

    EnterCriticalSection(&m->cs);
    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Exit: not owner");
        return;
    }
    if (m->recursion > 1) {
        m->recursion -= 1;
        LeaveCriticalSection(&m->cs);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        return;
    }

    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);
    LeaveCriticalSection(&m->cs);
    monitor_cleanup_retired_if_idle(obj, m);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Release the lock and wait until another thread calls Pause/PauseAll on this monitor.
void rt_monitor_wait(void *obj) {
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Wait: not owner");
        return;
    }

    const size_t saved_recursion = m->recursion;

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    // Queue before releasing so Pause/PauseAll cannot miss this waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    int wait_failed = 0;
    while (w.state == RT_MON_WAITER_WAITING_PAUSE) {
        if (!SleepConditionVariableCS(&w.cv, &m->cs, INFINITE)) {
            wait_failed = 1;
            break;
        }
    }

    if (wait_failed && w.state == RT_MON_WAITER_WAITING_PAUSE) {
        monitor_remove_wait(m, &w);
        w.state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, &w);
        if (!m->owner_valid && m->acq_head)
            monitor_grant_next_waiter(m);
    }

    // Wait for re-acquisition.
    while (w.state != RT_MON_WAITER_ACQUIRED && w.state != RT_MON_WAITER_CANCELLED) {
        if (!SleepConditionVariableCS(&w.cv, &m->cs, INFINITE)) {
            wait_failed = 1;
            if (w.state == RT_MON_WAITER_WAITING_LOCK) {
                monitor_remove_acq(m, &w);
                if (!m->owner_valid && m->acq_head)
                    monitor_grant_next_waiter(m);
            }
            break;
        }
    }

    LeaveCriticalSection(&m->cs);
    if (w.state == RT_MON_WAITER_CANCELLED)
        rt_trap("Monitor.Wait: object finalized while waiting");
    else if (wait_failed)
        rt_trap("Monitor.Wait: condition wait failed");
}

/// @brief Wait with a timeout. Returns 1 if woken by Pause, 0 on timeout.
int8_t rt_monitor_wait_for(void *obj, int64_t ms) {
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Wait: not owner");
        return 0;
    }

    if (ms < 0)
        ms = 0;

    const size_t saved_recursion = m->recursion;

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    // Queue before releasing so Pause/PauseAll cannot miss this waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    int timed_out = 0;
    int wait_failed = 0;
    ULONGLONG deadline = rt_win32_deadline_from_now_ms(ms);
    while (w.state == RT_MON_WAITER_WAITING_PAUSE) {
        DWORD wait_time = rt_win32_wait_slice_until(deadline);
        if (wait_time == 0) {
            timed_out = 1;
            break;
        }
        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && w.state == RT_MON_WAITER_WAITING_PAUSE) {
            DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT)
                continue;
            wait_failed = 1;
            break;
        }
    }

    if (timed_out || wait_failed) {
        // Timeout while still in the wait queue: remove and begin fair re-acquire.
        monitor_remove_wait(m, &w);
        w.state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, &w);
        if (!m->owner_valid && m->acq_head)
            monitor_grant_next_waiter(m);
    }

    while (w.state != RT_MON_WAITER_ACQUIRED && w.state != RT_MON_WAITER_CANCELLED) {
        if (!SleepConditionVariableCS(&w.cv, &m->cs, INFINITE)) {
            wait_failed = 1;
            if (w.state == RT_MON_WAITER_WAITING_LOCK) {
                monitor_remove_acq(m, &w);
                if (!m->owner_valid && m->acq_head)
                    monitor_grant_next_waiter(m);
            }
            break;
        }
    }

    LeaveCriticalSection(&m->cs);
    if (w.state == RT_MON_WAITER_CANCELLED)
        rt_trap("Monitor.Wait: object finalized while waiting");
    else if (wait_failed)
        rt_trap("Monitor.Wait: condition wait failed");
    return (timed_out || wait_failed) ? 0 : 1;
}

/// @brief Wake one thread waiting on this monitor (signal/notify pattern).
void rt_monitor_pause(void *obj) {
    if (!obj)
        rt_trap("Monitor.Notify: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Notify: not owner");
        return;
    }

    RtMonitorWaiter *w = m->wait_head;
    if (w) {
        m->wait_head = w->next;
        if (!m->wait_head)
            m->wait_tail = NULL;
        w->next = NULL;

        w->state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, w);
        WakeConditionVariable(&w->cv);
    }

    LeaveCriticalSection(&m->cs);
}

/// @brief Wake all threads waiting on this monitor (broadcast/notify-all pattern).
void rt_monitor_pause_all(void *obj) {
    if (!obj)
        rt_trap("Monitor.NotifyAll: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.NotifyAll: not owner");
        return;
    }

    while (m->wait_head) {
        RtMonitorWaiter *w = m->wait_head;
        m->wait_head = w->next;
        if (!m->wait_head)
            m->wait_tail = NULL;
        w->next = NULL;

        w->state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, w);
        WakeConditionVariable(&w->cv);
    }

    LeaveCriticalSection(&m->cs);
}

// ViperDOS provides pthreads — falls through to the Unix/pthread
// implementation below.
#endif // defined(_WIN32)
