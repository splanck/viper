//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_monitor.c
/// @brief FIFO-fair, re-entrant monitor implementation for Viper.Threads.Monitor.
///
/// This file implements Java-style monitors for Viper programs, providing
/// mutual exclusion and condition variable semantics. Monitors are associated
/// with objects (any Viper reference type) and provide thread synchronization
/// without explicit lock objects.
///
/// **What is a Monitor?**
/// A monitor is a synchronization primitive that combines:
/// 1. A mutex (for mutual exclusion)
/// 2. A condition variable (for wait/notify semantics)
/// 3. Re-entrancy (same thread can acquire multiple times)
/// 4. FIFO fairness (threads acquire in order they requested)
///
/// **Monitor Operations:**
/// ```
/// ┌─────────────────────────────────────────────────────────────────────┐
/// │ Operation       │ Description                                       │
/// ├─────────────────────────────────────────────────────────────────────┤
/// │ Enter(obj)      │ Acquire exclusive access (blocks if needed)       │
/// │ TryEnter(obj)   │ Try to acquire without blocking (returns bool)    │
/// │ TryEnterFor(ms) │ Try to acquire with timeout                       │
/// │ Exit(obj)       │ Release exclusive access                          │
/// │ Wait(obj)       │ Release lock and wait for Pause signal            │
/// │ WaitFor(obj,ms) │ Wait with timeout                                 │
/// │ Pause(obj)      │ Wake one waiting thread                           │
/// │ PauseAll(obj)   │ Wake all waiting threads                          │
/// └─────────────────────────────────────────────────────────────────────┘
/// ```
///
/// **Usage Example - Critical Section:**
/// ```
/// ' Thread-safe access to shared data
/// Monitor.Enter(sharedList)
/// Try
///     sharedList.Add(item)
/// Finally
///     Monitor.Exit(sharedList)
/// End Try
/// ```
///
/// **Usage Example - Producer/Consumer:**
/// ```
/// ' Producer thread
/// Monitor.Enter(queue)
/// queue.Add(item)
/// Monitor.Pause(queue)  ' Wake a consumer
/// Monitor.Exit(queue)
///
/// ' Consumer thread
/// Monitor.Enter(queue)
/// While queue.IsEmpty()
///     Monitor.Wait(queue)  ' Wait for items
/// Wend
/// Dim item = queue.Remove()
/// Monitor.Exit(queue)
/// ```
///
/// **Re-entrancy:**
/// The same thread can call Enter() multiple times on the same object.
/// Each Enter() must be balanced by a corresponding Exit().
/// ```
/// Monitor.Enter(obj)   ' recursion = 1
/// Monitor.Enter(obj)   ' recursion = 2 (same thread, OK)
/// DoWork()
/// Monitor.Exit(obj)    ' recursion = 1
/// Monitor.Exit(obj)    ' recursion = 0, released
/// ```
///
/// **FIFO Fairness:**
/// Threads acquire the monitor in the order they requested it. This prevents
/// starvation where a frequently-releasing thread could monopolize access.
/// ```
/// Thread A: Enter()           → acquires immediately (no contention)
/// Thread B: Enter()           → waits (A holds lock)
/// Thread C: Enter()           → waits (queue: B, C)
/// Thread A: Exit()            → B acquires (was first in queue)
/// Thread B: Exit()            → C acquires (was next in queue)
/// ```
///
/// **Implementation Notes:**
/// - Monitors are stored in a global hash table keyed by object address
/// - Uses pthreads mutex and condition variables internally
/// - Each waiting thread has its own condition variable for fairness
/// - Two wait queues: acq_queue (waiting for lock), wait_queue (called Wait)
///
/// **Platform Support:**
/// | Platform | Status                     |
/// |----------|----------------------------|
/// | macOS    | Full support (pthreads)    |
/// | Linux    | Full support (pthreads)    |
/// | Windows  | Full support (Win32 API)   |
///
/// @see rt_threads.c For thread creation and joining
/// @see rt_safe_i64.c For thread-safe integer operations using monitors
///
//===----------------------------------------------------------------------===//

#include "rt_threads.h"

#include "rt_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Waiter state enumeration for Windows.
enum
{
    RT_MON_WAITER_WAITING_PAUSE = 0, ///< Thread called Wait(), waiting for Pause signal.
    RT_MON_WAITER_WAITING_LOCK = 1,  ///< Thread waiting to acquire the lock.
    RT_MON_WAITER_ACQUIRED = 2,      ///< Thread has been granted ownership.
};

