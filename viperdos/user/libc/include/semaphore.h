/*
 * ViperDOS C Library - semaphore.h
 * POSIX semaphore operations
 */

#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <fcntl.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Semaphore value limit */
#define SEM_VALUE_MAX 32767

/* Failed semaphore open */
#define SEM_FAILED ((sem_t *)-1)

    /* Semaphore type */
    typedef struct
    {
        unsigned int value;
        int pshared;
    } sem_t;

    /*
     * Initialize an unnamed semaphore.
     * pshared: 0 for thread-shared, non-zero for process-shared
     * value: initial semaphore value
     * Returns 0 on success, -1 on error.
     */
    int sem_init(sem_t *sem, int pshared, unsigned int value);

    /*
     * Destroy an unnamed semaphore.
     * Returns 0 on success, -1 on error.
     */
    int sem_destroy(sem_t *sem);

    /*
     * Open a named semaphore.
     * name: semaphore name (must start with /)
     * oflag: O_CREAT, O_EXCL flags
     * mode: permission bits (when creating)
     * value: initial value (when creating)
     * Returns pointer to semaphore on success, SEM_FAILED on error.
     */
    sem_t *sem_open(const char *name, int oflag, ...);

    /*
     * Close a named semaphore.
     * Returns 0 on success, -1 on error.
     */
    int sem_close(sem_t *sem);

    /*
     * Remove a named semaphore.
     * Returns 0 on success, -1 on error.
     */
    int sem_unlink(const char *name);

    /*
     * Lock (decrement) a semaphore.
     * Blocks if value is zero.
     * Returns 0 on success, -1 on error.
     */
    int sem_wait(sem_t *sem);

    /*
     * Try to lock a semaphore without blocking.
     * Returns 0 on success, -1 with EAGAIN if would block.
     */
    int sem_trywait(sem_t *sem);

    /*
     * Lock a semaphore with timeout.
     * Returns 0 on success, -1 on error or timeout (ETIMEDOUT).
     */
    int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);

    /*
     * Unlock (increment) a semaphore.
     * Returns 0 on success, -1 on error.
     */
    int sem_post(sem_t *sem);

    /*
     * Get the current value of a semaphore.
     * Returns 0 on success, -1 on error.
     */
    int sem_getvalue(sem_t *sem, int *sval);

#ifdef __cplusplus
}
#endif

#endif /* _SEMAPHORE_H */
