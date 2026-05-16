//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_monitor.c
// Purpose: Implements FIFO-fair, re-entrant Java-style monitors for the
//          Viper.Threads.Monitor class. Monitors are keyed by object address in
//          a global hash table. Provides Enter/Exit (mutual exclusion), Wait/
//          WaitFor (condition wait), and Pause/PauseAll (condition signal).
//
// Key invariants:
//   - Monitors are re-entrant: the same thread may call Enter multiple times;
//     each Enter must be balanced by an Exit before the lock is truly released.
//   - FIFO fairness: threads acquire the lock in the order they requested it.
//   - Wait atomically releases the lock and sleeps until Pause/PauseAll signals.
//   - Monitors are stored in a global table keyed by object pointer; the table
//     entry is created on first Enter and remains until the object is finalized.
//   - Win32 uses CRITICAL_SECTION + CONDITION_VARIABLE; POSIX uses pthreads.
//   - Exit without a corresponding Enter traps immediately.
//
// Ownership/Lifetime:
//   - Monitor table entries are allocated on first lock contention and freed
//     when the owning object is garbage collected.
//   - Callers do not own monitor objects; they are accessed by object address.
//
// Links: src/runtime/threads/rt_monitor.h (public API, via rt_threads.h),
//        src/runtime/threads/rt_threads.h (thread ID query),
//        src/runtime/threads/rt_safe_i64.h (uses monitors for thread-safety)
//
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Clamp an int64 millisecond timeout into the Win32 `DWORD` domain.
/// @details Win32 wait APIs (`WaitForSingleObject`, `SleepConditionVariableCS`,
///          etc.) take an unsigned 32-bit `DWORD` timeout with special values
///          `0` (return immediately) and `INFINITE == MAXDWORD == 0xFFFFFFFF`.
///          A naked `(DWORD)ms` cast on a negative int64 sign-extends into a
///          huge unsigned and causes a ~49-day hang; a value greater than
///          `MAXDWORD` truncates unpredictably. This helper maps:
///            - `ms <= 0` → 0 (poll / return immediately)
///            - `ms > MAXDWORD` → `MAXDWORD` (effectively infinite)
///            - otherwise the direct cast.
///          Called before every Win32 timed-wait entry.
static DWORD monitor_clamp_timeout_ms(int64_t ms) {
    if (ms <= 0)
        return 0;
    if (ms > (int64_t)MAXDWORD)
        return MAXDWORD;
    return (DWORD)ms;
}

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
        if (!ok && GetLastError() == ERROR_TIMEOUT && w.state != RT_MON_WAITER_ACQUIRED) {
            monitor_remove_acq(m, &w);
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
    int acquired = monitor_enter_blocking(m, GetCurrentThreadId(), 0, 0);
    LeaveCriticalSection(&m->cs);
    if (!acquired && rt_obj_release_check0(obj))
        rt_obj_free(obj);
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

    ULONGLONG start = GetTickCount64();
    DWORD timeout_ms = monitor_clamp_timeout_ms(ms);
    while (w.state != RT_MON_WAITER_ACQUIRED) {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeout_ms) {
            if (w.state != RT_MON_WAITER_ACQUIRED) {
                monitor_remove_acq(m, &w);
                LeaveCriticalSection(&m->cs);
                if (rt_obj_release_check0(obj))
                    rt_obj_free(obj);
                return 0;
            }
            break;
        }
        DWORD wait_time = (DWORD)(timeout_ms - elapsed);

        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && GetLastError() == ERROR_TIMEOUT && w.state != RT_MON_WAITER_ACQUIRED) {
            monitor_remove_acq(m, &w);
            LeaveCriticalSection(&m->cs);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
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

    while (w.state == RT_MON_WAITER_WAITING_PAUSE) {
        SleepConditionVariableCS(&w.cv, &m->cs, INFINITE);
    }

    // Wait for re-acquisition.
    while (w.state != RT_MON_WAITER_ACQUIRED && w.state != RT_MON_WAITER_CANCELLED) {
        SleepConditionVariableCS(&w.cv, &m->cs, INFINITE);
    }

    LeaveCriticalSection(&m->cs);
    if (w.state == RT_MON_WAITER_CANCELLED)
        rt_trap("Monitor.Wait: object finalized while waiting");
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
    ULONGLONG start = GetTickCount64();
    DWORD timeout_ms = monitor_clamp_timeout_ms(ms);
    while (w.state == RT_MON_WAITER_WAITING_PAUSE) {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeout_ms) {
            if (w.state == RT_MON_WAITER_WAITING_PAUSE) {
                timed_out = 1;
                break;
            }
            break;
        }
        DWORD wait_time = (DWORD)(timeout_ms - elapsed);
        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && GetLastError() == ERROR_TIMEOUT && w.state == RT_MON_WAITER_WAITING_PAUSE) {
            timed_out = 1;
            break;
        }
    }

    if (timed_out) {
        // Timeout while still in the wait queue: remove and begin fair re-acquire.
        monitor_remove_wait(m, &w);
        w.state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, &w);
        if (!m->owner_valid && m->acq_head)
            monitor_grant_next_waiter(m);
    }

    while (w.state != RT_MON_WAITER_ACQUIRED && w.state != RT_MON_WAITER_CANCELLED) {
        SleepConditionVariableCS(&w.cv, &m->cs, INFINITE);
    }

    LeaveCriticalSection(&m->cs);
    if (w.state == RT_MON_WAITER_CANCELLED)
        rt_trap("Monitor.Wait: object finalized while waiting");
    return timed_out ? 0 : 1;
}

