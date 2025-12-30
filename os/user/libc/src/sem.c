/*
 * ViperOS C Library - System V semaphore implementation
 * Stub implementation for single-process environment
 */

#include <sys/sem.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

/* Maximum number of semaphore sets */
#define MAX_SEM_SETS 16
#define MAX_SEMS_PER_SET 32

/* Internal semaphore set structure */
struct sem_set {
    int in_use;
    key_t key;
    int nsems;
    struct semid_ds ds;
    unsigned short values[MAX_SEMS_PER_SET];
};

/* Global semaphore set table */
static struct sem_set sem_sets[MAX_SEM_SETS];
static int sem_initialized = 0;

static void init_semaphores(void) {
    if (!sem_initialized) {
        memset(sem_sets, 0, sizeof(sem_sets));
        sem_initialized = 1;
    }
}

static int find_by_key(key_t key) {
    for (int i = 0; i < MAX_SEM_SETS; i++) {
        if (sem_sets[i].in_use && sem_sets[i].key == key) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_SEM_SETS; i++) {
        if (!sem_sets[i].in_use) {
            return i;
        }
    }
    return -1;
}

int semget(key_t key, int nsems, int semflg) {
    init_semaphores();

    if (nsems < 0 || nsems > MAX_SEMS_PER_SET) {
        errno = EINVAL;
        return -1;
    }

    /* Check for IPC_PRIVATE or existing key */
    if (key != IPC_PRIVATE) {
        int existing = find_by_key(key);
        if (existing >= 0) {
            if (semflg & IPC_CREAT && semflg & IPC_EXCL) {
                errno = EEXIST;
                return -1;
            }
            /* Return existing set */
            if (nsems > 0 && nsems > sem_sets[existing].nsems) {
                errno = EINVAL;
                return -1;
            }
            return existing;
        }

        if (!(semflg & IPC_CREAT)) {
            errno = ENOENT;
            return -1;
        }
    }

    /* Create new semaphore set */
    if (nsems == 0) {
        errno = EINVAL;
        return -1;
    }

    int slot = find_free_slot();
    if (slot < 0) {
        errno = ENOSPC;
        return -1;
    }

    struct sem_set *ss = &sem_sets[slot];
    ss->in_use = 1;
    ss->key = key;
    ss->nsems = nsems;
    ss->ds.sem_perm.mode = semflg & 0777;
    ss->ds.sem_perm.uid = 0;  /* Would be getuid() */
    ss->ds.sem_perm.gid = 0;  /* Would be getgid() */
    ss->ds.sem_otime = 0;
    ss->ds.sem_ctime = 0;     /* Would be time(NULL) */
    ss->ds.sem_nsems = nsems;

    /* Initialize all semaphore values to 0 */
    for (int i = 0; i < nsems; i++) {
        ss->values[i] = 0;
    }

    return slot;
}

int semop(int semid, struct sembuf *sops, size_t nsops) {
    return semtimedop(semid, sops, nsops, NULL);
}

int semtimedop(int semid, struct sembuf *sops, size_t nsops,
               const struct timespec *timeout) {
    (void)timeout;  /* Timeout not implemented */

    init_semaphores();

    if (semid < 0 || semid >= MAX_SEM_SETS || !sem_sets[semid].in_use) {
        errno = EINVAL;
        return -1;
    }

    if (sops == NULL || nsops == 0 || nsops > SEMOPM) {
        errno = EINVAL;
        return -1;
    }

    struct sem_set *ss = &sem_sets[semid];

    /* Validate all operations first */
    for (size_t i = 0; i < nsops; i++) {
        if (sops[i].sem_num >= (unsigned short)ss->nsems) {
            errno = EFBIG;
            return -1;
        }
    }

    /* Perform all operations atomically */
    /* Note: In a real implementation, this would need to handle blocking
     * when semval would go negative. For now, we do non-blocking only. */
    for (size_t i = 0; i < nsops; i++) {
        unsigned short sem_num = sops[i].sem_num;
        short sem_op = sops[i].sem_op;
        short sem_flg = sops[i].sem_flg;

        if (sem_op > 0) {
            /* Add to semaphore value */
            int newval = ss->values[sem_num] + sem_op;
            if (newval > SEMVMX) {
                errno = ERANGE;
                return -1;
            }
            ss->values[sem_num] = newval;
        } else if (sem_op < 0) {
            /* Subtract from semaphore value */
            int newval = ss->values[sem_num] + sem_op;
            if (newval < 0) {
                if (sem_flg & IPC_NOWAIT) {
                    errno = EAGAIN;
                    return -1;
                }
                /* Would block - not implemented in single-process env */
                errno = EAGAIN;
                return -1;
            }
            ss->values[sem_num] = newval;
        } else {
            /* Wait for zero */
            if (ss->values[sem_num] != 0) {
                if (sem_flg & IPC_NOWAIT) {
                    errno = EAGAIN;
                    return -1;
                }
                /* Would block - not implemented */
                errno = EAGAIN;
                return -1;
            }
        }
    }

    /* Update otime - would use time(NULL) */
    ss->ds.sem_otime = 0;

    return 0;
}

