/*
 * Minimal pthreads implementation for ViperOS
 *
 * This provides stub implementations for single-threaded programs.
 * Mutexes work as simple flags, condition variables are no-ops,
 * and thread creation returns ENOSYS.
 */

#include "../include/pthread.h"
#include "../include/string.h"

/* Current "thread" ID (always 1 for the main thread) */
static pthread_t main_thread_id = 1;

/*
 * Thread functions
 */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    (void)thread;
    (void)attr;
    (void)start_routine;
    (void)arg;
    /* No threading support yet */
    return ENOSYS;
}

int pthread_join(pthread_t thread, void **retval)
{
    (void)thread;
    (void)retval;
    /* No other threads to join */
    return EINVAL;
}

void pthread_exit(void *retval)
{
    (void)retval;
    /* In single-threaded mode, this is equivalent to exit() */
    extern void exit(int);
    exit(0);
}

int pthread_detach(pthread_t thread)
{
    (void)thread;
    return 0;
}

pthread_t pthread_self(void)
{
    return main_thread_id;
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

/*
 * Thread attributes
 */

int pthread_attr_init(pthread_attr_t *attr)
{
    if (!attr) return EINVAL;
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    (void)attr;
    (void)detachstate;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    (void)attr;
    if (detachstate) *detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    (void)attr;
    (void)stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    (void)attr;
    if (stacksize) *stacksize = 8192;  /* Default stack size */
    return 0;
}

/*
 * Mutex functions
 */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (!mutex) return EINVAL;
    mutex->locked = 0;
    mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    if (mutex->locked) return EBUSY;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;

    /* Check for deadlock in error-checking mode */
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutex->locked) {
        return EDEADLK;
    }

    /* Recursive mutex: allow multiple locks */
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        mutex->locked++;
        return 0;
    }

    /* Normal mutex: just set flag (no contention in single-threaded) */
    mutex->locked = 1;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;

    if (mutex->locked && mutex->type != PTHREAD_MUTEX_RECURSIVE) {
        return EBUSY;
    }

    mutex->locked++;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;

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

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (!attr) return EINVAL;
    if (type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK)
        return EINVAL;
    attr->type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    if (!attr || !type) return EINVAL;
    *type = attr->type;
    return 0;
}

/*
 * Condition variable functions
 */

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    if (!cond) return EINVAL;
    memset(cond, 0, sizeof(*cond));
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    (void)cond;
    (void)mutex;
    /* In single-threaded mode, waiting would block forever */
    /* Return immediately (bad behavior but won't deadlock) */
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    (void)cond;
    (void)mutex;
    (void)abstime;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

/*
 * Condition variable attributes
 */

int pthread_condattr_init(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    (void)attr;
    return 0;
}

/*
 * Read-write lock functions
 */

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    (void)attr;
    if (!rwlock) return EINVAL;
    rwlock->readers = 0;
    rwlock->writer = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    if (rwlock->readers || rwlock->writer) return EBUSY;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    rwlock->readers++;
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    if (rwlock->writer) return EBUSY;
    rwlock->readers++;
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    rwlock->writer = 1;
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    if (rwlock->readers || rwlock->writer) return EBUSY;
    rwlock->writer = 1;
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
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

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine) return EINVAL;

    if (*once_control == 0) {
        *once_control = 1;
        init_routine();
    }
    return 0;
}

/*
 * Thread-local storage
 */

#define TLS_KEYS_MAX 64
static void *tls_values[TLS_KEYS_MAX];
static void (*tls_destructors[TLS_KEYS_MAX])(void *);
static int tls_key_used[TLS_KEYS_MAX];
static int tls_next_key = 0;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    if (!key) return EINVAL;

    for (int i = 0; i < TLS_KEYS_MAX; i++) {
        int k = (tls_next_key + i) % TLS_KEYS_MAX;
        if (!tls_key_used[k]) {
            tls_key_used[k] = 1;
            tls_destructors[k] = destructor;
            tls_values[k] = 0;
            *key = k;
            tls_next_key = (k + 1) % TLS_KEYS_MAX;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= TLS_KEYS_MAX || !tls_key_used[key])
        return EINVAL;

    tls_key_used[key] = 0;
    tls_destructors[key] = 0;
    tls_values[key] = 0;
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    if (key >= TLS_KEYS_MAX || !tls_key_used[key])
        return 0;
    return tls_values[key];
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= TLS_KEYS_MAX || !tls_key_used[key])
        return EINVAL;
    tls_values[key] = (void *)value;
    return 0;
}

/*
 * Cancellation (not supported)
 */

int pthread_cancel(pthread_t thread)
{
    (void)thread;
    return ENOSYS;
}

int pthread_setcancelstate(int state, int *oldstate)
{
    (void)state;
    if (oldstate) *oldstate = PTHREAD_CANCEL_DISABLE;
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype)
{
    (void)type;
    if (oldtype) *oldtype = PTHREAD_CANCEL_DEFERRED;
    return 0;
}

void pthread_testcancel(void)
{
    /* No cancellation in single-threaded mode */
}