/// @brief Wake one thread waiting on this monitor (signal/notify pattern).
void rt_monitor_pause(void *obj) {
    if (!obj)
        rt_trap("Monitor.Pause: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Pause: not owner");
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
        rt_trap("Monitor.PauseAll: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self)) {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.PauseAll: not owner");
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

#else

#include <pthread.h>
#if defined(__APPLE__)
extern int pthread_cond_timedwait_relative_np(pthread_cond_t *cond,
                                              pthread_mutex_t *mutex,
                                              const struct timespec *rel_time);
#endif

/// @brief Waiter state enumeration.
///
/// Tracks the state of a thread waiting on a monitor, used for the
/// FIFO-fair handoff mechanism.
enum {
    RT_MON_WAITER_WAITING_PAUSE = 0, ///< Thread called Wait(), waiting for Pause signal.
    RT_MON_WAITER_WAITING_LOCK = 1,  ///< Thread waiting to acquire the lock.
    RT_MON_WAITER_ACQUIRED = 2,      ///< Thread has been granted ownership.
    RT_MON_WAITER_CANCELLED = 3,     ///< Monitor was retired while the thread was waiting.
};

/// @brief Represents a thread waiting on a monitor.
///
/// Each waiting thread gets its own RtMonitorWaiter node with a personal
/// condition variable. This enables FIFO-fair wake-up: we can signal specific
/// threads in order rather than having all waiters race.
///
/// **State machine:**
/// ```
/// [Enter] ──┬──▶ WAITING_LOCK ──(granted)──▶ ACQUIRED
///           │
/// [Wait]  ──┴──▶ WAITING_PAUSE ──(Pause)──▶ WAITING_LOCK ──▶ ACQUIRED
/// ```
typedef struct RtMonitorWaiter {
    struct RtMonitorWaiter *next; ///< Next waiter in queue (singly linked).
    pthread_cond_t cv;            ///< Per-waiter condition variable.
    int8_t cond_uses_monotonic;   ///< Non-zero when cv uses CLOCK_MONOTONIC.
    pthread_t thread;             ///< The waiting thread's ID.
    int state;                    ///< Current state (from enum above).
    size_t desired_recursion;     ///< Recursion count to restore on acquisition.
} RtMonitorWaiter;

/// @brief The monitor state associated with an object.
///
/// Contains the mutex protecting the state, ownership info, recursion count,
/// and two queues: one for threads waiting to acquire the lock (acq_queue),
/// and one for threads that called Wait() (wait_queue).
///
/// **Memory layout:**
/// ```
/// RtMonitor:
/// ┌─────────────────────────────────────────────────────┐
/// │ mu            │ pthread mutex protecting this state │
/// ├───────────────┼─────────────────────────────────────┤
/// │ owner         │ pthread_t of current owner          │
/// │ owner_valid   │ 1 if owned, 0 if free               │
/// │ recursion     │ How many times owner called Enter() │
/// ├───────────────┼─────────────────────────────────────┤
/// │ acq_head/tail │ Queue of threads waiting to acquire │
/// │ wait_head/tail│ Queue of threads that called Wait() │
/// └───────────────┴─────────────────────────────────────┘
/// ```
typedef struct RtMonitor {
    pthread_mutex_t mu;         ///< Mutex protecting all monitor state.
    pthread_t owner;            ///< Current owner thread.
    int owner_valid;            ///< Non-zero if monitor is currently owned.
    size_t recursion;           ///< Re-entry count for owner.
    RtMonitorWaiter *acq_head;  ///< Head of acquisition queue.
    RtMonitorWaiter *acq_tail;  ///< Tail of acquisition queue.
    RtMonitorWaiter *wait_head; ///< Head of wait queue.
    RtMonitorWaiter *wait_tail; ///< Tail of wait queue.
    int retired;                ///< Monitor owner object is being finalized.
} RtMonitor;

/// @brief Hash table entry mapping object address to monitor.
///
/// Monitors are looked up by object address. The hash table uses separate
/// chaining for collision resolution.
typedef struct RtMonitorEntry {
    void *key;                   ///< Object address (hash key).
    int retired;                 ///< Object was finalized while monitor was busy.
    struct RtMonitorEntry *next; ///< Next entry in hash chain.
    RtMonitor monitor;           ///< The monitor state.
} RtMonitorEntry;

static void monitor_cancel_queue(RtMonitorWaiter *w);

#define RT_MONITOR_BUCKETS 4096u

static pthread_mutex_t g_monitor_table_mu = PTHREAD_MUTEX_INITIALIZER;
static RtMonitorEntry *g_monitor_table[RT_MONITOR_BUCKETS];

// ---------------------------------------------------------------------------
// POSIX backend — pthread_mutex_t + pthread_cond_t. Mirrors the
// Win32 helpers above; see those for FIFO-fairness rationale.
// ---------------------------------------------------------------------------

/// @brief Hash a pointer to a monitor-table bucket index (Knuth golden-ratio mix).
static size_t hash_ptr(void *p) {
    uintptr_t x = (uintptr_t)p;
    x >>= 4;
    x ^= x >> 16;
    x *= 0x9E3779B97F4A7C15ull;
    return (size_t)(x & (RT_MONITOR_BUCKETS - 1u));
}

/// @brief Locate (or lazily allocate) the monitor for `obj` (POSIX path).
/// @see Win32 `get_monitor_for` for the design rationale.
static RtMonitor *get_monitor_for(void *obj) {
    size_t idx = hash_ptr(obj);

    pthread_mutex_lock(&g_monitor_table_mu);
    RtMonitorEntry *it = g_monitor_table[idx];
    while (it) {
        if (it->key == obj && !it->retired) {
            pthread_mutex_unlock(&g_monitor_table_mu);
            return &it->monitor;
        }
        it = it->next;
    }

    RtMonitorEntry *node = (RtMonitorEntry *)calloc(1, sizeof(*node));
    if (!node) {
        pthread_mutex_unlock(&g_monitor_table_mu);
        rt_trap("rt_monitor: alloc failed");
        return NULL;
    }
    node->key = obj;
    node->next = g_monitor_table[idx];
    g_monitor_table[idx] = node;

    (void)pthread_mutex_init(&node->monitor.mu, NULL);

    pthread_mutex_unlock(&g_monitor_table_mu);
    return &node->monitor;
}

/// @brief Release the monitor associated with `obj` from the global table (POSIX).
///
/// Called from the GC finalizer of any object that has had a
/// monitor attached. Idempotent: silently no-ops if no entry exists.
void rt_monitor_forget(void *obj) {
    if (!obj)
        return;
    size_t idx = hash_ptr(obj);

    pthread_mutex_lock(&g_monitor_table_mu);
    RtMonitorEntry **link = &g_monitor_table[idx];
    RtMonitorEntry *node = *link;
    while (node && (node->key != obj || node->retired)) {
        link = &node->next;
        node = node->next;
    }
    if (!node) {
        pthread_mutex_unlock(&g_monitor_table_mu);
        return;
    }
    pthread_mutex_lock(&node->monitor.mu);
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
        pthread_mutex_unlock(&node->monitor.mu);
        pthread_mutex_unlock(&g_monitor_table_mu);
        return;
    }

    *link = node->next;
    pthread_mutex_unlock(&node->monitor.mu);
    pthread_mutex_unlock(&g_monitor_table_mu);

    (void)pthread_mutex_destroy(&node->monitor.mu);
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
    size_t idx = hash_ptr(obj);

    pthread_mutex_lock(&g_monitor_table_mu);
    RtMonitorEntry **link = &g_monitor_table[idx];
    RtMonitorEntry *node = *link;
    while (node && (node->key != obj || &node->monitor != monitor)) {
        link = &node->next;
        node = node->next;
    }
    if (!node) {
        pthread_mutex_unlock(&g_monitor_table_mu);
        return;
    }

    pthread_mutex_lock(&node->monitor.mu);
    if (!node->retired || node->monitor.owner_valid || node->monitor.acq_head ||
        node->monitor.wait_head) {
        pthread_mutex_unlock(&node->monitor.mu);
        pthread_mutex_unlock(&g_monitor_table_mu);
        return;
    }

    *link = node->next;
    pthread_mutex_unlock(&node->monitor.mu);
    pthread_mutex_unlock(&g_monitor_table_mu);

    (void)pthread_mutex_destroy(&node->monitor.mu);
    free(node);
}