/// @brief Represents a thread waiting on a monitor (Windows version).
typedef struct RtMonitorWaiter
{
    struct RtMonitorWaiter *next; ///< Next waiter in queue (singly linked).
    CONDITION_VARIABLE cv;        ///< Per-waiter condition variable.
    DWORD threadId;               ///< The waiting thread's ID.
    int state;                    ///< Current state (from enum above).
    size_t desired_recursion;     ///< Recursion count to restore on acquisition.
} RtMonitorWaiter;

/// @brief The monitor state associated with an object (Windows version).
typedef struct RtMonitor
{
    CRITICAL_SECTION cs;        ///< Critical section protecting all monitor state.
    DWORD owner;                ///< Current owner thread ID.
    int owner_valid;            ///< Non-zero if monitor is currently owned.
    size_t recursion;           ///< Re-entry count for owner.
    RtMonitorWaiter *acq_head;  ///< Head of acquisition queue.
    RtMonitorWaiter *acq_tail;  ///< Tail of acquisition queue.
    RtMonitorWaiter *wait_head; ///< Head of wait queue.
    RtMonitorWaiter *wait_tail; ///< Tail of wait queue.
} RtMonitor;

/// @brief Hash table entry mapping object address to monitor.
typedef struct RtMonitorEntry
{
    void *key;                   ///< Object address (hash key).
    struct RtMonitorEntry *next; ///< Next entry in hash chain.
    RtMonitor monitor;           ///< The monitor state.
} RtMonitorEntry;

#define RT_MONITOR_BUCKETS 4096u

static CRITICAL_SECTION g_monitor_table_cs;
static int g_monitor_table_cs_init = 0;
static RtMonitorEntry *g_monitor_table[RT_MONITOR_BUCKETS];

static void ensure_table_cs_init(void)
{
    // Simple one-time init; ok for single-threaded startup
    if (!g_monitor_table_cs_init)
    {
        InitializeCriticalSection(&g_monitor_table_cs);
        g_monitor_table_cs_init = 1;
    }
}

static size_t hash_ptr(void *p)
{
    uintptr_t x = (uintptr_t)p;
    x >>= 4;
    x ^= x >> 16;
    x *= 0x9E3779B97F4A7C15ull;
    return (size_t)(x & (RT_MONITOR_BUCKETS - 1u));
}

