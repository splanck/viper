/*
 * ViperOS libc - spawn.c
 * POSIX spawn implementation (stubs)
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
int posix_spawnattr_init(posix_spawnattr_t *attr)
{
    if (!attr)
    {
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
int posix_spawnattr_destroy(posix_spawnattr_t *attr)
{
    if (!attr)
    {
        return EINVAL;
    }
    /* Nothing to free */
    return 0;
}

/*
 * posix_spawnattr_getflags - Get spawn attribute flags
 */
int posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags)
{
    if (!attr || !flags)
    {
        return EINVAL;
    }
    *flags = attr->flags;
    return 0;
}

/*
 * posix_spawnattr_setflags - Set spawn attribute flags
 */
int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags)
{
    if (!attr)
    {
        return EINVAL;
    }
    attr->flags = flags;
    return 0;
}

/*
 * posix_spawnattr_getpgroup - Get process group
 */
int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr, pid_t *pgroup)
{
    if (!attr || !pgroup)
    {
        return EINVAL;
    }
    *pgroup = attr->pgroup;
    return 0;
}

/*
 * posix_spawnattr_setpgroup - Set process group
 */
int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup)
{
    if (!attr)
    {
        return EINVAL;
    }
    attr->pgroup = pgroup;
    return 0;
}

/*
 * posix_spawnattr_getsigdefault - Get default signals
 */
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr, sigset_t *sigdefault)
{
    if (!attr || !sigdefault)
    {
        return EINVAL;
    }
    *sigdefault = attr->sigdefault;
    return 0;
}

/*
 * posix_spawnattr_setsigdefault - Set default signals
 */
int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr, const sigset_t *sigdefault)
{
    if (!attr || !sigdefault)
    {
        return EINVAL;
    }
    attr->sigdefault = *sigdefault;
    return 0;
}

/*
 * posix_spawnattr_getsigmask - Get signal mask
 */
int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr, sigset_t *sigmask)
{
    if (!attr || !sigmask)
    {
        return EINVAL;
    }
    *sigmask = attr->sigmask;
    return 0;
}

/*
 * posix_spawnattr_setsigmask - Set signal mask
 */
int posix_spawnattr_setsigmask(posix_spawnattr_t *attr, const sigset_t *sigmask)
{
    if (!attr || !sigmask)
    {
        return EINVAL;
    }
    attr->sigmask = *sigmask;
    return 0;
}

/*
 * posix_spawnattr_getschedpolicy - Get scheduling policy
 */
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *attr, int *policy)
{
    if (!attr || !policy)
    {
        return EINVAL;
    }
    *policy = attr->schedpolicy;
    return 0;
}

/*
 * posix_spawnattr_setschedpolicy - Set scheduling policy
 */
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy)
{
    if (!attr)
    {
        return EINVAL;
    }
    attr->schedpolicy = policy;
    return 0;
}

/*
 * posix_spawnattr_getschedparam - Get scheduling parameters
 */
int posix_spawnattr_getschedparam(const posix_spawnattr_t *attr, struct sched_param *param)
{
    if (!attr || !param)
    {
        return EINVAL;
    }
    *param = attr->schedparam;
    return 0;
}

/*
 * posix_spawnattr_setschedparam - Set scheduling parameters
 */
int posix_spawnattr_setschedparam(posix_spawnattr_t *attr, const struct sched_param *param)
{
    if (!attr || !param)
    {
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
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions)
{
    if (!file_actions)
    {
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
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions)
{
    if (!file_actions)
    {
        return EINVAL;
    }

    /* Free any open action paths */
    for (int i = 0; i < file_actions->used; i++)
    {
        if (file_actions->actions[i].type == SPAWN_ACTION_OPEN)
        {
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
static int add_action(posix_spawn_file_actions_t *file_actions)
{
    if (file_actions->used >= file_actions->allocated)
    {
        int new_size = file_actions->allocated ? file_actions->allocated * 2 : 8;
        struct spawn_action *new_actions = (struct spawn_action *)realloc(
            file_actions->actions, new_size * sizeof(struct spawn_action));
        if (!new_actions)
        {
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
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions, int fd)
{
    if (!file_actions || fd < 0)
    {
        return EINVAL;
    }

    int err = add_action(file_actions);
    if (err)
    {
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
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions, int fd, int newfd)
{
    if (!file_actions || fd < 0 || newfd < 0)
    {
        return EINVAL;
    }

    int err = add_action(file_actions);
    if (err)
    {
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
    posix_spawn_file_actions_t *file_actions, int fd, const char *path, int oflag, mode_t mode)
{
    if (!file_actions || fd < 0 || !path)
    {
        return EINVAL;
    }

    int err = add_action(file_actions);
    if (err)
    {
        return err;
    }

    char *path_copy = strdup(path);
    if (!path_copy)
    {
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
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *file_actions, const char *path)
{
    (void)file_actions;
    (void)path;
    /* Not implemented in ViperOS */
    return ENOSYS;
}

/*
 * posix_spawn_file_actions_addfchdir_np - Add fchdir action (extension)
 */
int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *file_actions, int fd)
{
    (void)file_actions;
    (void)fd;
    /* Not implemented in ViperOS */
    return ENOSYS;
}

/* ============================================================
 * Spawn functions
 * ============================================================ */

/*
 * posix_spawn - Spawn a process
 *
 * ViperOS stub - returns ENOSYS since exec() is not fully implemented.
 */
int posix_spawn(pid_t *pid,
                const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[],
                char *const envp[])
{
    (void)pid;
    (void)path;
    (void)file_actions;
    (void)attrp;
    (void)argv;
    (void)envp;

    /* ViperOS doesn't yet support full process spawning */
    return ENOSYS;
}

/*
 * posix_spawnp - Spawn a process using PATH search
 */
int posix_spawnp(pid_t *pid,
                 const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[],
                 char *const envp[])
{
    (void)pid;
    (void)file;
    (void)file_actions;
    (void)attrp;
    (void)argv;
    (void)envp;

    /* ViperOS doesn't yet support full process spawning */
    return ENOSYS;
}