/// @brief True if pthread `self` currently owns the monitor (POSIX equality).
static int monitor_is_owner(const RtMonitor *m, pthread_t self) {
    return m->owner_valid && pthread_equal(m->owner, self);
}

/// @brief Pop the FIFO acquisition queue head, hand it the lock, signal its condvar (POSIX).
static void monitor_grant_next_waiter(RtMonitor *m) {
    RtMonitorWaiter *w = m->acq_head;
    if (!w)
        return;
    m->acq_head = w->next;
    if (!m->acq_head)
        m->acq_tail = NULL;

    m->owner = w->thread;
    m->owner_valid = 1;
    m->recursion = w->desired_recursion;

    w->state = RT_MON_WAITER_ACQUIRED;
    pthread_cond_signal(&w->cv);
}

/// @brief POSIX FIFO append for the acquisition queue.
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

/// @brief POSIX queue removal — splice `w` out of the acquisition queue.
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

/// @brief POSIX FIFO append for the condition-wait queue.
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

/// @brief POSIX queue removal — splice `w` out of the wait queue.
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

static void monitor_cancel_queue(RtMonitorWaiter *w) {
    while (w) {
        RtMonitorWaiter *next = w->next;
        w->next = NULL;
        w->state = RT_MON_WAITER_CANCELLED;
        pthread_cond_signal(&w->cv);
        w = next;
    }
}