static RtMonitor *get_monitor_for(void *obj)
{
    ensure_table_cs_init();
    size_t idx = hash_ptr(obj);

    EnterCriticalSection(&g_monitor_table_cs);
    RtMonitorEntry *it = g_monitor_table[idx];
    while (it)
    {
        if (it->key == obj)
        {
            LeaveCriticalSection(&g_monitor_table_cs);
            return &it->monitor;
        }
        it = it->next;
    }

    RtMonitorEntry *node = (RtMonitorEntry *)calloc(1, sizeof(*node));
    if (!node)
    {
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

static int monitor_is_owner(const RtMonitor *m, DWORD self)
{
    return m->owner_valid && m->owner == self;
}

static void monitor_grant_next_waiter(RtMonitor *m)
{
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

static void monitor_enqueue_acq(RtMonitor *m, RtMonitorWaiter *w)
{
    w->next = NULL;
    if (m->acq_tail)
    {
        m->acq_tail->next = w;
        m->acq_tail = w;
    }
    else
    {
        m->acq_head = w;
        m->acq_tail = w;
    }
}

static void monitor_remove_acq(RtMonitor *m, RtMonitorWaiter *w)
{
    RtMonitorWaiter *prev = NULL;
    RtMonitorWaiter *cur = m->acq_head;
    while (cur)
    {
        if (cur == w)
        {
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

static void monitor_enqueue_wait(RtMonitor *m, RtMonitorWaiter *w)
{
    w->next = NULL;
    if (m->wait_tail)
    {
        m->wait_tail->next = w;
        m->wait_tail = w;
    }
    else
    {
        m->wait_head = w;
        m->wait_tail = w;
    }
}

static void monitor_remove_wait(RtMonitor *m, RtMonitorWaiter *w)
{
    RtMonitorWaiter *prev = NULL;
    RtMonitorWaiter *cur = m->wait_head;
    while (cur)
    {
        if (cur == w)
        {
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

static void monitor_enter_blocking(RtMonitor *m, DWORD self, DWORD timeout_ms, int timed)
{
    if (!m)
    {
        rt_trap("rt_monitor: null monitor");
        return;
    }

    if (monitor_is_owner(m, self))
    {
        m->recursion += 1;
        return;
    }

    if (!m->owner_valid && m->acq_head == NULL)
    {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        return;
    }

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    ULONGLONG start = GetTickCount64();
    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        DWORD wait_time = INFINITE;
        if (timed)
        {
            ULONGLONG elapsed = GetTickCount64() - start;
            if (elapsed >= timeout_ms)
            {
                // Timeout
                if (w.state != RT_MON_WAITER_ACQUIRED)
                {
                    monitor_remove_acq(m, &w);
                    return;
                }
                break;
            }
            wait_time = (DWORD)(timeout_ms - elapsed);
        }

        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && GetLastError() == ERROR_TIMEOUT && w.state != RT_MON_WAITER_ACQUIRED)
        {
            monitor_remove_acq(m, &w);
            return;
        }
    }
}

void rt_monitor_enter(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    EnterCriticalSection(&m->cs);
    monitor_enter_blocking(m, GetCurrentThreadId(), 0, 0);
    LeaveCriticalSection(&m->cs);
}

int8_t rt_monitor_try_enter(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    DWORD self = GetCurrentThreadId();

    EnterCriticalSection(&m->cs);
    if (monitor_is_owner(m, self))
    {
        m->recursion += 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL)
    {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }
    LeaveCriticalSection(&m->cs);
    return 0;
}

int8_t rt_monitor_try_enter_for(void *obj, int64_t ms)
{
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return 0;
    if (ms < 0)
        ms = 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    DWORD self = GetCurrentThreadId();

    EnterCriticalSection(&m->cs);
    if (monitor_is_owner(m, self))
    {
        m->recursion += 1;
        LeaveCriticalSection(&m->cs);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL)
    {
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
    DWORD timeout_ms = (DWORD)ms;
    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeout_ms)
        {
            if (w.state != RT_MON_WAITER_ACQUIRED)
            {
                monitor_remove_acq(m, &w);
                LeaveCriticalSection(&m->cs);
                return 0;
            }
            break;
        }
        DWORD wait_time = (DWORD)(timeout_ms - elapsed);

        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && GetLastError() == ERROR_TIMEOUT && w.state != RT_MON_WAITER_ACQUIRED)
        {
            monitor_remove_acq(m, &w);
            LeaveCriticalSection(&m->cs);
            return 0;
        }
    }

    LeaveCriticalSection(&m->cs);
    return 1;
}

void rt_monitor_exit(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Exit: null object");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();

    EnterCriticalSection(&m->cs);
    if (!monitor_is_owner(m, self))
    {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Exit: not owner");
        return;
    }
    if (m->recursion > 1)
    {
        m->recursion -= 1;
        LeaveCriticalSection(&m->cs);
        return;
    }

    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);
    LeaveCriticalSection(&m->cs);
}

void rt_monitor_wait(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self))
    {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Wait: not owner");
        return;
    }

    const size_t saved_recursion = m->recursion;

    // Release the monitor fully and hand off to the next waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    while (w.state == RT_MON_WAITER_WAITING_PAUSE)
    {
        SleepConditionVariableCS(&w.cv, &m->cs, INFINITE);
    }

    // Wait for re-acquisition.
    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        SleepConditionVariableCS(&w.cv, &m->cs, INFINITE);
    }

    LeaveCriticalSection(&m->cs);
}

int8_t rt_monitor_wait_for(void *obj, int64_t ms)
{
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self))
    {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Wait: not owner");
        return 0;
    }

    if (ms < 0)
        ms = 0;

    const size_t saved_recursion = m->recursion;

    // Release the monitor fully and hand off to the next waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    RtMonitorWaiter w = {0};
    InitializeConditionVariable(&w.cv);
    w.threadId = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    int timed_out = 0;
    ULONGLONG start = GetTickCount64();
    DWORD timeout_ms = (DWORD)ms;
    while (w.state == RT_MON_WAITER_WAITING_PAUSE)
    {
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeout_ms)
        {
            if (w.state == RT_MON_WAITER_WAITING_PAUSE)
            {
                timed_out = 1;
                break;
            }
            break;
        }
        DWORD wait_time = (DWORD)(timeout_ms - elapsed);
        BOOL ok = SleepConditionVariableCS(&w.cv, &m->cs, wait_time);
        if (!ok && GetLastError() == ERROR_TIMEOUT && w.state == RT_MON_WAITER_WAITING_PAUSE)
        {
            timed_out = 1;
            break;
        }
    }

    if (timed_out)
    {
        // Timeout while still in the wait queue: remove and begin fair re-acquire.
        monitor_remove_wait(m, &w);
        w.state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, &w);
        if (!m->owner_valid && m->acq_head)
            monitor_grant_next_waiter(m);
    }

    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        SleepConditionVariableCS(&w.cv, &m->cs, INFINITE);
    }

    LeaveCriticalSection(&m->cs);
    return timed_out ? 0 : 1;
}

