//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/pthread.c
// Purpose: POSIX threads implementation for ViperDOS libc.
// Key invariants: Threads share process address space via kernel support.
// Ownership/Lifetime: Library; per-thread TLS via TPIDR_EL0.
// Links: user/libc/include/pthread.h
//
//===----------------------------------------------------------------------===//

/**
 * @file pthread.c
 * @brief POSIX threads implementation for ViperDOS libc.
 *
 * @details
 * This file provides real pthreads support using ViperDOS kernel thread
 * syscalls (SYS_THREAD_CREATE, SYS_THREAD_EXIT, SYS_THREAD_JOIN,
 * SYS_THREAD_DETACH, SYS_THREAD_SELF).
 *
 * Each thread gets:
 * - Its own mmap'd stack
 * - A Thread Control Block (TCB) at the base of the stack region
 * - Per-thread TLS via TPIDR_EL0 pointing to the TCB
 *
 * Mutexes and condition variables remain single-threaded stubs for now
 * (they work as simple flags/no-ops since ViperDOS is single-core).
 */

#include "../include/pthread.h"
#include "../include/string.h"
#include "syscall_internal.h"

/* Syscall numbers */
#define SYS_THREAD_CREATE 0xB0
#define SYS_THREAD_EXIT 0xB1
#define SYS_THREAD_JOIN 0xB2
#define SYS_THREAD_DETACH 0xB3
#define SYS_THREAD_SELF 0xB4
#define SYS_MMAP 0x150
#define SYS_MUNMAP 0x151
#define SYS_TASK_EXIT 0x01

/* mmap constants */
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

/* Default thread stack size (64KB) */
#define DEFAULT_STACK_SIZE (64 * 1024)

/* Maximum TLS keys per thread */
#define TLS_KEYS_MAX 64

/**
 * @brief Thread Control Block - placed at the base of each thread's stack.
 *
 * @details
 * TPIDR_EL0 points to this structure, allowing each thread to find its
 * own TLS data, stack info, and startup parameters.
 */
typedef struct {
    void *(*start_routine)(void *); /* Thread entry function */
    void *arg;                      /* Argument to start_routine */
    void *stack_base;               /* Base of mmap'd stack region */
    size_t stack_size;              /* Size of mmap'd stack region */
    pthread_t thread_id;            /* Kernel task ID */
    int detached;                   /* Detached state */
    int errno_value;                /* Per-thread errno storage */
    void *tls_values[TLS_KEYS_MAX]; /* Per-thread TLS storage */
} tcb_t;

/* Global TLS key management (shared across threads) */
static void (*tls_destructors[TLS_KEYS_MAX])(void *);
static int tls_key_used[TLS_KEYS_MAX];
static int tls_next_key = 0;

/* Main thread fallback TLS (for when TPIDR_EL0 == 0) */
static void *main_tls_values[TLS_KEYS_MAX];

/**
 * @brief Read TPIDR_EL0 to get current thread's TCB.
 */
static inline tcb_t *get_tcb(void) {
    unsigned long tpidr;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tpidr));
    return (tcb_t *)tpidr;
}

/**
 * @brief Thread wrapper function - the kernel jumps here for new threads.
 *
 * @details
 * This function reads the TCB (via TPIDR_EL0) to find the actual
 * start_routine and arg, calls it, then calls pthread_exit with the
 * return value.
 */
static void thread_wrapper(void) {
    tcb_t *tcb = get_tcb();
    if (!tcb) {
        __syscall1(SYS_THREAD_EXIT, 0);
        __builtin_unreachable();
    }

    void *retval = tcb->start_routine(tcb->arg);
    pthread_exit(retval);
    __builtin_unreachable();
}

/*
 * Thread functions
 */