typedef struct {
    struct timespec deadline;
} monitor_deadline_t;

/// @brief Initialize a pthread condvar for monitor Wait, preferring CLOCK_MONOTONIC.
/// @details Same rationale as the ConcurrentQueue cq_cond_init: timed Wait
///          deadlines should be immune to wall-clock adjustments. macOS
///          uses pthread_cond_timedwait_relative_np (intrinsically
///          monotonic, so we report uses_monotonic=1 even though
///          condattr_setclock isn't available). Other POSIX platforms
///          fall back to CLOCK_REALTIME if condattr_setclock fails.
/// @return 0 on success, errno-style error code otherwise.
static int monitor_cond_init(pthread_cond_t *cond, int8_t *uses_monotonic) {
    if (uses_monotonic)
        *uses_monotonic = 0;
#if defined(__APPLE__)
    if (uses_monotonic)
        *uses_monotonic = 1;
    return pthread_cond_init(cond, NULL);
#elif defined(CLOCK_MONOTONIC)
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0)
        return pthread_cond_init(cond, NULL);
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0) {
        pthread_condattr_destroy(&attr);
        return pthread_cond_init(cond, NULL);
    }
    if (uses_monotonic)
        *uses_monotonic = 1;
    int rc = pthread_cond_init(cond, &attr);
    if (rc != 0 && uses_monotonic)
        *uses_monotonic = 0;
    pthread_condattr_destroy(&attr);
    return rc;
#else
    return pthread_cond_init(cond, NULL);
#endif
}

