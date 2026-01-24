#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* waitpid options */
#define WNOHANG 0x00000001    /* Don't block waiting */
#define WUNTRACED 0x00000002  /* Report stopped children */
#define WCONTINUED 0x00000008 /* Report continued children */

/* Status analysis macros */
/* Status format: low 8 bits = signal or 0x7f for stopped, next 8 bits = exit code */
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status) ((status) & 0x7f)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status) (((status) >> 8) & 0xff)
#define WIFCONTINUED(status) ((status) == 0xffff)

/* Core dump flag */
#define WCOREDUMP(status) ((status) & 0x80)

/* Construct status values */
#define W_EXITCODE(ret, sig) (((ret) << 8) | (sig))
#define W_STOPCODE(sig) (((sig) << 8) | 0x7f)

/* Resource usage info (simplified) */
struct rusage {
    struct {
        long tv_sec;
        long tv_usec;
    } ru_utime; /* User time used */

    struct {
        long tv_sec;
        long tv_usec;
    } ru_stime; /* System time used */

    long ru_maxrss;   /* Maximum resident set size */
    long ru_ixrss;    /* Integral shared memory size */
    long ru_idrss;    /* Integral unshared data size */
    long ru_isrss;    /* Integral unshared stack size */
    long ru_minflt;   /* Minor page faults */
    long ru_majflt;   /* Major page faults */
    long ru_nswap;    /* Swaps */
    long ru_inblock;  /* Block input operations */
    long ru_oublock;  /* Block output operations */
    long ru_msgsnd;   /* Messages sent */
    long ru_msgrcv;   /* Messages received */
    long ru_nsignals; /* Signals received */
    long ru_nvcsw;    /* Voluntary context switches */
    long ru_nivcsw;   /* Involuntary context switches */
};

/* rusage who values */
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD 1

/* Wait for child process */
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
pid_t wait3(int *wstatus, int options, struct rusage *rusage);
pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);

/* ID-based wait */
typedef enum {
    P_ALL, /* Wait for any child */
    P_PID, /* Wait for specific PID */
    P_PGID /* Wait for any in process group */
} idtype_t;

/* siginfo_t structure (simplified) */
typedef struct {
    int si_signo;  /* Signal number */
    int si_code;   /* Signal code */
    pid_t si_pid;  /* Sending process ID */
    uid_t si_uid;  /* Sending user ID */
    int si_status; /* Exit value or signal */
} siginfo_t;

int waitid(idtype_t idtype, pid_t id, siginfo_t *infop, int options);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_WAIT_H */