/**
 * @brief Create a new thread with its own stack and TCB.
 * @param thread Receives the new thread's ID on success.
 * @param attr Thread attributes (stack size, detach state), or NULL for defaults.
 * @param start_routine Function the new thread will execute.
 * @param arg Argument passed to start_routine.
 * @return 0 on success, or an error code (EINVAL, ENOMEM, EAGAIN).
 */
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
    if (!thread || !start_routine)
        return EINVAL;

    /* Determine stack size */
    size_t stacksize = DEFAULT_STACK_SIZE;
    int detachstate = PTHREAD_CREATE_JOINABLE;
    if (attr) {
        if (attr->stacksize > 0)
            stacksize = attr->stacksize;
        detachstate = attr->detachstate;
    }

    /* Ensure stack size is page-aligned (4KB) */
    stacksize = (stacksize + 0xFFF) & ~0xFFFUL;

    /* Allocate stack via mmap */
    long stack_base = __syscall6(
        SYS_MMAP, 0, (long)stacksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (stack_base <= 0) {
        return ENOMEM;
    }

    /* Place TCB at the base of the stack region */
    tcb_t *tcb = (tcb_t *)stack_base;
    memset(tcb, 0, sizeof(tcb_t));
    tcb->start_routine = start_routine;
    tcb->arg = arg;
    tcb->stack_base = (void *)stack_base;
    tcb->stack_size = stacksize;
    tcb->detached = detachstate;

    /* Stack grows downward; top is at base + size, aligned to 16 bytes */
    unsigned long stack_top = (stack_base + (long)stacksize) & ~0xFUL;

    /* Create the kernel thread */
    long result = __syscall3(SYS_THREAD_CREATE,
                             (long)thread_wrapper,
                             (long)stack_top,
                             (long)tcb); /* tls_base = TCB address */

    if (result <= 0) {
        /* Failed - free the stack */
        __syscall3(SYS_MUNMAP, stack_base, (long)stacksize, 0);
        return EAGAIN;
    }

    tcb->thread_id = (pthread_t)result;
    *thread = (pthread_t)result;

    /* If detached, tell the kernel */
    if (detachstate == PTHREAD_CREATE_DETACHED) {
        __syscall1(SYS_THREAD_DETACH, result);
    }

    return 0;
}

/**
 * @brief Wait for a thread to terminate and retrieve its return value.
 * @param thread Thread ID to wait for.
 * @param retval If non-NULL, receives the thread's exit value.
 * @return 0 on success, EINVAL on error.
 */
int pthread_join(pthread_t thread, void **retval) {
    long result = __syscall1(SYS_THREAD_JOIN, (long)thread);
    if (result < 0) {
        return EINVAL;
    }
    if (retval) {
        *retval = (void *)result;
    }
    return 0;
}

/**
 * @brief Terminate the calling thread, invoking TLS destructors first.
 * @param retval Value returned to any thread calling pthread_join.
 */
void pthread_exit(void *retval) {
    /* Invoke TLS key destructors (Issue #79 fix)
     * POSIX requires destructors to be called up to PTHREAD_DESTRUCTOR_ITERATIONS times
     * until all non-NULL TLS values are cleared or the limit is reached.
     */
    tcb_t *tcb = get_tcb();
    void **tls = tcb ? tcb->tls_values : main_tls_values;
    for (int iter = 0; iter < 4; iter++) { /* PTHREAD_DESTRUCTOR_ITERATIONS = 4 */
        int any_called = 0;
        for (int i = 0; i < TLS_KEYS_MAX; i++) {
            if (tls_key_used[i] && tls_destructors[i] && tls[i]) {
                void *val = tls[i];
                tls[i] = NULL; /* Clear before calling destructor */
                tls_destructors[i](val);
                any_called = 1;
            }
        }
        if (!any_called)
            break;
    }

    __syscall1(SYS_THREAD_EXIT, (long)retval);
    /* If this is the main thread and SYS_THREAD_EXIT returns (shouldn't),
     * fall back to process exit */
    extern void exit(int);
    exit(0);
}

/**
 * @brief Mark a thread as detached so its resources are freed on exit.
 * @param thread Thread ID to detach.
 * @return 0 on success, EINVAL on error.
 */
int pthread_detach(pthread_t thread) {
    long result = __syscall1(SYS_THREAD_DETACH, (long)thread);
    if (result < 0) {
        return EINVAL;
    }
    return 0;
}

/** @brief Return the thread ID of the calling thread. */
pthread_t pthread_self(void) {
    long result = __syscall0(SYS_THREAD_SELF);
    return (pthread_t)result;
}

/** @brief Test whether two thread IDs refer to the same thread. */
int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

/*
 * Thread attributes
 */

/** @brief Initialize thread attributes with default values. */
int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr)
        return EINVAL;
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    attr->stacksize = DEFAULT_STACK_SIZE;
    return 0;
}

/** @brief Destroy thread attributes (no-op in this implementation). */
int pthread_attr_destroy(pthread_attr_t *attr) {
    (void)attr;
    return 0;
}

