//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/signal.c
// Purpose: Signal handling functions for ViperDOS libc.
// Key invariants: SIGKILL/SIGSTOP cannot be caught; signal mask per-process.
// Ownership/Lifetime: Library; signal handlers persist until changed.
// Links: user/libc/include/signal.h
//
//===----------------------------------------------------------------------===//

/**
 * @file signal.c
 * @brief Signal handling functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX signal handling:
 *
 * - Signal handling: signal, sigaction, raise, kill
 * - Signal sets: sigemptyset, sigfillset, sigaddset, sigdelset, sigismember
 * - Signal mask: sigprocmask, sigpending, sigsuspend
 * - Signal info: strsignal, psignal
 *
 * Signals SIGKILL and SIGSTOP cannot be caught or blocked.
 * Signal handlers are process-wide and persist until explicitly changed.
 */

#include "../include/signal.h"
#include "../include/stdio.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Syscall numbers */
#define SYS_SIGACTION 0x90
#define SYS_SIGPROCMASK 0x91
#define SYS_KILL 0x93
#define SYS_SIGPENDING 0x94
#define SYS_TASK_CURRENT 0x02

/* Signal names for strsignal */
static const char *signal_names[] = {
    "Unknown signal 0",
    "Hangup",                   /* SIGHUP */
    "Interrupt",                /* SIGINT */
    "Quit",                     /* SIGQUIT */
    "Illegal instruction",      /* SIGILL */
    "Trace/breakpoint trap",    /* SIGTRAP */
    "Aborted",                  /* SIGABRT */
    "Bus error",                /* SIGBUS */
    "Floating point exception", /* SIGFPE */
    "Killed",                   /* SIGKILL */
    "User defined signal 1",    /* SIGUSR1 */
    "Segmentation fault",       /* SIGSEGV */
    "User defined signal 2",    /* SIGUSR2 */
    "Broken pipe",              /* SIGPIPE */
    "Alarm clock",              /* SIGALRM */
    "Terminated",               /* SIGTERM */
    "Stack fault",              /* SIGSTKFLT */
    "Child exited",             /* SIGCHLD */
    "Continued",                /* SIGCONT */
    "Stopped (signal)",         /* SIGSTOP */
    "Stopped",                  /* SIGTSTP */
    "Stopped (tty input)",      /* SIGTTIN */
    "Stopped (tty output)",     /* SIGTTOU */
    "Urgent I/O condition",     /* SIGURG */
    "CPU time limit exceeded",  /* SIGXCPU */
    "File size limit exceeded", /* SIGXFSZ */
    "Virtual timer expired",    /* SIGVTALRM */
    "Profiling timer expired",  /* SIGPROF */
    "Window changed",           /* SIGWINCH */
    "I/O possible",             /* SIGIO */
    "Power failure",            /* SIGPWR */
    "Bad system call",          /* SIGSYS */
};

#define NUM_SIGNAL_NAMES (sizeof(signal_names) / sizeof(signal_names[0]))

/* Signal handler table - reserved for future use */
static sighandler_t signal_handlers[NSIG] __attribute__((unused));

/**
 * @brief Install a signal handler.
 *
 * @details
 * Sets the disposition of the signal signum to handler, which can be:
 * - SIG_IGN: Ignore the signal
 * - SIG_DFL: Restore default action
 * - A function pointer: Custom signal handler
 *
 * This function provides a simplified interface to sigaction(). The handler
 * is installed with SA_RESTART flag, meaning interrupted system calls will
 * be automatically restarted.
 *
 * @warning SIGKILL and SIGSTOP cannot be caught or ignored; attempts to do
 * so will return SIG_ERR.
 *
 * @param signum Signal number to handle (1-31, excluding SIGKILL/SIGSTOP).
 * @param handler New signal handler (SIG_IGN, SIG_DFL, or function pointer).
 * @return Previous signal handler on success, or SIG_ERR on error.
 *
 * @see sigaction, raise, kill
 */
