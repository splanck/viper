//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/spawn.c
// Purpose: POSIX spawn functions for ViperDOS libc.
// Key invariants: posix_spawn calls SYS_TASK_SPAWN kernel syscall.
// Ownership/Lifetime: Library; file actions dynamically allocated.
// Links: user/libc/include/spawn.h
//
//===----------------------------------------------------------------------===//

/**
 * @file spawn.c
 * @brief POSIX spawn functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX spawn attribute and file action functions:
 *
 * Spawn Attributes:
 * - posix_spawnattr_init/destroy: Initialize/destroy attributes
 * - posix_spawnattr_get/setflags: Get/set spawn flags
 * - posix_spawnattr_get/setpgroup: Get/set process group
 * - posix_spawnattr_get/setsigdefault: Get/set default signals
 * - posix_spawnattr_get/setsigmask: Get/set signal mask
 * - posix_spawnattr_get/setschedpolicy: Get/set scheduling policy
 * - posix_spawnattr_get/setschedparam: Get/set scheduling parameters
 *
 * File Actions:
 * - posix_spawn_file_actions_init/destroy: Initialize/destroy actions
 * - posix_spawn_file_actions_addclose: Add close action
 * - posix_spawn_file_actions_adddup2: Add dup2 action
 * - posix_spawn_file_actions_addopen: Add open action
 *
 * The spawn functions (posix_spawn, posix_spawnp) call the kernel's
 * SYS_TASK_SPAWN syscall to create a new process from an ELF binary.
 */

#include "../include/spawn.h"
#include "../include/errno.h"
#include "../include/fcntl.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* Action types */
#define SPAWN_ACTION_CLOSE 1
#define SPAWN_ACTION_DUP2 2
#define SPAWN_ACTION_OPEN 3

/* ============================================================
 * Spawn attributes functions
 * ============================================================ */

/*
 * posix_spawnattr_init - Initialize spawn attributes
 */
int posix_spawnattr_init(posix_spawnattr_t *attr) {
    if (!attr) {
        return EINVAL;
    }

    attr->flags = 0;
    attr->pgroup = 0;
    sigemptyset(&attr->sigdefault);
    sigemptyset(&attr->sigmask);
    attr->schedpolicy = SCHED_OTHER;
    attr->schedparam.sched_priority = 0;

    return 0;
}

/*
 * posix_spawnattr_destroy - Destroy spawn attributes
 */
int posix_spawnattr_destroy(posix_spawnattr_t *attr) {
    if (!attr) {
        return EINVAL;
    }
    /* Nothing to free */
    return 0;
}

/*
 * posix_spawnattr_getflags - Get spawn attribute flags
 */
int posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags) {
    if (!attr || !flags) {
        return EINVAL;
    }
    *flags = attr->flags;
    return 0;
}

/*
 * posix_spawnattr_setflags - Set spawn attribute flags
 */
int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags) {
    if (!attr) {
        return EINVAL;
    }
    attr->flags = flags;
    return 0;
}

/*
 * posix_spawnattr_getpgroup - Get process group
 */
int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr, pid_t *pgroup) {
    if (!attr || !pgroup) {
        return EINVAL;
    }
    *pgroup = attr->pgroup;
    return 0;
}

/*
 * posix_spawnattr_setpgroup - Set process group
 */
int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup) {
    if (!attr) {
        return EINVAL;
    }
    attr->pgroup = pgroup;
    return 0;
}

/*
 * posix_spawnattr_getsigdefault - Get default signals
 */
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr, sigset_t *sigdefault) {
    if (!attr || !sigdefault) {
        return EINVAL;
    }
    *sigdefault = attr->sigdefault;
    return 0;
}

/*
 * posix_spawnattr_setsigdefault - Set default signals
 */
int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr, const sigset_t *sigdefault) {
    if (!attr || !sigdefault) {
        return EINVAL;
    }
    attr->sigdefault = *sigdefault;
    return 0;
}

/*
 * posix_spawnattr_getsigmask - Get signal mask
 */
int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr, sigset_t *sigmask) {
    if (!attr || !sigmask) {
        return EINVAL;
    }
    *sigmask = attr->sigmask;
    return 0;
}

/*
 * posix_spawnattr_setsigmask - Set signal mask
 */
int posix_spawnattr_setsigmask(posix_spawnattr_t *attr, const sigset_t *sigmask) {
    if (!attr || !sigmask) {
        return EINVAL;
    }
    attr->sigmask = *sigmask;
    return 0;
}

/*
 * posix_spawnattr_getschedpolicy - Get scheduling policy
 */
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *attr, int *policy) {
    if (!attr || !policy) {
        return EINVAL;
    }
    *policy = attr->schedpolicy;
    return 0;
}

/*
 * posix_spawnattr_setschedpolicy - Set scheduling policy
 */
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy) {
    if (!attr) {
        return EINVAL;
    }
    attr->schedpolicy = policy;
    return 0;
}

/*
 * posix_spawnattr_getschedparam - Get scheduling parameters
 */
int posix_spawnattr_getschedparam(const posix_spawnattr_t *attr, struct sched_param *param) {
    if (!attr || !param) {
        return EINVAL;
    }
    *param = attr->schedparam;
    return 0;
}

/*
 * posix_spawnattr_setschedparam - Set scheduling parameters
 */
int posix_spawnattr_setschedparam(posix_spawnattr_t *attr, const struct sched_param *param) {
    if (!attr || !param) {
        return EINVAL;
    }
    attr->schedparam = *param;
    return 0;
}

/* ============================================================
 * Spawn file actions functions
 * ============================================================ */