/** @brief Set the detach state in thread attributes (joinable or detached). */
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
    if (!attr)
        return EINVAL;
    if (detachstate != PTHREAD_CREATE_JOINABLE && detachstate != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    attr->detachstate = detachstate;
    return 0;
}

/** @brief Get the detach state from thread attributes. */
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
    if (!attr || !detachstate)
        return EINVAL;
    *detachstate = attr->detachstate;
    return 0;
}

/** @brief Set the stack size in thread attributes (minimum 4096 bytes). */
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    if (!attr || stacksize < 4096)
        return EINVAL;
    attr->stacksize = stacksize;
    return 0;
}

/** @brief Get the stack size from thread attributes. */
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
    if (!attr || !stacksize)
        return EINVAL;
    *stacksize = attr->stacksize;
    return 0;
}

/*
 * Mutex functions (single-core, so these work as simple flags)
 */

/** @brief Initialize a mutex with the given attributes (or defaults). */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    if (!mutex)
        return EINVAL;
    mutex->locked = 0;
    mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
    return 0;
}

/** @brief Destroy a mutex. Returns EBUSY if the mutex is still locked. */
int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex)
        return EINVAL;
    if (mutex->locked)
        return EBUSY;
    return 0;
}

/** @brief Lock a mutex. Supports normal, recursive, and error-checking types. */
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex)
        return EINVAL;

    /* Check for deadlock in error-checking mode */
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutex->locked) {
        return EDEADLK;
    }

    /* Recursive mutex: allow multiple locks */
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        mutex->locked++;
        return 0;
    }

    /* Normal mutex: just set flag (single-core, no contention) */
    mutex->locked = 1;
    return 0;
}

/** @brief Try to lock a mutex without blocking. Returns EBUSY if already held. */
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!mutex)
        return EINVAL;

    if (mutex->locked && mutex->type != PTHREAD_MUTEX_RECURSIVE) {
        return EBUSY;
    }

    mutex->locked++;
    return 0;
}

/** @brief Unlock a mutex. For recursive mutexes, decrements the lock count. */
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex)
        return EINVAL;

    if (!mutex->locked) {
        if (mutex->type == PTHREAD_MUTEX_ERRORCHECK)
            return EPERM;
        return 0;
    }

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        mutex->locked--;
    } else {
        mutex->locked = 0;
    }
    return 0;
}

/*
 * Mutex attributes
 */

/** @brief Initialize mutex attributes with default type (PTHREAD_MUTEX_NORMAL). */
int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (!attr)
        return EINVAL;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

/** @brief Destroy mutex attributes (no-op in this implementation). */
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    (void)attr;
    return 0;
}

/** @brief Set the mutex type (normal, recursive, or error-checking). */
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (!attr)
        return EINVAL;
    if (type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK)
        return EINVAL;
    attr->type = type;
    return 0;
}

/** @brief Get the mutex type from mutex attributes. */
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type) {
    if (!attr || !type)
        return EINVAL;
    *type = attr->type;
    return 0;
}

/*
 * Condition variable functions
 */

/** @brief Initialize a condition variable (stub for single-core system). */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    if (!cond)
        return EINVAL;
    memset(cond, 0, sizeof(*cond));
    return 0;
}

/** @brief Destroy a condition variable (no-op). */
int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

/** @brief Wait on a condition variable (returns immediately on single-core). */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    (void)cond;
    (void)mutex;
    /* Single-core: return immediately to avoid deadlock */
    return 0;
}

/** @brief Timed wait on a condition variable (returns immediately on single-core). */
int pthread_cond_timedwait(pthread_cond_t *cond,
                           pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    (void)cond;
    (void)mutex;
    (void)abstime;
    return 0;
}

/** @brief Signal one thread waiting on a condition variable (no-op). */
int pthread_cond_signal(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

/** @brief Wake all threads waiting on a condition variable (no-op). */
int pthread_cond_broadcast(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

/*
 * Condition variable attributes
 */

/** @brief Initialize condition variable attributes with defaults. */
int pthread_condattr_init(pthread_condattr_t *attr) {
    if (!attr)
        return EINVAL;
    memset(attr, 0, sizeof(*attr));
    return 0;
}

/** @brief Destroy condition variable attributes (no-op). */
int pthread_condattr_destroy(pthread_condattr_t *attr) {
    (void)attr;
    return 0;
}

/*
 * Read-write lock functions
 */

/** @brief Initialize a read-write lock with reader/writer counters at zero. */
int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
    (void)attr;
    if (!rwlock)
        return EINVAL;
    rwlock->readers = 0;
    rwlock->writer = 0;
    return 0;
}

/** @brief Destroy a read-write lock. Returns EBUSY if still held. */
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    if (rwlock->readers || rwlock->writer)
        return EBUSY;
    return 0;
}

