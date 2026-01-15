//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/shm.c
// Purpose: System V shared memory stubs for ViperDOS libc.
// Key invariants: All functions return ENOSYS (not implemented).
// Ownership/Lifetime: Library; stub implementations.
// Links: user/libc/include/sys/shm.h
//
//===----------------------------------------------------------------------===//

/**
 * @file shm.c
 * @brief System V shared memory stubs for ViperDOS libc.
 *
 * @details
 * This file provides stub implementations for System V shared memory:
 *
 * - shmget: Get or create shared memory segment (stub)
 * - shmat: Attach shared memory segment (stub)
 * - shmdt: Detach shared memory segment (stub)
 * - shmctl: Shared memory control operations (stub)
 *
 * Shared memory is not yet implemented in ViperDOS. All functions
 * return -1 with errno set to ENOSYS.
 */

#include "../include/sys/shm.h"
#include "../include/errno.h"

/*
 * shmget - Get or create shared memory segment
 *
 * ViperDOS stub - shared memory not yet implemented.
 */
int shmget(key_t key, size_t size, int shmflg)
{
    (void)key;
    (void)size;
    (void)shmflg;

    /* Shared memory not supported in ViperDOS */
    errno = ENOSYS;
    return -1;
}

/*
 * shmat - Attach shared memory segment
 *
 * ViperDOS stub - shared memory not yet implemented.
 */
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
    (void)shmid;
    (void)shmaddr;
    (void)shmflg;

    /* Shared memory not supported in ViperDOS */
    errno = ENOSYS;
    return (void *)-1;
}

/*
 * shmdt - Detach shared memory segment
 *
 * ViperDOS stub - shared memory not yet implemented.
 */
int shmdt(const void *shmaddr)
{
    (void)shmaddr;

    /* Shared memory not supported in ViperDOS */
    errno = ENOSYS;
    return -1;
}

/*
 * shmctl - Shared memory control operations
 *
 * ViperDOS stub - shared memory not yet implemented.
 */
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    (void)shmid;
    (void)cmd;
    (void)buf;

    /* Shared memory not supported in ViperDOS */
    errno = ENOSYS;
    return -1;
}
