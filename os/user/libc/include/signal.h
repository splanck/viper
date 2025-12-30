#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Signal numbers */
#define SIGHUP    1   /* Hangup */
#define SIGINT    2   /* Interrupt */
#define SIGQUIT   3   /* Quit */
#define SIGILL    4   /* Illegal instruction */
#define SIGTRAP   5   /* Trace trap */
#define SIGABRT   6   /* Abort */
#define SIGIOT    SIGABRT
#define SIGBUS    7   /* Bus error */
#define SIGFPE    8   /* Floating-point exception */
#define SIGKILL   9   /* Kill (cannot be caught or ignored) */
#define SIGUSR1   10  /* User-defined signal 1 */
#define SIGSEGV   11  /* Segmentation violation */
#define SIGUSR2   12  /* User-defined signal 2 */
#define SIGPIPE   13  /* Broken pipe */
#define SIGALRM   14  /* Alarm clock */
#define SIGTERM   15  /* Termination */
#define SIGSTKFLT 16  /* Stack fault */
#define SIGCHLD   17  /* Child status changed */
#define SIGCONT   18  /* Continue */
#define SIGSTOP   19  /* Stop (cannot be caught or ignored) */
#define SIGTSTP   20  /* Keyboard stop */
#define SIGTTIN   21  /* Background read from tty */
#define SIGTTOU   22  /* Background write to tty */
#define SIGURG    23  /* Urgent condition on socket */
#define SIGXCPU   24  /* CPU limit exceeded */
#define SIGXFSZ   25  /* File size limit exceeded */
#define SIGVTALRM 26  /* Virtual alarm clock */
#define SIGPROF   27  /* Profiling alarm clock */
#define SIGWINCH  28  /* Window size change */
#define SIGIO     29  /* I/O now possible */
#define SIGPOLL   SIGIO
#define SIGPWR    30  /* Power failure restart */
#define SIGSYS    31  /* Bad system call */

#define NSIG      32  /* Number of signals */

/* Signal handler type */
typedef void (*sighandler_t)(int);

/* Atomic type safe for use in signal handlers */
typedef volatile int sig_atomic_t;

/* Special signal handlers */
#define SIG_DFL ((sighandler_t)0)   /* Default action */
#define SIG_IGN ((sighandler_t)1)   /* Ignore signal */
#define SIG_ERR ((sighandler_t)-1)  /* Error return */

/* Signal set type */
typedef unsigned long sigset_t;

/* sigaction flags */
#define SA_NOCLDSTOP  0x00000001  /* Don't send SIGCHLD when children stop */
#define SA_NOCLDWAIT  0x00000002  /* Don't create zombie on child death */
#define SA_SIGINFO    0x00000004  /* Use sa_sigaction instead of sa_handler */
#define SA_ONSTACK    0x08000000  /* Use alternate signal stack */
#define SA_RESTART    0x10000000  /* Restart syscalls */
#define SA_NODEFER    0x40000000  /* Don't block signal during handler */
#define SA_RESETHAND  0x80000000  /* Reset to SIG_DFL on handler entry */

/* sigprocmask how values */
#define SIG_BLOCK     0  /* Block signals in set */
#define SIG_UNBLOCK   1  /* Unblock signals in set */
#define SIG_SETMASK   2  /* Set signal mask to set */

/* sigaction structure */
struct sigaction {
    sighandler_t sa_handler;   /* Signal handler */
    sigset_t     sa_mask;      /* Signals to block during handler */
    int          sa_flags;     /* Flags */
    void         (*sa_restorer)(void);  /* Restore function (internal) */
};

/* sigevent for async notification */
union sigval {
    int   sival_int;    /* Integer value */
    void *sival_ptr;    /* Pointer value */
};

#define SIGEV_NONE      0   /* No notification */
#define SIGEV_SIGNAL    1   /* Generate a signal */
#define SIGEV_THREAD    2   /* Call a function in a new thread */

struct sigevent {
    int             sigev_notify;           /* Notification type */
    int             sigev_signo;            /* Signal number */
    union sigval    sigev_value;            /* Signal value */
    void          (*sigev_notify_function)(union sigval);  /* Notify function */
    void           *sigev_notify_attributes;  /* Thread attributes */
};

/* Signal functions */
sighandler_t signal(int signum, sighandler_t handler);
int raise(int sig);
int kill(int pid, int sig);

/* sigaction */
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

/* Signal set operations */
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

/* Signal mask operations */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);

/* String conversion */
const char *strsignal(int signum);
void psignal(int sig, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_H */