sighandler_t signal(int signum, sighandler_t handler)
{
    if (signum < 1 || signum >= NSIG)
        return SIG_ERR;

    /* SIGKILL and SIGSTOP cannot be caught */
    if (signum == SIGKILL || signum == SIGSTOP)
        return SIG_ERR;

    struct sigaction act, oldact;
    act.sa_handler = handler;
    act.sa_mask = 0;
    act.sa_flags = SA_RESTART;
    act.sa_restorer = (void (*)(void))0;

    if (sigaction(signum, &act, &oldact) < 0)
        return SIG_ERR;

    return oldact.sa_handler;
}

/**
 * @brief Send a signal to the calling process.
 *
 * @details
 * Sends the specified signal to the current process. This is equivalent
 * to calling kill(getpid(), sig). The signal will be delivered to a handler
 * if one is installed, or cause the default action otherwise.
 *
 * @param sig Signal number to send.
 * @return 0 on success, non-zero on error.
 *
 * @see kill, signal, sigaction
 */
int raise(int sig)
{
    long pid = __syscall1(SYS_TASK_CURRENT, 0);
    return kill((int)pid, sig);
}

/**
 * @brief Send a signal to a process or process group.
 *
 * @details
 * Sends the signal sig to the process specified by pid:
 * - pid > 0: Send to the process with that process ID
 * - pid == 0: Send to all processes in the caller's process group
 * - pid == -1: Send to all processes (broadcast, requires privileges)
 * - pid < -1: Send to all processes in process group |pid|
 *
 * If sig is 0 (null signal), no signal is sent but error checking is still
 * performed. This can be used to check process existence.
 *
 * @param pid Target process ID, process group, or special value.
 * @param sig Signal number to send (0 for existence check only).
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see raise, sigaction, signal
 */
int kill(int pid, int sig)
{
    return (int)__syscall2(SYS_KILL, pid, sig);
}

/**
 * @brief Examine and change signal action.
 *
 * @details
 * The sigaction() function provides a more flexible interface for signal
 * handling than the simple signal() function. It allows control over:
 * - The signal handler function
 * - Which signals are blocked during handler execution (sa_mask)
 * - Various flags controlling handler behavior (sa_flags)
 *
 * If act is non-NULL, the new action for signal signum is installed.
 * If oldact is non-NULL, the previous action is saved there.
 *
 * Common sa_flags values:
 * - SA_RESTART: Automatically restart interrupted system calls
 * - SA_NOCLDSTOP: Don't generate SIGCHLD when children stop
 * - SA_SIGINFO: Use sa_sigaction instead of sa_handler
 *
 * @warning SIGKILL and SIGSTOP cannot have custom handlers installed.
 *
 * @param signum Signal number to modify (1-31, not SIGKILL/SIGSTOP).
 * @param act New signal action to install (or NULL to query only).
 * @param oldact Buffer to store previous action (or NULL if not needed).
 * @return 0 on success, -1 on error.
 *
 * @see signal, kill, sigprocmask
 */
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (signum < 1 || signum >= NSIG)
        return -1;

    /* SIGKILL and SIGSTOP cannot have custom handlers */
    if (signum == SIGKILL || signum == SIGSTOP)
        return -1;

    return (int)__syscall3(SYS_SIGACTION, signum, (long)act, (long)oldact);
}

/**
 * @defgroup sigset Signal Set Operations
 * @brief Functions for manipulating signal sets.
 *
 * Signal sets are used with sigprocmask() to specify which signals should
 * be blocked, and with sigaction() to specify which signals should be
 * blocked during execution of a signal handler.
 * @{
 */

/**
 * @brief Initialize a signal set to empty (no signals).
 *
 * @details
 * Clears all signals from the set, so that sigismember() returns 0 for
 * all signal numbers. This must be called before using sigaddset() to
 * build up a set of specific signals.
 *
 * @param set Pointer to signal set to initialize.
 * @return 0 on success, -1 if set is NULL.
 *
 * @see sigfillset, sigaddset, sigdelset, sigismember
 */