/// @brief Read the current clock, preferring the monotonic source when requested
///        and available.
/// @details Monitor timeouts and pthread condvar waits need a consistent clock
///          source. `CLOCK_MONOTONIC` is the right choice for "wait 500ms" because
///          it's immune to wall-clock adjustments (NTP sync, DST), but some
///          pthread implementations require `CLOCK_REALTIME` for `pthread_cond_timedwait`
///          with the default attributes — the caller picks which per-condvar.
///          On platforms without `CLOCK_MONOTONIC` (few, at this point) we fall
///          back to `CLOCK_REALTIME` silently. A stack-allocated `timespec` is
///          zero-initialized so a `clock_gettime` failure returns a sensible
///          "epoch" value rather than uninitialized stack garbage.
static struct timespec monitor_now_clock(int8_t use_monotonic) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
#ifdef CLOCK_MONOTONIC
    if (use_monotonic && clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return ts;
#endif
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

/// @brief Compute an absolute deadline `ms` milliseconds in the future from now,
///        using the caller-selected clock source.
/// @details Takes the current clock reading and adds the millisecond budget as
///          `(sec, nsec)`. Overflow-safe: before adding, checks that
///          `d.deadline.tv_sec + add_sec` doesn't exceed `LONG_MAX` — if it
///          would, clamps to `(LONG_MAX, 999999999ns)` so the deadline becomes
///          "effectively forever" rather than wrapping to a past value that
///          would trigger an immediate spurious timeout. Also normalizes
///          nanosecond overflow (`ns >= 1e9` → carry into seconds) so downstream
///          `pthread_cond_timedwait` doesn't see a malformed timespec. Negative
///          or zero `ms` returns the current time so a "no timeout" caller still
///          gets a valid struct.
static monitor_deadline_t monitor_deadline_ms_from_now(int64_t ms, int8_t use_monotonic) {
    monitor_deadline_t d;
    d.deadline = monitor_now_clock(use_monotonic);
    if (ms <= 0)
        return d;

    int64_t add_sec = ms / 1000;
    long add_nsec = (long)((ms % 1000) * 1000000L);
    int64_t sec_room = (int64_t)LONG_MAX - (int64_t)d.deadline.tv_sec;
    if (add_sec > sec_room ||
        (add_sec == sec_room && d.deadline.tv_nsec > 999999999L - add_nsec)) {
        d.deadline.tv_sec = (time_t)LONG_MAX;
        d.deadline.tv_nsec = 999999999L;
        return d;
    }
    int64_t sec = (int64_t)d.deadline.tv_sec + add_sec;
    int64_t ns = (int64_t)d.deadline.tv_nsec + add_nsec;
    if (ns >= 1000000000) {
        sec += 1;
        ns -= 1000000000;
    }
    d.deadline.tv_sec = (time_t)sec;
    d.deadline.tv_nsec = (long)ns;
    return d;
}

#if defined(__APPLE__)
/// @brief Return milliseconds remaining until @p deadline (macOS-only relative-wait helper).
/// @details Counterpart to cq_remaining_ms in rt_concqueue.c — used to
///          convert an absolute monitor-Wait deadline into a relative
///          timeout for pthread_cond_timedwait_relative_np. Returns 0 if
///          the deadline has lapsed; saturates at INT64_MAX for very
///          large remaining intervals.
static int64_t monitor_remaining_ms(monitor_deadline_t deadline, int8_t use_monotonic) {
    struct timespec now = monitor_now_clock(use_monotonic);
    int64_t sec = (int64_t)deadline.deadline.tv_sec - (int64_t)now.tv_sec;
    int64_t ns = (int64_t)deadline.deadline.tv_nsec - (int64_t)now.tv_nsec;
    if (ns < 0) {
        sec--;
        ns += 1000000000L;
    }
    if (sec < 0)
        return 0;
    if (sec > INT64_MAX / 1000)
        return INT64_MAX;
    return sec * 1000 + ns / 1000000L;
}
#endif

/// @brief Cross-platform pthread_cond_timedwait against an absolute monitor-Wait deadline.
/// @details Per-platform: on macOS, computes a relative timeout via
///          monitor_remaining_ms and calls
///          pthread_cond_timedwait_relative_np; returns ETIMEDOUT if the
///          deadline has lapsed. On Linux/POSIX, calls the standard
///          pthread_cond_timedwait with the absolute timespec stored in
///          the deadline. Caller must hold the monitor mutex.
static int monitor_cond_timedwait_deadline(pthread_cond_t *cond,
                                           pthread_mutex_t *mutex,
                                           monitor_deadline_t deadline,
                                           int8_t use_monotonic) {
#if defined(__APPLE__)
    int64_t remaining = monitor_remaining_ms(deadline, use_monotonic);
    if (remaining <= 0)
        return ETIMEDOUT;
    struct timespec rel;
    rel.tv_sec = (time_t)(remaining / 1000);
    rel.tv_nsec = (long)((remaining % 1000) * 1000000L);
    return pthread_cond_timedwait_relative_np(cond, mutex, &rel);
#else
    (void)use_monotonic;
    return pthread_cond_timedwait(cond, mutex, &deadline.deadline);
#endif
}

/// @brief POSIX equivalent of the Win32 `monitor_enter_blocking`.
///
/// Uses pthread mutex + per-waiter condvar. Identical fast-paths
/// (re-entry, uncontended) and identical FIFO-fairness contract;
/// see the Win32 version for the design rationale.
static int monitor_enter_blocking(RtMonitor *m, pthread_t self, int64_t timeout_ms, int timed) {
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
    if (monitor_cond_init(&w.cv, &w.cond_uses_monotonic) != 0) {
        rt_trap("Monitor.Enter: condition init failed");
        return 0;
    }
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    int rc = 0;
    const monitor_deadline_t deadline =
        monitor_deadline_ms_from_now(timeout_ms, w.cond_uses_monotonic);
    while (w.state != RT_MON_WAITER_ACQUIRED) {
        if (!timed) {
            rc = pthread_cond_wait(&w.cv, &m->mu);
        } else {
            rc = monitor_cond_timedwait_deadline(
                &w.cv, &m->mu, deadline, w.cond_uses_monotonic);
        }

        if (timed && rc == ETIMEDOUT && w.state != RT_MON_WAITER_ACQUIRED) {
            monitor_remove_acq(m, &w);
            pthread_cond_destroy(&w.cv);
            return 0;
        }
        if (w.state == RT_MON_WAITER_CANCELLED) {
            pthread_cond_destroy(&w.cv);
            rt_trap("Monitor.Enter: object finalized while waiting");
            return 0;
        }
    }

    pthread_cond_destroy(&w.cv);
    return w.state == RT_MON_WAITER_ACQUIRED ? 1 : 0;
}

/// @brief Acquires exclusive access to an object's monitor.
///
/// Blocks until the calling thread can acquire exclusive ownership of the
/// monitor. If the monitor is free, acquires immediately. If another thread
/// owns it, waits in a FIFO queue until granted ownership.
///
/// Re-entrancy: If the calling thread already owns the monitor, the recursion
/// count is incremented and the call returns immediately.
///
/// **Example:**
/// ```
/// Monitor.Enter(sharedData)
/// Try
///     ' Critical section - exclusive access guaranteed
///     sharedData.Update()
/// Finally
///     Monitor.Exit(sharedData)
/// End Try
/// ```
///
/// @param obj The object whose monitor to acquire. Must not be NULL.
///
/// @note Traps if obj is NULL.
/// @note Each Enter() must be balanced by a corresponding Exit().
/// @note Blocks indefinitely - use TryEnter or TryEnterFor for timeouts.
/// @note FIFO-fair: threads acquire in the order they called Enter().
///
/// @see rt_monitor_exit For releasing the monitor
/// @see rt_monitor_try_enter For non-blocking acquisition
/// @see rt_monitor_try_enter_for For acquisition with timeout
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
    pthread_mutex_lock(&m->mu);
    int acquired = monitor_enter_blocking(m, pthread_self(), /*timeout_ms=*/0, /*timed=*/0);
    pthread_mutex_unlock(&m->mu);
    if (!acquired && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Attempts to acquire a monitor without blocking.
///
/// Tries to acquire the monitor immediately. If successful, returns true and
/// the calling thread now owns the monitor. If another thread owns it or
/// threads are waiting, returns false without blocking.
///
/// **Example:**
/// ```
/// If Monitor.TryEnter(resource) Then
///     Try
///         UseResource(resource)
///     Finally
///         Monitor.Exit(resource)
///     End Try
/// Else
///     ' Resource busy, do something else
///     DoAlternateWork()
/// End If
/// ```
///
/// @param obj The object whose monitor to try acquiring. Must not be NULL.
///
/// @return 1 (true) if the monitor was acquired, 0 (false) if it's busy.
///
/// @note Traps if obj is NULL.
/// @note If already owner, increments recursion and returns true.
/// @note Never blocks - returns immediately.
///
/// @see rt_monitor_enter For blocking acquisition
/// @see rt_monitor_try_enter_for For acquisition with timeout
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
    pthread_t self = pthread_self();

    pthread_mutex_lock(&m->mu);
    if (m->retired) {
        pthread_mutex_unlock(&m->mu);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        rt_trap("Monitor.Enter: object finalized");
        return 0;
    }
    if (monitor_is_owner(m, self)) {
        if (m->recursion == SIZE_MAX) {
            pthread_mutex_unlock(&m->mu);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: recursion overflow");
            return 0;
        }
        m->recursion += 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL) {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }
    pthread_mutex_unlock(&m->mu);
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
    pthread_t self = pthread_self();

    pthread_mutex_lock(&m->mu);
    if (m->retired) {
        pthread_mutex_unlock(&m->mu);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        rt_trap("Monitor.Enter: object finalized");
        return 0;
    }
    if (monitor_is_owner(m, self)) {
        if (m->recursion == SIZE_MAX) {
            pthread_mutex_unlock(&m->mu);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: recursion overflow");
            return 0;
        }
        m->recursion += 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL) {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }

    RtMonitorWaiter w = {0};
    if (monitor_cond_init(&w.cv, &w.cond_uses_monotonic) != 0) {
        pthread_mutex_unlock(&m->mu);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        rt_trap("Monitor.Enter: condition init failed");
        return 0;
    }
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    const monitor_deadline_t deadline = monitor_deadline_ms_from_now(ms, w.cond_uses_monotonic);
    int rc = 0;
    while (w.state != RT_MON_WAITER_ACQUIRED) {
        rc = monitor_cond_timedwait_deadline(&w.cv, &m->mu, deadline, w.cond_uses_monotonic);
        if (rc == ETIMEDOUT && w.state != RT_MON_WAITER_ACQUIRED) {
            monitor_remove_acq(m, &w);
            pthread_cond_destroy(&w.cv);
            pthread_mutex_unlock(&m->mu);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            return 0;
        }
        if (w.state == RT_MON_WAITER_CANCELLED) {
            pthread_cond_destroy(&w.cv);
            pthread_mutex_unlock(&m->mu);
            if (rt_obj_release_check0(obj))
                rt_obj_free(obj);
            rt_trap("Monitor.Enter: object finalized while waiting");
            return 0;
        }
    }

    pthread_cond_destroy(&w.cv);
    pthread_mutex_unlock(&m->mu);
    return 1;
}

/// @brief Releases the monitor, allowing other threads to acquire it.
///
/// Releases one level of ownership of the monitor. If the calling thread
/// entered the monitor multiple times (re-entrancy), only decrements the
/// recursion count. When recursion reaches zero, releases completely and
/// wakes the next waiting thread if any.
///
/// **Example:**
/// ```
/// Monitor.Enter(obj)
/// Try
///     DoWork()
/// Finally
///     Monitor.Exit(obj)  ' Always exit, even on exception
/// End Try
/// ```
///
/// @param obj The object whose monitor to release. Must not be NULL.
///
/// @note Traps if obj is NULL.
/// @note Traps if the calling thread doesn't own the monitor.
/// @note FIFO-fair: wakes the thread that has been waiting longest.
///
/// @see rt_monitor_enter For acquiring the monitor
/// @brief Release the monitor lock (must be called once for each enter, respects reentrancy).
void rt_monitor_exit(void *obj) {
    if (!obj)
        rt_trap("Monitor.Exit: null object");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();

    pthread_mutex_lock(&m->mu);
    if (!monitor_is_owner(m, self)) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Exit: not owner");
        return;
    }
    if (m->recursion > 1) {
        m->recursion -= 1;
        pthread_mutex_unlock(&m->mu);
        if (rt_obj_release_check0(obj))
            rt_obj_free(obj);
        return;
    }

    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);
    pthread_mutex_unlock(&m->mu);
    monitor_cleanup_retired_if_idle(obj, m);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Releases the monitor and waits for a Pause signal.