int semctl(int semid, int semnum, int cmd, ...) {
    init_semaphores();

    if (semid < 0 || semid >= MAX_SEM_SETS) {
        errno = EINVAL;
        return -1;
    }

    /* IPC_RMID doesn't need a valid semid in some cases */
    if (cmd != IPC_RMID && cmd != IPC_INFO && cmd != SEM_INFO) {
        if (!sem_sets[semid].in_use) {
            errno = EINVAL;
            return -1;
        }
    }

    struct sem_set *ss = &sem_sets[semid];
    va_list ap;
    union semun arg;

    switch (cmd) {
    case IPC_RMID:
        if (!sem_sets[semid].in_use) {
            errno = EINVAL;
            return -1;
        }
        ss->in_use = 0;
        return 0;

    case IPC_STAT:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
        if (arg.buf == NULL) {
            errno = EFAULT;
            return -1;
        }
        memcpy(arg.buf, &ss->ds, sizeof(struct semid_ds));
        return 0;

    case IPC_SET:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
        if (arg.buf == NULL) {
            errno = EFAULT;
            return -1;
        }
        ss->ds.sem_perm.uid = arg.buf->sem_perm.uid;
        ss->ds.sem_perm.gid = arg.buf->sem_perm.gid;
        ss->ds.sem_perm.mode = arg.buf->sem_perm.mode & 0777;
        /* ss->ds.sem_ctime = time(NULL); */
        return 0;

    case GETVAL:
        if (semnum < 0 || semnum >= ss->nsems) {
            errno = EINVAL;
            return -1;
        }
        return ss->values[semnum];

    case SETVAL:
        if (semnum < 0 || semnum >= ss->nsems) {
            errno = EINVAL;
            return -1;
        }
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
        if (arg.val < 0 || arg.val > SEMVMX) {
            errno = ERANGE;
            return -1;
        }
        ss->values[semnum] = arg.val;
        /* ss->ds.sem_ctime = time(NULL); */
        return 0;

    case GETALL:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
        if (arg.array == NULL) {
            errno = EFAULT;
            return -1;
        }
        for (int i = 0; i < ss->nsems; i++) {
            arg.array[i] = ss->values[i];
        }
        return 0;

    case SETALL:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
        if (arg.array == NULL) {
            errno = EFAULT;
            return -1;
        }
        for (int i = 0; i < ss->nsems; i++) {
            ss->values[i] = arg.array[i];
        }
        /* ss->ds.sem_ctime = time(NULL); */
        return 0;

    case GETPID:
        /* Not tracked in this implementation */
        return 0;

    case GETNCNT:
    case GETZCNT:
        /* No blocking in single-process env */
        return 0;

    case IPC_INFO:
    case SEM_INFO:
        va_start(ap, cmd);
        arg = va_arg(ap, union semun);
        va_end(ap);
        if (arg.__buf == NULL) {
            errno = EFAULT;
            return -1;
        }
        arg.__buf->semmap = 0;
        arg.__buf->semmni = SEMMNI;
        arg.__buf->semmns = SEMMNS;
        arg.__buf->semmnu = SEMMNU;
        arg.__buf->semmsl = SEMMSL;
        arg.__buf->semopm = SEMOPM;
        arg.__buf->semume = SEMUME;
        arg.__buf->semusz = sizeof(struct sem);
        arg.__buf->semvmx = SEMVMX;
        arg.__buf->semaem = SEMAEM;
        return MAX_SEM_SETS - 1;  /* Highest used index */

    default:
        errno = EINVAL;
        return -1;
    }
}
