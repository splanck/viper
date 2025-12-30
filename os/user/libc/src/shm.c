/*
 * ViperOS libc - shm.c
 * Shared memory implementation (stubs)
 */

#include "../include/sys/shm.h"
#include "../include/errno.h"

/*
 * shmget - Get or create shared memory segment
 *
 * ViperOS stub - shared memory not yet implemented.
 */
int shmget(key_t key, size_t size, int shmflg)
{
    (void)key;
    (void)size;
    (void)shmflg;

    /* Shared memory not supported in ViperOS */
    errno = ENOSYS;
    return -1;
}

/*
 * shmat - Attach shared memory segment
 *
 * ViperOS stub - shared memory not yet implemented.
 */
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
    (void)shmid;
    (void)shmaddr;
    (void)shmflg;

    /* Shared memory not supported in ViperOS */
    errno = ENOSYS;
    return (void *)-1;
}

/*
 * shmdt - Detach shared memory segment
 *
 * ViperOS stub - shared memory not yet implemented.
 */
int shmdt(const void *shmaddr)
{
    (void)shmaddr;

    /* Shared memory not supported in ViperOS */
    errno = ENOSYS;
    return -1;
}

/*
 * shmctl - Shared memory control operations
 *
 * ViperOS stub - shared memory not yet implemented.
 */
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    (void)shmid;
    (void)cmd;
    (void)buf;

    /* Shared memory not supported in ViperOS */
    errno = ENOSYS;
    return -1;
}
