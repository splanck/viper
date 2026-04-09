//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/threads/rt_concqueue.c
// Purpose: Implements a thread-safe concurrent FIFO queue for the
//          Viper.Threads.ConcQueue class. Uses a linked-list of nodes protected
//          by a mutex and condition variable. Supports Enqueue, Dequeue
//          (blocking), TryDequeue (non-blocking), Count, and Clear.
//
// Key invariants:
//   - Enqueue always succeeds; the queue is unbounded.
//   - Dequeue blocks until an item is available or a timeout expires.
//   - TryDequeue returns false immediately if the queue is empty.
//   - Count reflects the number of items currently in the queue.
//   - All operations acquire the mutex before touching the head/tail/count.
//   - Win32 uses CRITICAL_SECTION + CONDITION_VARIABLE; POSIX uses pthreads.
//
// Ownership/Lifetime:
//   - Enqueued void* values are retained on enqueue and released on
//     dequeue, clear, or finalize.
//   - Node allocations are freed on Dequeue or during Clear/finalize.
//   - The queue object is heap-allocated and managed by the runtime GC.
//
// Links: src/runtime/threads/rt_concqueue.h (public API),
//        src/runtime/threads/rt_channel.h (bounded channel, related concept)
//
//===----------------------------------------------------------------------===//

#include "rt_concqueue.h"

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#if defined(__APPLE__)
extern int pthread_cond_timedwait_relative_np(pthread_cond_t *cond,
                                              pthread_mutex_t *mutex,
                                              const struct timespec *rel_time);
#endif
#endif

// --- Node for linked list queue ---

typedef struct cq_node {
    void *value;
    struct cq_node *next;
} cq_node;

typedef struct {
    void *vptr;
    cq_node *head;
    cq_node *tail;
    int64_t count;
    int8_t closed;
#if defined(_WIN32)
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond;
#else
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int8_t cond_uses_monotonic;
#endif
} rt_concqueue_impl;

// --- Platform abstraction macros ---

#if defined(_WIN32)
#define CQ_LOCK(cq) EnterCriticalSection(&(cq)->mutex)
#define CQ_UNLOCK(cq) LeaveCriticalSection(&(cq)->mutex)
#define CQ_SIGNAL(cq) WakeConditionVariable(&(cq)->cond)
#define CQ_WAIT(cq) SleepConditionVariableCS(&(cq)->cond, &(cq)->mutex, INFINITE)
#else
#define CQ_LOCK(cq) pthread_mutex_lock(&(cq)->mutex)
#define CQ_UNLOCK(cq) pthread_mutex_unlock(&(cq)->mutex)
#define CQ_SIGNAL(cq) pthread_cond_signal(&(cq)->cond)
#define CQ_WAIT(cq) pthread_cond_wait(&(cq)->cond, &(cq)->mutex)
#endif

#if !defined(_WIN32)
typedef struct {
    struct timespec deadline;
} cq_deadline_t;

static int cq_cond_init(pthread_cond_t *cond, int8_t *uses_monotonic) {
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
    {
        if (uses_monotonic)
            *uses_monotonic = 1;
        const int rc = pthread_cond_init(cond, &attr);
        if (rc != 0 && uses_monotonic)
            *uses_monotonic = 0;
        pthread_condattr_destroy(&attr);
        return rc;
    }
#else
    return pthread_cond_init(cond, NULL);
#endif
}

