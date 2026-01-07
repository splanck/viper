//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/resource.c
// Purpose: Resource limits and usage functions for ViperOS libc.
// Key invariants: Default limits stored in static table; syscalls fallback.
// Ownership/Lifetime: Library; static limit table.
// Links: user/libc/include/sys/resource.h
//
//===----------------------------------------------------------------------===//

/**
 * @file resource.c
 * @brief Resource limits and usage functions for ViperOS libc.
 *
 * @details
 * This file implements POSIX resource management functions:
 *
 * - getrlimit/setrlimit: Get/set resource limits
 * - prlimit: Get and set resource limits atomically
 * - getrusage: Get resource usage statistics
 * - getpriority/setpriority: Get/set process priority
 *
 * Resource limits are stored in a static table with defaults.
 * The kernel may override these via syscalls; if syscalls are
 * not implemented, the library provides reasonable defaults.
 * Common limits include stack size, open files, and memory.
 */

#include "../include/sys/resource.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);

/* Syscall numbers */
#define SYS_GETRLIMIT 0xF0
#define SYS_SETRLIMIT 0xF1
#define SYS_PRLIMIT 0xF2
#define SYS_GETRUSAGE 0xF3
#define SYS_GETPRIORITY 0xF4
#define SYS_SETPRIORITY 0xF5

/* Default limits for the system */
static struct rlimit default_limits[RLIMIT_NLIMITS] = {
    [RLIMIT_CPU] = {RLIM_INFINITY, RLIM_INFINITY},
    [RLIMIT_FSIZE] = {RLIM_INFINITY, RLIM_INFINITY},
    [RLIMIT_DATA] = {RLIM_INFINITY, RLIM_INFINITY},
    [RLIMIT_STACK] = {8 * 1024 * 1024, RLIM_INFINITY}, /* 8MB default */
    [RLIMIT_CORE] = {0, RLIM_INFINITY},
    [RLIMIT_RSS] = {RLIM_INFINITY, RLIM_INFINITY},
    [RLIMIT_NPROC] = {1024, 1024},
    [RLIMIT_NOFILE] = {1024, 4096},
    [RLIMIT_MEMLOCK] = {64 * 1024, 64 * 1024},
    [RLIMIT_AS] = {RLIM_INFINITY, RLIM_INFINITY},
    [RLIMIT_LOCKS] = {RLIM_INFINITY, RLIM_INFINITY},
    [RLIMIT_SIGPENDING] = {1024, 1024},
    [RLIMIT_MSGQUEUE] = {819200, 819200},
    [RLIMIT_NICE] = {0, 0},
    [RLIMIT_RTPRIO] = {0, 0},
    [RLIMIT_RTTIME] = {RLIM_INFINITY, RLIM_INFINITY},
};

/*
 * getrlimit - Get resource limits
 */
int getrlimit(int resource, struct rlimit *rlim)
{
    if (!rlim)
    {
        errno = EFAULT;
        return -1;
    }

    if (resource < 0 || resource >= RLIMIT_NLIMITS)
    {
        errno = EINVAL;
        return -1;
    }

    /* Try syscall first */
    long result = __syscall2(SYS_GETRLIMIT, (long)resource, (long)rlim);
    if (result == 0)
    {
        return 0;
    }

    /* Fall back to static defaults */
    *rlim = default_limits[resource];
    return 0;
}

/*
 * setrlimit - Set resource limits
 */
int setrlimit(int resource, const struct rlimit *rlim)
{
    if (!rlim)
    {
        errno = EFAULT;
        return -1;
    }

    if (resource < 0 || resource >= RLIMIT_NLIMITS)
    {
        errno = EINVAL;
        return -1;
    }

    /* Validate limits */
    if (rlim->rlim_cur > rlim->rlim_max)
    {
        errno = EINVAL;
        return -1;
    }

    /* Try syscall */
    long result = __syscall2(SYS_SETRLIMIT, (long)resource, (long)rlim);
    if (result < 0)
    {
        /* Syscall not implemented - store locally */
        if (resource >= 0 && resource < RLIMIT_NLIMITS)
        {
            /* Only allow lowering limits, not raising above max */
            if (rlim->rlim_max > default_limits[resource].rlim_max)
            {
                errno = EPERM;
                return -1;
            }
            default_limits[resource] = *rlim;
        }
    }

    return 0;
}

/*
 * prlimit - Get and set resource limits
 */
int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit)
{
    if (resource < 0 || resource >= RLIMIT_NLIMITS)
    {
        errno = EINVAL;
        return -1;
    }

    /* Try syscall */
    long result =
        __syscall4(SYS_PRLIMIT, (long)pid, (long)resource, (long)new_limit, (long)old_limit);
    if (result < 0)
    {
        /* Syscall not implemented - handle locally for current process */
        if (pid != 0)
        {
            errno = ESRCH;
            return -1;
        }

        if (old_limit)
        {
            *old_limit = default_limits[resource];
        }

        if (new_limit)
        {
            if (new_limit->rlim_cur > new_limit->rlim_max)
            {
                errno = EINVAL;
                return -1;
            }
            default_limits[resource] = *new_limit;
        }
    }

    return 0;
}

/*
 * getrusage - Get resource usage
 */
int getrusage(int who, struct rusage *usage)
{
    if (!usage)
    {
        errno = EFAULT;
        return -1;
    }

    if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN && who != RUSAGE_THREAD)
    {
        errno = EINVAL;
        return -1;
    }

    /* Try syscall */
    long result = __syscall2(SYS_GETRUSAGE, (long)who, (long)usage);
    if (result < 0)
    {
        /* Syscall not implemented - return zeros */
        memset(usage, 0, sizeof(struct rusage));
    }

    return 0;
}

/*
 * getpriority - Get process priority
 */
int getpriority(int which, id_t who)
{
    if (which < 0 || which > PRIO_USER)
    {
        errno = EINVAL;
        return -1;
    }

    /* Try syscall */
    long result = __syscall2(SYS_GETPRIORITY, (long)which, (long)who);
    if (result < 0 && result > -4096)
    {
        errno = (int)(-result);
        return -1;
    }

    if (result < -4096)
    {
        /* Syscall not implemented - return default priority */
        return 0;
    }

    return (int)result;
}

/*
 * setpriority - Set process priority
 */
int setpriority(int which, id_t who, int prio)
{
    if (which < 0 || which > PRIO_USER)
    {
        errno = EINVAL;
        return -1;
    }

    if (prio < PRIO_MIN)
        prio = PRIO_MIN;
    if (prio > PRIO_MAX)
        prio = PRIO_MAX;

    /* Try syscall */
    long result = __syscall3(SYS_SETPRIORITY, (long)which, (long)who, (long)prio);
    if (result < 0 && result > -4096)
    {
        errno = (int)(-result);
        return -1;
    }

    /* Syscall not implemented - silently succeed */
    return 0;
}
