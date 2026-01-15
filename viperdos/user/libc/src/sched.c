//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/sched.c
// Purpose: Process scheduling functions for ViperDOS libc.
// Key invariants: Single CPU; priority changes not supported.
// Ownership/Lifetime: Library; stub implementations.
// Links: user/libc/include/sched.h
//
//===----------------------------------------------------------------------===//

/**
 * @file sched.c
 * @brief Process scheduling functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX scheduling functions:
 *
 * - sched_yield: Yield processor to other threads
 * - sched_get_priority_max/min: Get priority range for a policy
 * - sched_getscheduler/setscheduler: Get/set scheduling policy
 * - sched_getparam/setparam: Get/set scheduling parameters
 * - sched_rr_get_interval: Get round-robin time quantum
 * - sched_getaffinity/setaffinity: Get/set CPU affinity
 *
 * ViperDOS runs on a single CPU and uses a simple scheduler.
 * Most scheduling APIs return reasonable defaults but do not
 * actually change kernel behavior. Policy changes are rejected
 * with EPERM.
 */

#include "../include/sched.h"
#include "../include/errno.h"

/*
 * sched_yield - Yield processor
 */
int sched_yield(void)
{
    /* ViperDOS single-threaded - just return success */
    return 0;
}

/*
 * sched_get_priority_max - Get maximum priority value
 */
int sched_get_priority_max(int policy)
{
    switch (policy)
    {
        case SCHED_FIFO:
        case SCHED_RR:
            return 99;
        case SCHED_OTHER:
        case SCHED_BATCH:
        case SCHED_IDLE:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

/*
 * sched_get_priority_min - Get minimum priority value
 */
int sched_get_priority_min(int policy)
{
    switch (policy)
    {
        case SCHED_FIFO:
        case SCHED_RR:
            return 1;
        case SCHED_OTHER:
        case SCHED_BATCH:
        case SCHED_IDLE:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

/*
 * sched_getscheduler - Get scheduling policy
 */
int sched_getscheduler(pid_t pid)
{
    (void)pid;
    /* ViperDOS uses SCHED_OTHER equivalent */
    return SCHED_OTHER;
}

/*
 * sched_setscheduler - Set scheduling policy
 */
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
    (void)pid;
    (void)policy;
    (void)param;
    /* ViperDOS doesn't support changing schedulers */
    errno = EPERM;
    return -1;
}

/*
 * sched_getparam - Get scheduling parameters
 */
int sched_getparam(pid_t pid, struct sched_param *param)
{
    (void)pid;
    if (!param)
    {
        errno = EINVAL;
        return -1;
    }
    param->sched_priority = 0;
    return 0;
}

/*
 * sched_setparam - Set scheduling parameters
 */
int sched_setparam(pid_t pid, const struct sched_param *param)
{
    (void)pid;
    (void)param;
    /* ViperDOS doesn't support changing priorities */
    errno = EPERM;
    return -1;
}

/*
 * sched_rr_get_interval - Get round-robin time quantum
 */
int sched_rr_get_interval(pid_t pid, struct timespec *interval)
{
    (void)pid;
    if (!interval)
    {
        errno = EINVAL;
        return -1;
    }
    /* Return a reasonable default (10ms) */
    interval->tv_sec = 0;
    interval->tv_nsec = 10000000; /* 10ms */
    return 0;
}

/*
 * sched_getaffinity - Get CPU affinity mask
 */
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
    (void)pid;
    if (!mask || cpusetsize < sizeof(cpu_set_t))
    {
        errno = EINVAL;
        return -1;
    }
    /* ViperDOS single CPU - set CPU 0 */
    CPU_ZERO(mask);
    CPU_SET(0, mask);
    return 0;
}

/*
 * sched_setaffinity - Set CPU affinity mask
 */
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask)
{
    (void)pid;
    (void)cpusetsize;
    (void)mask;
    /* ViperDOS single CPU - ignore */
    return 0;
}