///
/// Atomically releases the monitor and enters a wait state. The thread
/// remains blocked until another thread calls Monitor.Pause() or
/// Monitor.PauseAll() on the same object. When signaled, the thread
/// re-acquires the monitor before returning.
///
/// **Producer/Consumer Example:**
/// ```
/// ' Consumer thread
/// Monitor.Enter(queue)
/// While queue.IsEmpty()
///     Monitor.Wait(queue)  ' Release lock and wait
/// Wend
/// Dim item = queue.Remove()
/// Monitor.Exit(queue)
/// ```
///
/// **Workflow:**
/// 1. Saves the current recursion count
/// 2. Fully releases the monitor (recursion → 0)
/// 3. Grants ownership to next thread waiting to acquire
/// 4. Joins the wait queue
/// 5. Blocks until Pause/PauseAll signals this thread
/// 6. Moves to acquisition queue
/// 7. Re-acquires the monitor (restoring recursion count)
/// 8. Returns to caller
///
/// @param obj The object whose monitor to wait on. Must be owned by caller.
///
/// @note Traps if obj is NULL.
/// @note Traps if the calling thread doesn't own the monitor.
/// @note The monitor is always re-acquired before this function returns.
/// @note Use WaitFor() for a timed wait.
///
/// @see rt_monitor_pause For waking one waiting thread
/// @see rt_monitor_pause_all For waking all waiting threads
/// @see rt_monitor_wait_for For waiting with timeout
/// @brief Release the lock and wait until another thread calls Pause/PauseAll on this monitor.
void rt_monitor_wait(void *obj) {
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self)) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Wait: not owner");
        return;
    }

    const size_t saved_recursion = m->recursion;

    RtMonitorWaiter w = {0};
    if (monitor_cond_init(&w.cv, &w.cond_uses_monotonic) != 0) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Wait: condition init failed");
        return;
    }
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    // Queue before releasing so Pause/PauseAll cannot miss this waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    while (w.state == RT_MON_WAITER_WAITING_PAUSE) {
        (void)pthread_cond_wait(&w.cv, &m->mu);
    }

    // Wait for re-acquisition.
    while (w.state != RT_MON_WAITER_ACQUIRED && w.state != RT_MON_WAITER_CANCELLED) {
        (void)pthread_cond_wait(&w.cv, &m->mu);
    }

    pthread_cond_destroy(&w.cv);
    pthread_mutex_unlock(&m->mu);
    if (w.state == RT_MON_WAITER_CANCELLED)
        rt_trap("Monitor.Wait: object finalized while waiting");
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
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self)) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Wait: not owner");
        return 0;
    }

    if (ms < 0)
        ms = 0;

    const size_t saved_recursion = m->recursion;

    RtMonitorWaiter w = {0};
    if (monitor_cond_init(&w.cv, &w.cond_uses_monotonic) != 0) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Wait: condition init failed");
        return 0;
    }
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    // Queue before releasing so Pause/PauseAll cannot miss this waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    int timed_out = 0;
    const monitor_deadline_t deadline = monitor_deadline_ms_from_now(ms, w.cond_uses_monotonic);
    while (w.state == RT_MON_WAITER_WAITING_PAUSE) {
        const int rc =
            monitor_cond_timedwait_deadline(&w.cv, &m->mu, deadline, w.cond_uses_monotonic);
        if (rc == ETIMEDOUT && w.state == RT_MON_WAITER_WAITING_PAUSE) {
            timed_out = 1;
            break;
        }
    }

    if (timed_out) {
        // Timeout while still in the wait queue: remove and begin fair re-acquire.
        monitor_remove_wait(m, &w);
        w.state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, &w);
        if (!m->owner_valid && m->acq_head)
            monitor_grant_next_waiter(m);
    }

    while (w.state != RT_MON_WAITER_ACQUIRED && w.state != RT_MON_WAITER_CANCELLED) {
        (void)pthread_cond_wait(&w.cv, &m->mu);
    }

    pthread_cond_destroy(&w.cv);
    pthread_mutex_unlock(&m->mu);
    if (w.state == RT_MON_WAITER_CANCELLED)
        rt_trap("Monitor.Wait: object finalized while waiting");
    return timed_out ? 0 : 1;
}

