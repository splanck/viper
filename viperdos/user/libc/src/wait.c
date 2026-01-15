//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/wait.c
// Purpose: Process wait functions for ViperDOS libc.
// Key invariants: Wraps kernel wait4 syscall; fills rusage if provided.
// Ownership/Lifetime: Library; wraps kernel syscalls.
// Links: user/libc/include/sys/wait.h
//
//===----------------------------------------------------------------------===//

/**
 * @file wait.c
 * @brief Process wait functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX process waiting functions:
 *
 * - wait: Wait for any child process to terminate
 * - waitpid: Wait for specific child process
 * - wait3: Wait with resource usage (any child)
 * - wait4: Wait with resource usage (specific child)
 * - waitid: ID-based wait with siginfo
 *
 * The WIFEXITED, WEXITSTATUS, WIFSIGNALED, etc. macros are used to
 * interpret the status value returned via wstatus.
 */

#include "../include/sys/wait.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall4(long num, long arg0, long arg1, long arg2, long arg3);

/* Syscall numbers */
#define SYS_WAIT4 0xB0

/*
 * wait - Wait for any child process
 */
pid_t wait(int *wstatus)
{
    return waitpid(-1, wstatus, 0);
}

/*
 * waitpid - Wait for specific child process
 * pid > 0: wait for child with that PID
 * pid == -1: wait for any child
 * pid == 0: wait for any child in same process group
 * pid < -1: wait for any child in process group |pid|
 */
pid_t waitpid(pid_t pid, int *wstatus, int options)
{
    long result = __syscall4(SYS_WAIT4, pid, (long)wstatus, options, 0);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return (pid_t)result;
}

/*
 * wait3 - Wait with resource usage (any child)
 */
pid_t wait3(int *wstatus, int options, struct rusage *rusage)
{
    return wait4(-1, wstatus, options, rusage);
}

/*
 * wait4 - Wait with resource usage
 */
pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage)
{
    /* Clear rusage if provided */
    if (rusage)
    {
        memset(rusage, 0, sizeof(struct rusage));
    }

    long result = __syscall4(SYS_WAIT4, pid, (long)wstatus, options, (long)rusage);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return (pid_t)result;
}

/*
 * waitid - ID-based wait
 */
int waitid(idtype_t idtype, pid_t id, siginfo_t *infop, int options)
{
    pid_t pid;
    int wstatus = 0;

    /* Convert idtype to waitpid-style pid */
    switch (idtype)
    {
        case P_ALL:
            pid = -1;
            break;
        case P_PID:
            pid = id;
            break;
        case P_PGID:
            pid = -id;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    pid_t result = waitpid(pid, &wstatus, options);
    if (result < 0)
    {
        return -1;
    }

    /* Fill in siginfo_t if provided */
    if (infop)
    {
        infop->si_pid = result;
        infop->si_uid = 0; /* ViperDOS is single-user */

        if (WIFEXITED(wstatus))
        {
            infop->si_signo = 17; /* SIGCHLD */
            infop->si_code = 1;   /* CLD_EXITED */
            infop->si_status = WEXITSTATUS(wstatus);
        }
        else if (WIFSIGNALED(wstatus))
        {
            infop->si_signo = 17; /* SIGCHLD */
            infop->si_code = 2;   /* CLD_KILLED */
            infop->si_status = WTERMSIG(wstatus);
        }
        else if (WIFSTOPPED(wstatus))
        {
            infop->si_signo = 17; /* SIGCHLD */
            infop->si_code = 5;   /* CLD_STOPPED */
            infop->si_status = WSTOPSIG(wstatus);
        }
        else if (WIFCONTINUED(wstatus))
        {
            infop->si_signo = 17;  /* SIGCHLD */
            infop->si_code = 6;    /* CLD_CONTINUED */
            infop->si_status = 18; /* SIGCONT */
        }
    }

    return 0;
}