int sigemptyset(sigset_t *set)
{
    if (!set)
        return -1;
    *set = 0;
    return 0;
}

/**
 * @brief Initialize a signal set to full (all signals).
 *
 * @details
 * Adds all signals to the set, so that sigismember() returns 1 for all
 * valid signal numbers. This is useful when you want to block all signals
 * except a few (use sigdelset() to remove specific ones).
 *
 * @note Even with a full set, SIGKILL and SIGSTOP cannot actually be blocked
 * by sigprocmask() - these signals are always deliverable.
 *
 * @param set Pointer to signal set to initialize.
 * @return 0 on success, -1 if set is NULL.
 *
 * @see sigemptyset, sigaddset, sigdelset, sigismember
 */
int sigfillset(sigset_t *set)
{
    if (!set)
        return -1;
    *set = ~(sigset_t)0;
    return 0;
}

/**
 * @brief Add a signal to a signal set.
 *
 * @details
 * Adds the specified signal to set, so that subsequent calls to
 * sigismember(set, signum) will return 1. The set must have been
 * initialized with sigemptyset() or sigfillset() before calling this.
 *
 * @param set Pointer to signal set to modify.
 * @param signum Signal number to add (1 to NSIG-1).
 * @return 0 on success, -1 if set is NULL or signum is invalid.
 *
 * @see sigemptyset, sigfillset, sigdelset, sigismember
 */
int sigaddset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG)
        return -1;
    *set |= (1UL << signum);
    return 0;
}

/**
 * @brief Remove a signal from a signal set.
 *
 * @details
 * Removes the specified signal from set, so that subsequent calls to
 * sigismember(set, signum) will return 0. This is often used after
 * sigfillset() to create a set containing "all signals except X".
 *
 * @param set Pointer to signal set to modify.
 * @param signum Signal number to remove (1 to NSIG-1).
 * @return 0 on success, -1 if set is NULL or signum is invalid.
 *
 * @see sigemptyset, sigfillset, sigaddset, sigismember
 */
int sigdelset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG)
        return -1;
    *set &= ~(1UL << signum);
    return 0;
}

/**
 * @brief Test whether a signal is a member of a signal set.
 *
 * @details
 * Checks if the specified signal is present in the set. Returns 1 if
 * the signal is a member, 0 if not, or -1 on error.
 *
 * @param set Pointer to signal set to query.
 * @param signum Signal number to test (1 to NSIG-1).
 * @return 1 if signum is in set, 0 if not, -1 on error.
 *
 * @see sigemptyset, sigfillset, sigaddset, sigdelset
 */
int sigismember(const sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG)
        return -1;
    return (*set & (1UL << signum)) ? 1 : 0;
}

/** @} */ /* end of sigset group */

/**
 * @defgroup sigmask Signal Mask Operations
 * @brief Functions for controlling the process signal mask.
 *
 * The signal mask determines which signals are blocked (delayed) from
 * delivery to the process. Blocked signals remain pending until unblocked.
 * @{
 */

/**
 * @brief Examine and change blocked signals.
 *
 * @details
 * Controls which signals are blocked from delivery to the current thread.
 * The behavior depends on the 'how' parameter:
 *
 * - SIG_BLOCK: Add signals in 'set' to the current mask (block more)
 * - SIG_UNBLOCK: Remove signals in 'set' from the current mask (unblock)
 * - SIG_SETMASK: Replace the current mask with 'set'
 *
 * If 'oldset' is non-NULL, the previous signal mask is stored there.
 * If 'set' is NULL, the current mask is unchanged but can be retrieved
 * via 'oldset'.
 *
 * Blocked signals are not delivered until unblocked; they remain pending.
 * SIGKILL and SIGSTOP can never be blocked.
 *
 * @param how How to modify the mask (SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK).
 * @param set New signal mask to apply (or NULL to query only).
 * @param oldset Buffer to store previous mask (or NULL if not needed).
 * @return 0 on success, -1 on error.
 *
 * @see sigpending, sigsuspend, sigaction
 */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return (int)__syscall3(SYS_SIGPROCMASK, how, (long)set, (long)oldset);
}