/// @brief Wakes one thread waiting on the monitor.
///
/// Moves the oldest thread from the wait queue (threads that called Wait())
/// to the acquisition queue. The woken thread will re-acquire the monitor
/// after the current owner releases it.
///
/// **Producer/Consumer Example:**
/// ```
/// ' Producer thread
/// Monitor.Enter(queue)
/// queue.Add(item)
/// Monitor.Pause(queue)  ' Wake one consumer
/// Monitor.Exit(queue)
/// ```
///
/// **Behavior:**
/// - If no threads are waiting, this is a no-op (no error)
/// - The woken thread doesn't run immediately - it waits to acquire the lock
/// - The current thread keeps the monitor until it calls Exit()
/// - FIFO-fair: wakes the thread that has been waiting longest
///
/// @param obj The object whose waiting threads to signal. Must be owned by caller.
///
/// @note Traps if obj is NULL.
/// @note Traps if the calling thread doesn't own the monitor.
/// @note Does nothing if no threads are waiting.
/// @note The caller still holds the monitor after this call.
///
/// @see rt_monitor_pause_all For waking all waiting threads
/// @see rt_monitor_wait For entering the wait state
/// @brief Wake one thread waiting on this monitor (signal/notify pattern).
void rt_monitor_pause(void *obj) {
    if (!obj)
        rt_trap("Monitor.Pause: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self)) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Pause: not owner");
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
        pthread_cond_signal(&w->cv);
    }

    pthread_mutex_unlock(&m->mu);
}

