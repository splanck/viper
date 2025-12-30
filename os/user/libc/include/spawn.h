/*
 * ViperOS libc - spawn.h
 * POSIX spawn interface
 */

#ifndef _SPAWN_H
#define _SPAWN_H

#include "sys/types.h"
#include "signal.h"
#include "sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn attribute flags */
#define POSIX_SPAWN_RESETIDS        0x0001  /* Reset effective IDs */
#define POSIX_SPAWN_SETPGROUP       0x0002  /* Set process group */
#define POSIX_SPAWN_SETSIGDEF       0x0004  /* Set signal defaults */
#define POSIX_SPAWN_SETSIGMASK      0x0008  /* Set signal mask */
#define POSIX_SPAWN_SETSCHEDPARAM   0x0010  /* Set scheduling parameters */
#define POSIX_SPAWN_SETSCHEDULER    0x0020  /* Set scheduling policy */
#define POSIX_SPAWN_USEVFORK        0x0040  /* Use vfork (GNU extension) */
#define POSIX_SPAWN_SETSID          0x0080  /* Create new session (GNU) */

/* Opaque spawn attribute type */
typedef struct {
    short flags;
    pid_t pgroup;
    sigset_t sigdefault;
    sigset_t sigmask;
    int schedpolicy;
    struct sched_param schedparam;
} posix_spawnattr_t;

/* Opaque file actions type */
typedef struct posix_spawn_file_actions {
    int allocated;
    int used;
    struct spawn_action *actions;
} posix_spawn_file_actions_t;

/* Action types (internal) */
struct spawn_action {
    int type;
    union {
        struct {
            int fd;
        } close_action;
        struct {
            int fd;
            int newfd;
        } dup2_action;
        struct {
            int fd;
            char *path;
            int oflag;
            mode_t mode;
        } open_action;
    };
};

/* ============================================================
 * Spawn functions
 * ============================================================ */

/*
 * posix_spawn - Spawn a process
 *
 * Creates a new process running the program 'path'.
 * If pid is non-NULL, stores the child PID.
 * file_actions specifies file descriptor operations.
 * attrp specifies process attributes.
 * argv and envp are passed to the new process.
 *
 * Returns 0 on success, or error number on failure.
 */
int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[]);

/*
 * posix_spawnp - Spawn a process using PATH search
 *
 * Like posix_spawn, but searches PATH for 'file'.
 */
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[]);

/* ============================================================
 * Spawn attributes functions
 * ============================================================ */

/*
 * posix_spawnattr_init - Initialize spawn attributes
 */
int posix_spawnattr_init(posix_spawnattr_t *attr);

/*
 * posix_spawnattr_destroy - Destroy spawn attributes
 */
int posix_spawnattr_destroy(posix_spawnattr_t *attr);

/*
 * posix_spawnattr_getflags - Get spawn attribute flags
 */
int posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags);

/*
 * posix_spawnattr_setflags - Set spawn attribute flags
 */
int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags);

/*
 * posix_spawnattr_getpgroup - Get process group
 */
int posix_spawnattr_getpgroup(const posix_spawnattr_t *attr, pid_t *pgroup);

/*
 * posix_spawnattr_setpgroup - Set process group
 */
int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgroup);

/*
 * posix_spawnattr_getsigdefault - Get default signals
 */
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attr,
                                   sigset_t *sigdefault);

/*
 * posix_spawnattr_setsigdefault - Set default signals
 */
int posix_spawnattr_setsigdefault(posix_spawnattr_t *attr,
                                   const sigset_t *sigdefault);

/*
 * posix_spawnattr_getsigmask - Get signal mask
 */
int posix_spawnattr_getsigmask(const posix_spawnattr_t *attr,
                                sigset_t *sigmask);

/*
 * posix_spawnattr_setsigmask - Set signal mask
 */
int posix_spawnattr_setsigmask(posix_spawnattr_t *attr,
                                const sigset_t *sigmask);

/*
 * posix_spawnattr_getschedpolicy - Get scheduling policy
 */
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *attr, int *policy);

/*
 * posix_spawnattr_setschedpolicy - Set scheduling policy
 */
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy);

/*
 * posix_spawnattr_getschedparam - Get scheduling parameters
 */
int posix_spawnattr_getschedparam(const posix_spawnattr_t *attr,
                                   struct sched_param *param);

/*
 * posix_spawnattr_setschedparam - Set scheduling parameters
 */
int posix_spawnattr_setschedparam(posix_spawnattr_t *attr,
                                   const struct sched_param *param);

/* ============================================================
 * Spawn file actions functions
 * ============================================================ */

/*
 * posix_spawn_file_actions_init - Initialize file actions
 */
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *file_actions);

/*
 * posix_spawn_file_actions_destroy - Destroy file actions
 */
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *file_actions);

/*
 * posix_spawn_file_actions_addclose - Add close action
 */
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *file_actions,
                                       int fd);

/*
 * posix_spawn_file_actions_adddup2 - Add dup2 action
 */
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *file_actions,
                                      int fd, int newfd);

/*
 * posix_spawn_file_actions_addopen - Add open action
 */
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *file_actions,
                                      int fd, const char *path,
                                      int oflag, mode_t mode);

/*
 * posix_spawn_file_actions_addchdir_np - Add chdir action (extension)
 */
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *file_actions,
                                          const char *path);

/*
 * posix_spawn_file_actions_addfchdir_np - Add fchdir action (extension)
 */
int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *file_actions,
                                           int fd);

#ifdef __cplusplus
}
#endif

#endif /* _SPAWN_H */