/**
 * @brief Examine pending signals.
 *
 * @details
 * Returns the set of signals that are currently pending for delivery to
 * the calling thread. A signal is pending if it has been raised but is
 * currently blocked by the signal mask.
 *
 * This function can be used to check what signals are waiting before
 * unblocking them, allowing the program to prepare for signal handling.
 *
 * @param set Buffer to receive the set of pending signals.
 * @return 0 on success, -1 if set is NULL.
 *
 * @see sigprocmask, sigsuspend
 */
int sigpending(sigset_t *set)
{
    if (!set)
        return -1;
    return (int)__syscall1(SYS_SIGPENDING, (long)set);
}

/**
 * @brief Wait for a signal with a temporary signal mask.
 *
 * @details
 * Temporarily replaces the current signal mask with the one specified by
 * 'mask', then suspends the process until a signal is delivered that either
 * terminates the process or causes invocation of a signal handler.
 *
 * When a signal handler returns, sigsuspend() restores the original signal
 * mask and returns -1 with errno set to EINTR. This is the only possible
 * return behavior.
 *
 * This function is typically used to atomically unblock signals and wait
 * for them, avoiding race conditions that would occur with separate
 * sigprocmask() and pause() calls.
 *
 * @note Not implemented in ViperDOS - always returns -1.
 *
 * @param mask Temporary signal mask to use while waiting.
 * @return Always returns -1 with errno set to EINTR (or ENOSYS if not implemented).
 *
 * @see sigprocmask, pause, sigwait
 */
int sigsuspend(const sigset_t *mask)
{
    /* Not implemented - would require atomic mask change + wait */
    (void)mask;
    return -1;
}

/** @} */ /* end of sigmask group */

/**
 * @brief Get string describing a signal.
 *
 * @details
 * Returns a pointer to a string that describes the signal signum.
 * The returned string is either a known signal name (e.g., "Terminated",
 * "Segmentation fault") or "Unknown signal N" for unrecognized signals.
 *
 * @warning The returned pointer may point to static storage that is
 * overwritten by subsequent calls. The string should not be modified.
 *
 * @param signum Signal number to describe.
 * @return Pointer to a string describing the signal (never NULL).
 *
 * @see psignal, strerror
 */
const char *strsignal(int signum)
{
    static char unknown_buf[32];

    if (signum >= 0 && (size_t)signum < NUM_SIGNAL_NAMES)
        return signal_names[signum];

    /* Build "Unknown signal N" string */
    char *p = unknown_buf;
    const char *prefix = "Unknown signal ";
    while (*prefix)
        *p++ = *prefix++;

    int n = signum;
    if (n < 0)
    {
        *p++ = '-';
        n = -n;
    }
    char digits[12];
    int i = 0;
    do
    {
        digits[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    while (i > 0)
        *p++ = digits[--i];
    *p = '\0';

    return unknown_buf;
}

/**
 * @brief Print a signal message to stderr.
 *
 * @details
 * Prints a message describing the signal to standard error. If s is
 * non-NULL and non-empty, it is printed followed by ": " before the
 * signal description. The output format is:
 *
 *   [s: ]<signal description>\n
 *
 * This is the signal equivalent of perror() for error numbers.
 *
 * @param sig Signal number to describe.
 * @param s Optional prefix string (or NULL for no prefix).
 *
 * @see strsignal, perror
 */
void psignal(int sig, const char *s)
{
    if (s && *s)
    {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(strsignal(sig), stderr);
    fputc('\n', stderr);
}