static struct timespec cq_now_clock(int8_t use_monotonic) {
    struct timespec ts;
    memset(&ts, 0, sizeof(ts));
#ifdef CLOCK_MONOTONIC
    if (use_monotonic && clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return ts;
#endif
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

static cq_deadline_t cq_deadline_ms_from_now(int64_t timeout_ms, int8_t use_monotonic) {
    cq_deadline_t d;
    d.deadline = cq_now_clock(use_monotonic);
    if (timeout_ms <= 0)
        return d;
    d.deadline.tv_sec += timeout_ms / 1000;
    d.deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (d.deadline.tv_nsec >= 1000000000L) {
        d.deadline.tv_sec++;
        d.deadline.tv_nsec -= 1000000000L;
    }
    return d;
}

#if defined(__APPLE__)
static int64_t cq_remaining_ms(cq_deadline_t deadline, int8_t use_monotonic) {
    struct timespec now = cq_now_clock(use_monotonic);
    int64_t sec = (int64_t)deadline.deadline.tv_sec - (int64_t)now.tv_sec;
    int64_t ns = (int64_t)deadline.deadline.tv_nsec - (int64_t)now.tv_nsec;
    if (ns < 0) {
        sec--;
        ns += 1000000000L;
    }
    if (sec < 0)
        return 0;
    return sec * 1000 + ns / 1000000L;
}
#endif

static int cq_cond_timedwait_deadline(pthread_cond_t *cond,
                                      pthread_mutex_t *mutex,
                                      cq_deadline_t deadline,
                                      int8_t use_monotonic) {
#if defined(__APPLE__)
    int64_t remaining = cq_remaining_ms(deadline, use_monotonic);
    if (remaining <= 0)
        return ETIMEDOUT;
    struct timespec rel;
    rel.tv_sec = (time_t)(remaining / 1000);
    rel.tv_nsec = (long)((remaining % 1000) * 1000000L);
    return pthread_cond_timedwait_relative_np(cond, mutex, &rel);
#else
    return pthread_cond_timedwait(cond, mutex, &deadline.deadline);
#endif
}
#endif

// --- Internal helpers ---

static void cq_finalizer(void *obj) {
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    CQ_LOCK(cq);
    cq_node *n = cq->head;
    while (n) {
        cq_node *next = n->next;
        if (rt_obj_release_check0(n->value))
            rt_obj_free(n->value);
        free(n);
        n = next;
    }
    cq->head = NULL;
    cq->tail = NULL;
    cq->count = 0;
    CQ_UNLOCK(cq);

#if defined(_WIN32)
    DeleteCriticalSection(&cq->mutex);
    // CONDITION_VARIABLE does not need explicit destruction on Windows
#else
    pthread_mutex_destroy(&cq->mutex);
    pthread_cond_destroy(&cq->cond);
#endif
}

// --- Public API ---

/// @brief Create a new thread-safe concurrent queue (unbounded, mutex + condvar protected).
void *rt_concqueue_new(void) {
    rt_concqueue_impl *cq = (rt_concqueue_impl *)rt_obj_new_i64(0, sizeof(rt_concqueue_impl));
    if (!cq) {
        rt_trap("ConcurrentQueue: memory allocation failed");
        return NULL;
    }
    cq->head = NULL;
    cq->tail = NULL;
    cq->count = 0;
    cq->closed = 0;
#if defined(_WIN32)
    InitializeCriticalSection(&cq->mutex);
    InitializeConditionVariable(&cq->cond);
#else
    pthread_mutex_init(&cq->mutex, NULL);
    cq_cond_init(&cq->cond, &cq->cond_uses_monotonic);
#endif
    rt_obj_set_finalizer(cq, cq_finalizer);
    return (void *)cq;
}

/// @brief Return the number of elements in the concqueue.
int64_t rt_concqueue_len(void *obj) {
    if (!obj)
        return 0;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;
    CQ_LOCK(cq);
    int64_t len = cq->count;
    CQ_UNLOCK(cq);
    return len;
}

/// @brief Check whether the concqueue has no entries.
int8_t rt_concqueue_is_empty(void *obj) {
    return rt_concqueue_len(obj) == 0 ? 1 : 0;
}

int8_t rt_concqueue_get_is_closed(void *obj) {
    if (!obj)
        return 1;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;
    CQ_LOCK(cq);
    int8_t closed = cq->closed;
    CQ_UNLOCK(cq);
    return closed;
}

/// @brief Enqueue the concqueue.
void rt_concqueue_enqueue(void *obj, void *item) {
    if (!obj)
        return;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    cq_node *node = (cq_node *)malloc(sizeof(cq_node));
    if (!node) {
        rt_trap("ConcurrentQueue: memory allocation failed");
        return;
    }
    node->value = item;
    rt_obj_retain_maybe(item);
    node->next = NULL;

    CQ_LOCK(cq);
    if (cq->closed) {
        CQ_UNLOCK(cq);
        if (item && rt_obj_release_check0(item))
            rt_obj_free(item);
        free(node);
        rt_trap("ConcurrentQueue.Enqueue: queue is closed");
        return;
    }
    if (cq->tail)
        cq->tail->next = node;
    else
        cq->head = node;
    cq->tail = node;
    cq->count++;
    CQ_SIGNAL(cq);
    CQ_UNLOCK(cq);
}

void *rt_concqueue_try_dequeue(void *obj) {
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    CQ_LOCK(cq);
    if (!cq->head) {
        CQ_UNLOCK(cq);
        return NULL;
    }

    cq_node *node = cq->head;
    cq->head = node->next;
    if (!cq->head)
        cq->tail = NULL;
    cq->count--;
    CQ_UNLOCK(cq);

    void *value = node->value;
    free(node);
    return value;
}

void *rt_concqueue_dequeue(void *obj) {
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    CQ_LOCK(cq);
    while (!cq->head && !cq->closed)
        CQ_WAIT(cq);
    if (!cq->head) {
        CQ_UNLOCK(cq);
        return NULL;
    }

    cq_node *node = cq->head;
    cq->head = node->next;
    if (!cq->head)
        cq->tail = NULL;
    cq->count--;
    CQ_UNLOCK(cq);

    void *value = node->value;
    free(node);
    return value;
}

void *rt_concqueue_dequeue_timeout(void *obj, int64_t timeout_ms) {
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;
    if (timeout_ms <= 0)
        return rt_concqueue_try_dequeue(obj);

#if defined(_WIN32)
    CQ_LOCK(cq);
    DWORD start = GetTickCount();
    while (!cq->head && !cq->closed) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - start;
        DWORD remaining = (elapsed >= (DWORD)timeout_ms) ? 0 : (DWORD)timeout_ms - elapsed;
        if (remaining == 0) {
            CQ_UNLOCK(cq);
            return NULL;
        }
        if (!SleepConditionVariableCS(&cq->cond, &cq->mutex, remaining)) {
            // Timeout or error
            CQ_UNLOCK(cq);
            return NULL;
        }
    }
#else
    cq_deadline_t deadline = cq_deadline_ms_from_now(timeout_ms, cq->cond_uses_monotonic);

    CQ_LOCK(cq);
    while (!cq->head && !cq->closed) {
        int rc =
            cq_cond_timedwait_deadline(&cq->cond, &cq->mutex, deadline, cq->cond_uses_monotonic);
        if (rc == ETIMEDOUT) {
            // Timeout or error
            CQ_UNLOCK(cq);
            return NULL;
        }
    }
#endif

    if (!cq->head) {
        CQ_UNLOCK(cq);
        return NULL;
    }

    cq_node *node = cq->head;
    cq->head = node->next;
    if (!cq->head)
        cq->tail = NULL;
    cq->count--;
    CQ_UNLOCK(cq);

    void *value = node->value;
    free(node);
    return value;
}

void *rt_concqueue_peek(void *obj) {
    if (!obj)
        return NULL;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    CQ_LOCK(cq);
    void *value = cq->head ? cq->head->value : NULL;
    if (value)
        rt_obj_retain_maybe(value);
    CQ_UNLOCK(cq);
    return value;
}

/// @brief Remove all entries from the concqueue.
void rt_concqueue_clear(void *obj) {
    if (!obj)
        return;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;

    CQ_LOCK(cq);
    cq_node *n = cq->head;
    while (n) {
        cq_node *next = n->next;
        if (rt_obj_release_check0(n->value))
            rt_obj_free(n->value);
        free(n);
        n = next;
    }
    cq->head = NULL;
    cq->tail = NULL;
    cq->count = 0;
    CQ_UNLOCK(cq);
}

void rt_concqueue_close(void *obj) {
    if (!obj)
        return;
    rt_concqueue_impl *cq = (rt_concqueue_impl *)obj;
    CQ_LOCK(cq);
    if (cq->closed) {
        CQ_UNLOCK(cq);
        return;
    }
    cq->closed = 1;
#if defined(_WIN32)
    WakeAllConditionVariable(&cq->cond);
#else
    pthread_cond_broadcast(&cq->cond);
#endif
    CQ_UNLOCK(cq);
}
