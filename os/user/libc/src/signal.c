//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/signal.c
// Purpose: Signal handling functions for ViperOS libc.
// Key invariants: SIGKILL/SIGSTOP cannot be caught; signal mask per-process.
// Ownership/Lifetime: Library; signal handlers persist until changed.
// Links: user/libc/include/signal.h
//
//===----------------------------------------------------------------------===//

/**
 * @file signal.c
 * @brief Signal handling functions for ViperOS libc.
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

int raise(int sig)
{
    long pid = __syscall1(SYS_TASK_CURRENT, 0);
    return kill((int)pid, sig);
}

int kill(int pid, int sig)
{
    return (int)__syscall2(SYS_KILL, pid, sig);
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (signum < 1 || signum >= NSIG)
        return -1;

    /* SIGKILL and SIGSTOP cannot have custom handlers */
    if (signum == SIGKILL || signum == SIGSTOP)
        return -1;

    return (int)__syscall3(SYS_SIGACTION, signum, (long)act, (long)oldact);
}

/* Signal set operations */
int sigemptyset(sigset_t *set)
{
    if (!set)
        return -1;
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set)
{
    if (!set)
        return -1;
    *set = ~(sigset_t)0;
    return 0;
}

int sigaddset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG)
        return -1;
    *set |= (1UL << signum);
    return 0;
}

int sigdelset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG)
        return -1;
    *set &= ~(1UL << signum);
    return 0;
}

int sigismember(const sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG)
        return -1;
    return (*set & (1UL << signum)) ? 1 : 0;
}

/* Signal mask operations */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return (int)__syscall3(SYS_SIGPROCMASK, how, (long)set, (long)oldset);
}

int sigpending(sigset_t *set)
{
    if (!set)
        return -1;
    return (int)__syscall1(SYS_SIGPENDING, (long)set);
}

int sigsuspend(const sigset_t *mask)
{
    /* Not implemented - would require atomic mask change + wait */
    (void)mask;
    return -1;
}

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