/// @brief Wakes all threads waiting on the monitor.
///
/// Moves all threads from the wait queue to the acquisition queue. All woken
/// threads will compete to re-acquire the monitor in FIFO order after the
/// current owner releases it.
///
/// **Broadcast Example:**
/// ```
/// ' Signal all consumers that data is ready
/// Monitor.Enter(queue)
/// dataReady = True
/// Monitor.PauseAll(queue)  ' Wake all waiters
/// Monitor.Exit(queue)
/// ```
///
/// **When to use PauseAll vs Pause:**
/// - Use Pause() when any one waiter can handle the condition
/// - Use PauseAll() when the condition might affect multiple waiters
/// - Use PauseAll() for broadcast notifications (state changes)
/// - PauseAll() is safer but may cause more contention
///
/// @param obj The object whose waiting threads to signal. Must be owned by caller.
///
/// @note Traps if obj is NULL.
/// @note Traps if the calling thread doesn't own the monitor.
/// @note Does nothing if no threads are waiting.
/// @note The caller still holds the monitor after this call.
/// @note All woken threads will compete for the lock in FIFO order.
///
/// @see rt_monitor_pause For waking just one thread
/// @see rt_monitor_wait For entering the wait state
/// @brief Wake all threads waiting on this monitor (broadcast/notify-all pattern).
void rt_monitor_pause_all(void *obj) {
    if (!obj)
        rt_trap("Monitor.PauseAll: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self)) {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.PauseAll: not owner");
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
        pthread_cond_signal(&w->cv);
    }

    pthread_mutex_unlock(&m->mu);
}

#endif