void rt_monitor_pause(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Pause: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self))
    {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.Pause: not owner");
        return;
    }

    RtMonitorWaiter *w = m->wait_head;
    if (w)
    {
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

void rt_monitor_pause_all(void *obj)
{
    if (!obj)
        rt_trap("Monitor.PauseAll: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    DWORD self = GetCurrentThreadId();
    EnterCriticalSection(&m->cs);

    if (!monitor_is_owner(m, self))
    {
        LeaveCriticalSection(&m->cs);
        rt_trap("Monitor.PauseAll: not owner");
        return;
    }

    while (m->wait_head)
    {
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

/// @brief Waiter state enumeration.
///
/// Tracks the state of a thread waiting on a monitor, used for the
/// FIFO-fair handoff mechanism.
enum
{
    RT_MON_WAITER_WAITING_PAUSE = 0, ///< Thread called Wait(), waiting for Pause signal.
    RT_MON_WAITER_WAITING_LOCK = 1,  ///< Thread waiting to acquire the lock.
    RT_MON_WAITER_ACQUIRED = 2,      ///< Thread has been granted ownership.
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
typedef struct RtMonitorWaiter
{
    struct RtMonitorWaiter *next; ///< Next waiter in queue (singly linked).
    pthread_cond_t cv;            ///< Per-waiter condition variable.
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
typedef struct RtMonitor
{
    pthread_mutex_t mu;         ///< Mutex protecting all monitor state.
    pthread_t owner;            ///< Current owner thread.
    int owner_valid;            ///< Non-zero if monitor is currently owned.
    size_t recursion;           ///< Re-entry count for owner.
    RtMonitorWaiter *acq_head;  ///< Head of acquisition queue.
    RtMonitorWaiter *acq_tail;  ///< Tail of acquisition queue.
    RtMonitorWaiter *wait_head; ///< Head of wait queue.
    RtMonitorWaiter *wait_tail; ///< Tail of wait queue.
} RtMonitor;

/// @brief Hash table entry mapping object address to monitor.
///
/// Monitors are looked up by object address. The hash table uses separate
/// chaining for collision resolution.
typedef struct RtMonitorEntry
{
    void *key;                   ///< Object address (hash key).
    struct RtMonitorEntry *next; ///< Next entry in hash chain.
    RtMonitor monitor;           ///< The monitor state.
} RtMonitorEntry;

#define RT_MONITOR_BUCKETS 4096u

static pthread_mutex_t g_monitor_table_mu = PTHREAD_MUTEX_INITIALIZER;
static RtMonitorEntry *g_monitor_table[RT_MONITOR_BUCKETS];

static size_t hash_ptr(void *p)
{
    uintptr_t x = (uintptr_t)p;
    x >>= 4;
    x ^= x >> 16;
    x *= 0x9E3779B97F4A7C15ull;
    return (size_t)(x & (RT_MONITOR_BUCKETS - 1u));
}

static RtMonitor *get_monitor_for(void *obj)
{
    size_t idx = hash_ptr(obj);

    pthread_mutex_lock(&g_monitor_table_mu);
    RtMonitorEntry *it = g_monitor_table[idx];
    while (it)
    {
        if (it->key == obj)
        {
            pthread_mutex_unlock(&g_monitor_table_mu);
            return &it->monitor;
        }
        it = it->next;
    }

    RtMonitorEntry *node = (RtMonitorEntry *)calloc(1, sizeof(*node));
    if (!node)
    {
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

static int monitor_is_owner(const RtMonitor *m, pthread_t self)
{
    return m->owner_valid && pthread_equal(m->owner, self);
}

static void monitor_grant_next_waiter(RtMonitor *m)
{
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

static void monitor_enqueue_acq(RtMonitor *m, RtMonitorWaiter *w)
{
    w->next = NULL;
    if (m->acq_tail)
    {
        m->acq_tail->next = w;
        m->acq_tail = w;
    }
    else
    {
        m->acq_head = w;
        m->acq_tail = w;
    }
}

static void monitor_remove_acq(RtMonitor *m, RtMonitorWaiter *w)
{
    RtMonitorWaiter *prev = NULL;
    RtMonitorWaiter *cur = m->acq_head;
    while (cur)
    {
        if (cur == w)
        {
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

static void monitor_enqueue_wait(RtMonitor *m, RtMonitorWaiter *w)
{
    w->next = NULL;
    if (m->wait_tail)
    {
        m->wait_tail->next = w;
        m->wait_tail = w;
    }
    else
    {
        m->wait_head = w;
        m->wait_tail = w;
    }
}

static void monitor_remove_wait(RtMonitor *m, RtMonitorWaiter *w)
{
    RtMonitorWaiter *prev = NULL;
    RtMonitorWaiter *cur = m->wait_head;
    while (cur)
    {
        if (cur == w)
        {
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

static struct timespec abs_time_ms_from_now(int64_t ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (ms <= 0)
        return ts;

    const int64_t kNsPerMs = 1000000;
    int64_t add_ns = ms * kNsPerMs;
    int64_t sec = (int64_t)ts.tv_sec + add_ns / 1000000000;
    int64_t ns = (int64_t)ts.tv_nsec + add_ns % 1000000000;
    if (ns >= 1000000000)
    {
        sec += 1;
        ns -= 1000000000;
    }
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)ns;
    return ts;
}

static void monitor_enter_blocking(RtMonitor *m, pthread_t self, int64_t timeout_ms, int timed)
{
    if (!m)
    {
        rt_trap("rt_monitor: null monitor");
        return;
    }

    if (monitor_is_owner(m, self))
    {
        m->recursion += 1;
        return;
    }

    if (!m->owner_valid && m->acq_head == NULL)
    {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        return;
    }

    RtMonitorWaiter w = {0};
    (void)pthread_cond_init(&w.cv, NULL);
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    int rc = 0;
    const struct timespec deadline = abs_time_ms_from_now(timeout_ms);
    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        if (!timed)
        {
            rc = pthread_cond_wait(&w.cv, &m->mu);
        }
        else
        {
            rc = pthread_cond_timedwait(&w.cv, &m->mu, &deadline);
        }

        if (timed && rc == ETIMEDOUT && w.state != RT_MON_WAITER_ACQUIRED)
        {
            monitor_remove_acq(m, &w);
            pthread_cond_destroy(&w.cv);
            return;
        }
    }

    pthread_cond_destroy(&w.cv);
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
void rt_monitor_enter(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_mutex_lock(&m->mu);
    monitor_enter_blocking(m, pthread_self(), /*timeout_ms=*/0, /*timed=*/0);
    pthread_mutex_unlock(&m->mu);
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
int8_t rt_monitor_try_enter(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    pthread_t self = pthread_self();

    pthread_mutex_lock(&m->mu);
    if (monitor_is_owner(m, self))
    {
        m->recursion += 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL)
    {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }
    pthread_mutex_unlock(&m->mu);
    return 0;
}

int8_t rt_monitor_try_enter_for(void *obj, int64_t ms)
{
    if (!obj)
        rt_trap("Monitor.Enter: null object");
    if (!obj)
        return 0;
    if (ms < 0)
        ms = 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    pthread_t self = pthread_self();

    pthread_mutex_lock(&m->mu);
    if (monitor_is_owner(m, self))
    {
        m->recursion += 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }
    if (!m->owner_valid && m->acq_head == NULL)
    {
        m->owner = self;
        m->owner_valid = 1;
        m->recursion = 1;
        pthread_mutex_unlock(&m->mu);
        return 1;
    }

    RtMonitorWaiter w = {0};
    (void)pthread_cond_init(&w.cv, NULL);
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_LOCK;
    w.desired_recursion = 1;
    monitor_enqueue_acq(m, &w);

    const struct timespec deadline = abs_time_ms_from_now(ms);
    int rc = 0;
    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        rc = pthread_cond_timedwait(&w.cv, &m->mu, &deadline);
        if (rc == ETIMEDOUT && w.state != RT_MON_WAITER_ACQUIRED)
        {
            monitor_remove_acq(m, &w);
            pthread_cond_destroy(&w.cv);
            pthread_mutex_unlock(&m->mu);
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
void rt_monitor_exit(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Exit: null object");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();

    pthread_mutex_lock(&m->mu);
    if (!monitor_is_owner(m, self))
    {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Exit: not owner");
        return;
    }
    if (m->recursion > 1)
    {
        m->recursion -= 1;
        pthread_mutex_unlock(&m->mu);
        return;
    }

    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);
    pthread_mutex_unlock(&m->mu);
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
void rt_monitor_wait(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self))
    {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Wait: not owner");
        return;
    }

    const size_t saved_recursion = m->recursion;

    // Release the monitor fully and hand off to the next waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    RtMonitorWaiter w = {0};
    (void)pthread_cond_init(&w.cv, NULL);
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    while (w.state == RT_MON_WAITER_WAITING_PAUSE)
    {
        (void)pthread_cond_wait(&w.cv, &m->mu);
    }

    // Wait for re-acquisition.
    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        (void)pthread_cond_wait(&w.cv, &m->mu);
    }

    pthread_cond_destroy(&w.cv);
    pthread_mutex_unlock(&m->mu);
}

int8_t rt_monitor_wait_for(void *obj, int64_t ms)
{
    if (!obj)
        rt_trap("Monitor.Wait: not owner");
    if (!obj)
        return 0;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return 0;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self))
    {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Wait: not owner");
        return 0;
    }

    if (ms < 0)
        ms = 0;

    const size_t saved_recursion = m->recursion;

    // Release the monitor fully and hand off to the next waiter.
    m->owner_valid = 0;
    m->recursion = 0;
    monitor_grant_next_waiter(m);

    RtMonitorWaiter w = {0};
    (void)pthread_cond_init(&w.cv, NULL);
    w.thread = self;
    w.state = RT_MON_WAITER_WAITING_PAUSE;
    w.desired_recursion = saved_recursion;
    monitor_enqueue_wait(m, &w);

    int timed_out = 0;
    const struct timespec deadline = abs_time_ms_from_now(ms);
    while (w.state == RT_MON_WAITER_WAITING_PAUSE)
    {
        const int rc = pthread_cond_timedwait(&w.cv, &m->mu, &deadline);
        if (rc == ETIMEDOUT && w.state == RT_MON_WAITER_WAITING_PAUSE)
        {
            timed_out = 1;
            break;
        }
    }

    if (timed_out)
    {
        // Timeout while still in the wait queue: remove and begin fair re-acquire.
        monitor_remove_wait(m, &w);
        w.state = RT_MON_WAITER_WAITING_LOCK;
        monitor_enqueue_acq(m, &w);
        if (!m->owner_valid && m->acq_head)
            monitor_grant_next_waiter(m);
    }

    while (w.state != RT_MON_WAITER_ACQUIRED)
    {
        (void)pthread_cond_wait(&w.cv, &m->mu);
    }

    pthread_cond_destroy(&w.cv);
    pthread_mutex_unlock(&m->mu);
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
void rt_monitor_pause(void *obj)
{
    if (!obj)
        rt_trap("Monitor.Pause: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self))
    {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.Pause: not owner");
        return;
    }

    RtMonitorWaiter *w = m->wait_head;
    if (w)
    {
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
void rt_monitor_pause_all(void *obj)
{
    if (!obj)
        rt_trap("Monitor.PauseAll: not owner");
    if (!obj)
        return;
    RtMonitor *m = get_monitor_for(obj);
    if (!m)
        return;
    pthread_t self = pthread_self();
    pthread_mutex_lock(&m->mu);

    if (!monitor_is_owner(m, self))
    {
        pthread_mutex_unlock(&m->mu);
        rt_trap("Monitor.PauseAll: not owner");
        return;
    }

    while (m->wait_head)
    {
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
