/*
 * ViperOS C Library - POSIX semaphore implementation
 * Basic implementation for single-process environment
 */

#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* Named semaphore storage */
#define MAX_NAMED_SEMS 16
#define MAX_SEM_NAME 32

struct named_sem {
    int in_use;
    char name[MAX_SEM_NAME];
    sem_t sem;
    int refcount;
};

static struct named_sem named_sems[MAX_NAMED_SEMS];
static int named_sems_initialized = 0;

static void init_named_sems(void) {
    if (!named_sems_initialized) {
        memset(named_sems, 0, sizeof(named_sems));
        named_sems_initialized = 1;
    }
}

static struct named_sem *find_named_sem(const char *name) {
    for (int i = 0; i < MAX_NAMED_SEMS; i++) {
        if (named_sems[i].in_use &&
            strcmp(named_sems[i].name, name) == 0) {
            return &named_sems[i];
        }
    }
    return NULL;
}

static struct named_sem *alloc_named_sem(void) {
    for (int i = 0; i < MAX_NAMED_SEMS; i++) {
        if (!named_sems[i].in_use) {
            return &named_sems[i];
        }
    }
    return NULL;
}

int sem_init(sem_t *sem, int pshared, unsigned int value) {
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }

    sem->value = value;
    sem->pshared = pshared;

    return 0;
}

int sem_destroy(sem_t *sem) {
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* In a real implementation, we'd check for waiters */
    sem->value = 0;
    return 0;
}

sem_t *sem_open(const char *name, int oflag, ...) {
    init_named_sems();

    if (name == NULL || name[0] != '/') {
        errno = EINVAL;
        return SEM_FAILED;
    }

    /* Skip leading slash for storage */
    const char *short_name = name + 1;
    if (strlen(short_name) >= MAX_SEM_NAME) {
        errno = ENAMETOOLONG;
        return SEM_FAILED;
    }

    struct named_sem *ns = find_named_sem(short_name);

    if (ns != NULL) {
        /* Semaphore exists */
        if ((oflag & O_CREAT) && (oflag & O_EXCL)) {
            errno = EEXIST;
            return SEM_FAILED;
        }
        ns->refcount++;
        return &ns->sem;
    }

    /* Semaphore doesn't exist */
    if (!(oflag & O_CREAT)) {
        errno = ENOENT;
        return SEM_FAILED;
    }

    /* Create new named semaphore */
    ns = alloc_named_sem();
    if (ns == NULL) {
        errno = EMFILE;
        return SEM_FAILED;
    }

    /* Parse optional arguments: mode and value */
    va_list ap;
    va_start(ap, oflag);
    mode_t mode = va_arg(ap, mode_t);
    unsigned int value = va_arg(ap, unsigned int);
    va_end(ap);

    (void)mode;  /* Mode not used in this implementation */

    if (value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return SEM_FAILED;
    }

    ns->in_use = 1;
    strncpy(ns->name, short_name, MAX_SEM_NAME - 1);
    ns->name[MAX_SEM_NAME - 1] = '\0';
    ns->sem.value = value;
    ns->sem.pshared = 1;  /* Named semaphores are process-shared */
    ns->refcount = 1;

    return &ns->sem;
}

int sem_close(sem_t *sem) {
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Find which named semaphore this is */
    for (int i = 0; i < MAX_NAMED_SEMS; i++) {
        if (named_sems[i].in_use && &named_sems[i].sem == sem) {
            named_sems[i].refcount--;
            if (named_sems[i].refcount <= 0 && !named_sems[i].in_use) {
                /* Marked for removal and no more references */
                memset(&named_sems[i], 0, sizeof(struct named_sem));
            }
            return 0;
        }
    }

    /* Not a named semaphore - this is an error per POSIX */
    errno = EINVAL;
    return -1;
}

int sem_unlink(const char *name) {
    init_named_sems();

    if (name == NULL || name[0] != '/') {
        errno = EINVAL;
        return -1;
    }

    const char *short_name = name + 1;
    struct named_sem *ns = find_named_sem(short_name);

    if (ns == NULL) {
        errno = ENOENT;
        return -1;
    }

    /* Mark for removal */
    ns->in_use = 0;
    ns->name[0] = '\0';

    if (ns->refcount <= 0) {
        /* No open references, free immediately */
        memset(ns, 0, sizeof(struct named_sem));
    }

    return 0;
}

int sem_wait(sem_t *sem) {
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* In a real implementation, this would block if value is 0 */
    if (sem->value == 0) {
        /* Single-process: can't block, return EAGAIN */
        errno = EAGAIN;
        return -1;
    }

    sem->value--;
    return 0;
}

int sem_trywait(sem_t *sem) {
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (sem->value == 0) {
        errno = EAGAIN;
        return -1;
    }

    sem->value--;
    return 0;
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout) {
    (void)abs_timeout;  /* Timeout not implemented */

    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (sem->value == 0) {
        /* Would block - in real impl, would wait until timeout */
        errno = ETIMEDOUT;
        return -1;
    }

    sem->value--;
    return 0;
}

int sem_post(sem_t *sem) {
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (sem->value >= SEM_VALUE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    sem->value++;
    /* In a real implementation, would wake up a waiter here */
    return 0;
}

int sem_getvalue(sem_t *sem, int *sval) {
    if (sem == NULL || sval == NULL) {
        errno = EINVAL;
        return -1;
    }

    *sval = (int)sem->value;
    return 0;
}