/** @brief Acquire a read lock (increments reader count). */
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    rwlock->readers++;
    return 0;
}

/** @brief Try to acquire a read lock without blocking. Returns EBUSY if a writer holds it. */
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    if (rwlock->writer)
        return EBUSY;
    rwlock->readers++;
    return 0;
}

/** @brief Acquire a write lock (sets writer flag). */
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    rwlock->writer = 1;
    return 0;
}

/** @brief Try to acquire a write lock without blocking. Returns EBUSY if held by any thread. */
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    if (rwlock->readers || rwlock->writer)
        return EBUSY;
    rwlock->writer = 1;
    return 0;
}

/** @brief Release a read or write lock. */
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
    if (!rwlock)
        return EINVAL;
    if (rwlock->writer) {
        rwlock->writer = 0;
    } else if (rwlock->readers) {
        rwlock->readers--;
    }
    return 0;
}

/*
 * Once control
 */

/** @brief Execute init_routine exactly once, regardless of how many threads call this. */
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine)
        return EINVAL;

    if (*once_control == 0) {
        *once_control = 1;
        init_routine();
    }
    return 0;
}

/*
 * Thread-local storage (per-thread via TPIDR_EL0 / TCB)
 */

/**
 * @brief Create a thread-local storage key with an optional destructor.
 * @param key Receives the new TLS key on success.
 * @param destructor Called with the key's value when a thread exits, or NULL.
 * @return 0 on success, EAGAIN if all TLS slots are in use.
 */
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key)
        return EINVAL;

    for (int i = 0; i < TLS_KEYS_MAX; i++) {
        int k = (tls_next_key + i) % TLS_KEYS_MAX;
        if (!tls_key_used[k]) {
            tls_key_used[k] = 1;
            tls_destructors[k] = destructor;
            *key = k;
            tls_next_key = (k + 1) % TLS_KEYS_MAX;
            return 0;
        }
    }
    return EAGAIN;
}

/** @brief Delete a thread-local storage key, freeing the slot for reuse. */
int pthread_key_delete(pthread_key_t key) {
    if (key >= TLS_KEYS_MAX || !tls_key_used[key])
        return EINVAL;

    tls_key_used[key] = 0;
    tls_destructors[key] = 0;
    return 0;
}

/** @brief Get the calling thread's value for a TLS key. */
void *pthread_getspecific(pthread_key_t key) {
    if (key >= TLS_KEYS_MAX || !tls_key_used[key])
        return 0;

    tcb_t *tcb = get_tcb();
    if (tcb) {
        return tcb->tls_values[key];
    }
    /* Main thread fallback */
    return main_tls_values[key];
}

/** @brief Set the calling thread's value for a TLS key. */
int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= TLS_KEYS_MAX || !tls_key_used[key])
        return EINVAL;

    tcb_t *tcb = get_tcb();
    if (tcb) {
        tcb->tls_values[key] = (void *)value;
    } else {
        /* Main thread fallback */
        main_tls_values[key] = (void *)value;
    }
    return 0;
}

/*
 * Cancellation (not supported)
 */

/** @brief Request cancellation of a thread (not supported; returns ENOSYS). */
int pthread_cancel(pthread_t thread) {
    (void)thread;
    return ENOSYS;
}

/** @brief Set the cancellation state (stub; cancellation is always disabled). */
int pthread_setcancelstate(int state, int *oldstate) {
    (void)state;
    if (oldstate)
        *oldstate = PTHREAD_CANCEL_DISABLE;
    return 0;
}

/** @brief Set the cancellation type (stub; cancellation not supported). */
int pthread_setcanceltype(int type, int *oldtype) {
    (void)type;
    if (oldtype)
        *oldtype = PTHREAD_CANCEL_DEFERRED;
    return 0;
}

/** @brief Test for pending cancellation (no-op; cancellation not supported). */
void pthread_testcancel(void) {
    /* No cancellation support */
}
