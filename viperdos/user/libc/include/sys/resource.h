/*
 * ViperDOS libc - sys/resource.h
 * Resource usage and limits
 */

#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include "../sys/types.h"
#include "../time.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Resource usage who values */
#define RUSAGE_SELF 0        /* Current process */
#define RUSAGE_CHILDREN (-1) /* All terminated children */
#define RUSAGE_THREAD 1      /* Current thread (Linux extension) */

/* Resource limit resources */
#define RLIMIT_CPU 0         /* CPU time in seconds */
#define RLIMIT_FSIZE 1       /* Maximum file size */
#define RLIMIT_DATA 2        /* Maximum data segment size */
#define RLIMIT_STACK 3       /* Maximum stack size */
#define RLIMIT_CORE 4        /* Maximum core file size */
#define RLIMIT_RSS 5         /* Maximum resident set size */
#define RLIMIT_NPROC 6       /* Maximum number of processes */
#define RLIMIT_NOFILE 7      /* Maximum number of open files */
#define RLIMIT_MEMLOCK 8     /* Maximum locked memory */
#define RLIMIT_AS 9          /* Maximum address space size */
#define RLIMIT_LOCKS 10      /* Maximum file locks */
#define RLIMIT_SIGPENDING 11 /* Maximum pending signals */
#define RLIMIT_MSGQUEUE 12   /* Maximum message queue bytes */
#define RLIMIT_NICE 13       /* Maximum nice priority */
#define RLIMIT_RTPRIO 14     /* Maximum real-time priority */
#define RLIMIT_RTTIME 15     /* Maximum real-time timeout */
#define RLIMIT_NLIMITS 16    /* Number of resource limits */

/* Infinite limit */
#define RLIM_INFINITY ((rlim_t) - 1)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY

/* Process priority ranges */
#define PRIO_MIN (-20)
#define PRIO_MAX 20

/* Process priority who values */
#define PRIO_PROCESS 0 /* Process */
#define PRIO_PGRP 1    /* Process group */
#define PRIO_USER 2    /* User */

    /* Resource limit type */
    typedef unsigned long rlim_t;

    /* Resource limit structure */
    struct rlimit
    {
        rlim_t rlim_cur; /* Soft limit (current) */
        rlim_t rlim_max; /* Hard limit (maximum) */
    };

    /* Resource usage structure */
    struct rusage
    {
        struct timeval ru_utime; /* User CPU time used */
        struct timeval ru_stime; /* System CPU time used */
        long ru_maxrss;          /* Maximum resident set size */
        long ru_ixrss;           /* Integral shared memory size */
        long ru_idrss;           /* Integral unshared data size */
        long ru_isrss;           /* Integral unshared stack size */
        long ru_minflt;          /* Page reclaims (soft page faults) */
        long ru_majflt;          /* Page faults (hard page faults) */
        long ru_nswap;           /* Swaps */
        long ru_inblock;         /* Block input operations */
        long ru_oublock;         /* Block output operations */
        long ru_msgsnd;          /* IPC messages sent */
        long ru_msgrcv;          /* IPC messages received */
        long ru_nsignals;        /* Signals received */
        long ru_nvcsw;           /* Voluntary context switches */
        long ru_nivcsw;          /* Involuntary context switches */
    };

    /*
     * getrlimit - Get resource limits
     *
     * Returns 0 on success, -1 on error with errno set.
     */
    int getrlimit(int resource, struct rlimit *rlim);

    /*
     * setrlimit - Set resource limits
     *
     * Returns 0 on success, -1 on error with errno set.
     */
    int setrlimit(int resource, const struct rlimit *rlim);

    /*
     * prlimit - Get and set resource limits (Linux extension)
     *
     * Returns 0 on success, -1 on error with errno set.
     */
    int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit);

    /*
     * getrusage - Get resource usage
     *
     * Returns 0 on success, -1 on error with errno set.
     */
    int getrusage(int who, struct rusage *usage);

    /*
     * getpriority - Get process priority
     *
     * Returns the priority on success, -1 on error with errno set.
     * Note: Can return -1 as a valid priority, so check errno.
     */
    int getpriority(int which, id_t who);

    /*
     * setpriority - Set process priority
     *
     * Returns 0 on success, -1 on error with errno set.
     */
    int setpriority(int which, id_t who, int prio);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_RESOURCE_H */