/*
 * posix_spawn_file_actions_init - Initialize file actions
 */
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions) {
    if (!file_actions) {
        return EINVAL;
    }

    file_actions->allocated = 0;
    file_actions->used = 0;
    file_actions->actions = NULL;

    return 0;
}

/*
 * posix_spawn_file_actions_destroy - Destroy file actions
 */
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions) {
    if (!file_actions) {
        return EINVAL;
    }

    /* Free any open action paths */
    for (int i = 0; i < file_actions->used; i++) {
        if (file_actions->actions[i].type == SPAWN_ACTION_OPEN) {
            free(file_actions->actions[i].open_action.path);
        }
    }

    free(file_actions->actions);
    file_actions->actions = NULL;
    file_actions->allocated = 0;
    file_actions->used = 0;

    return 0;
}

/*
 * add_action - Add an action to the list
 */
static int add_action(posix_spawn_file_actions_t *file_actions) {
    if (file_actions->used >= file_actions->allocated) {
        int new_size = file_actions->allocated ? file_actions->allocated * 2 : 8;
        struct spawn_action *new_actions = (struct spawn_action *)realloc(
            file_actions->actions, new_size * sizeof(struct spawn_action));
        if (!new_actions) {
            return ENOMEM;
        }
        file_actions->actions = new_actions;
        file_actions->allocated = new_size;
    }
    return 0;
}

/*
 * posix_spawn_file_actions_addclose - Add close action
 */
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions, int fd) {
    if (!file_actions || fd < 0) {
        return EINVAL;
    }

    int err = add_action(file_actions);
    if (err) {
        return err;
    }

    file_actions->actions[file_actions->used].type = SPAWN_ACTION_CLOSE;
    file_actions->actions[file_actions->used].close_action.fd = fd;
    file_actions->used++;

    return 0;
}

/*
 * posix_spawn_file_actions_adddup2 - Add dup2 action
 */
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions, int fd, int newfd) {
    if (!file_actions || fd < 0 || newfd < 0) {
        return EINVAL;
    }

    int err = add_action(file_actions);
    if (err) {
        return err;
    }

    file_actions->actions[file_actions->used].type = SPAWN_ACTION_DUP2;
    file_actions->actions[file_actions->used].dup2_action.fd = fd;
    file_actions->actions[file_actions->used].dup2_action.newfd = newfd;
    file_actions->used++;

    return 0;
}

/*
 * posix_spawn_file_actions_addopen - Add open action
 */
int posix_spawn_file_actions_addopen(
    posix_spawn_file_actions_t *file_actions, int fd, const char *path, int oflag, mode_t mode) {
    if (!file_actions || fd < 0 || !path) {
        return EINVAL;
    }

    int err = add_action(file_actions);
    if (err) {
        return err;
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return ENOMEM;
    }

    file_actions->actions[file_actions->used].type = SPAWN_ACTION_OPEN;
    file_actions->actions[file_actions->used].open_action.fd = fd;
    file_actions->actions[file_actions->used].open_action.path = path_copy;
    file_actions->actions[file_actions->used].open_action.oflag = oflag;
    file_actions->actions[file_actions->used].open_action.mode = mode;
    file_actions->used++;

    return 0;
}

/*
 * posix_spawn_file_actions_addchdir_np - Add chdir action (extension)
 */
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *file_actions,
                                         const char *path) {
    (void)file_actions;
    (void)path;
    /* Not implemented in ViperDOS */
    return ENOSYS;
}

/*
 * posix_spawn_file_actions_addfchdir_np - Add fchdir action (extension)
 */
int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *file_actions, int fd) {
    (void)file_actions;
    (void)fd;
    /* Not implemented in ViperDOS */
    return ENOSYS;
}

/* ============================================================
 * Spawn functions
 * ============================================================ */

/* Syscall wrapper */
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Kernel syscall number for process spawning */
#define SYS_TASK_SPAWN 0x03

/*
 * posix_spawn - Spawn a process
 *
 * Calls the kernel's SYS_TASK_SPAWN syscall to create a new process
 * from the ELF binary at the given path. argv[] is flattened into a
 * single space-separated args string for the kernel ABI.
 *
 * Note: file_actions and attrp are accepted but not applied (the kernel
 * handles file descriptor inheritance and scheduling internally).
 */
int posix_spawn(pid_t *pid,
                const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[],
                char *const envp[]) {
    (void)file_actions;
    (void)attrp;
    (void)envp;

    if (!path) {
        return EINVAL;
    }

    /* Build a single space-separated args string from argv[] */
    char args_buf[256];
    args_buf[0] = '\0';
    if (argv) {
        int pos = 0;
        for (int i = 0; argv[i] && pos < 255; i++) {
            if (i > 0 && pos < 255)
                args_buf[pos++] = ' ';
            for (int j = 0; argv[i][j] && pos < 255; j++)
                args_buf[pos++] = argv[i][j];
        }
        args_buf[pos] = '\0';
    }

    long result = __syscall3(SYS_TASK_SPAWN,
                             (long)path,
                             (long)path,
                             (long)(args_buf[0] ? args_buf : (char *)0));

    if (result < 0) {
        /* Kernel returned a negative error code */
        return (int)(-result);
    }

    /* result contains the viper_id of the new process */
    if (pid) {
        *pid = (pid_t)result;
    }

    return 0;
}

/*
 * posix_spawnp - Spawn a process using PATH search
 *
 * ViperDOS uses absolute paths, so this delegates to posix_spawn.
 */
int posix_spawnp(pid_t *pid,
                 const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[],
                 char *const envp[]) {
    return posix_spawn(pid, file, file_actions, attrp, argv, envp);
}
