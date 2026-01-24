/*
 * ViperDOS C Library - sys/sem.h
 * System V semaphore operations
 */

#ifndef _SYS_SEM_H
#define _SYS_SEM_H

#include <sys/ipc.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semaphore operation flags */
#define SEM_UNDO 0x1000 /* Undo operation on exit */

/* semctl() commands */
#define GETVAL 12   /* Get semval */
#define SETVAL 16   /* Set semval */
#define GETPID 11   /* Get sempid */
#define GETNCNT 14  /* Get semncnt */
#define GETZCNT 15  /* Get semzcnt */
#define GETALL 13   /* Get all semvals */
#define SETALL 17   /* Set all semvals */
#define IPC_STAT 2  /* Get semid_ds structure */
#define IPC_SET 1   /* Set ipc_perm options */
#define IPC_RMID 0  /* Remove identifier */
#define IPC_INFO 3  /* Get system info */
#define SEM_INFO 19 /* Get semaphore info */
#define SEM_STAT 18 /* Get semid_ds (special) */

/* Maximum values (implementation-defined) */
#define SEMMNI 128               /* Max number of semaphore sets */
#define SEMMSL 250               /* Max semaphores per semid */
#define SEMMNS (SEMMNI * SEMMSL) /* Max semaphores in system */
#define SEMOPM 32                /* Max operations per semop call */
#define SEMVMX 32767             /* Max semaphore value */
#define SEMAEM SEMVMX            /* Max adjust on exit value */
#define SEMUME SEMOPM            /* Max undo entries per process */
#define SEMMNU SEMMNS            /* Max undo structures in system */

/* Semaphore buffer for semop() */
struct sembuf {
    unsigned short sem_num; /* Semaphore number */
    short sem_op;           /* Semaphore operation */
    short sem_flg;          /* Operation flags */
};

/* Individual semaphore structure (kernel internal representation) */
struct sem {
    unsigned short semval;  /* Semaphore value */
    pid_t sempid;           /* PID of last operation */
    unsigned short semncnt; /* # waiting for increase */
    unsigned short semzcnt; /* # waiting for zero */
};

/* Semaphore set data structure */
struct semid_ds {
    struct ipc_perm sem_perm; /* Operation permission struct */
    time_t sem_otime;         /* Last semop() time */
    time_t sem_ctime;         /* Last change time */
    unsigned long sem_nsems;  /* Number of semaphores in set */
};

/* Info structure for IPC_INFO/SEM_INFO */
struct seminfo {
    int semmap; /* # of entries in semaphore map */
    int semmni; /* Max # of semaphore sets */
    int semmns; /* Max # of semaphores in system */
    int semmnu; /* Max # of undo structures in system */
    int semmsl; /* Max # of semaphores per set */
    int semopm; /* Max # of operations per semop call */
    int semume; /* Max # of undo entries per process */
    int semusz; /* Size of struct sem_undo */
    int semvmx; /* Max semaphore value */
    int semaem; /* Max value for adjust on exit */
};

/* Union for semctl() fourth argument */
union semun {
    int val;               /* Value for SETVAL */
    struct semid_ds *buf;  /* Buffer for IPC_STAT, IPC_SET */
    unsigned short *array; /* Array for GETALL, SETALL */
    struct seminfo *__buf; /* Buffer for IPC_INFO */
};

/*
 * Get a semaphore set identifier.
 * Returns semaphore set ID on success, -1 on error.
 */
int semget(key_t key, int nsems, int semflg);

/*
 * Perform semaphore operations.
 * Returns 0 on success, -1 on error.
 */
int semop(int semid, struct sembuf *sops, size_t nsops);

/*
 * Perform timed semaphore operations.
 * Returns 0 on success, -1 on error.
 */
int semtimedop(int semid, struct sembuf *sops, size_t nsops, const struct timespec *timeout);

/*
 * Semaphore control operations.
 * Return value depends on cmd.
 */
int semctl(int semid, int semnum, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SEM_H */
