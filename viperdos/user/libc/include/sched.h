/*
 * ViperDOS libc - sched.h
 * Process scheduling
 */

#ifndef _SCHED_H
#define _SCHED_H

#include "sys/types.h"
#include "time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scheduling policies */
#define SCHED_OTHER 0 /* Standard time-sharing */
#define SCHED_FIFO 1  /* First-in first-out */
#define SCHED_RR 2    /* Round-robin */
#define SCHED_BATCH 3 /* Batch processing */
#define SCHED_IDLE 5  /* Idle priority */

/* Scheduling parameters */
struct sched_param {
    int sched_priority; /* Scheduling priority */
};

/* CPU set type (simplified) */
#define CPU_SETSIZE 128

typedef struct {
    unsigned long __bits[CPU_SETSIZE / (8 * sizeof(unsigned long))];
} cpu_set_t;

/* CPU set operations */
#define CPU_SET(cpu, set)                                                                          \
    ((set)->__bits[(cpu) / (8 * sizeof(unsigned long))] |=                                         \
     (1UL << ((cpu) % (8 * sizeof(unsigned long)))))

#define CPU_CLR(cpu, set)                                                                          \
    ((set)->__bits[(cpu) / (8 * sizeof(unsigned long))] &=                                         \
     ~(1UL << ((cpu) % (8 * sizeof(unsigned long)))))

#define CPU_ISSET(cpu, set)                                                                        \
    (((set)->__bits[(cpu) / (8 * sizeof(unsigned long))] &                                         \
      (1UL << ((cpu) % (8 * sizeof(unsigned long))))) != 0)

#define CPU_ZERO(set)                                                                              \
    do {                                                                                           \
        for (size_t __i = 0; __i < sizeof((set)->__bits) / sizeof((set)->__bits[0]); __i++)        \
            (set)->__bits[__i] = 0;                                                                \
    } while (0)

/*
 * sched_yield - Yield processor
 *
 * Returns 0 on success, -1 on failure.
 */
int sched_yield(void);

/*
 * sched_get_priority_max - Get maximum priority value
 */
int sched_get_priority_max(int policy);

/*
 * sched_get_priority_min - Get minimum priority value
 */
int sched_get_priority_min(int policy);

/*
 * sched_getscheduler - Get scheduling policy
 */
int sched_getscheduler(pid_t pid);

/*
 * sched_setscheduler - Set scheduling policy
 */
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);

/*
 * sched_getparam - Get scheduling parameters
 */
int sched_getparam(pid_t pid, struct sched_param *param);

/*
 * sched_setparam - Set scheduling parameters
 */
int sched_setparam(pid_t pid, const struct sched_param *param);

/*
 * sched_rr_get_interval - Get round-robin time quantum
 */
int sched_rr_get_interval(pid_t pid, struct timespec *interval);

/*
 * sched_getaffinity - Get CPU affinity mask
 */
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask);

/*
 * sched_setaffinity - Set CPU affinity mask
 */
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);

#ifdef __cplusplus
}
#endif

#endif /* _SCHED_H */
