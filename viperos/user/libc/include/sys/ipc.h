/*
 * ViperOS libc - sys/ipc.h
 * Inter-process communication base definitions
 */

#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * IPC key type
     */
    typedef int key_t;

/*
 * Mode bits for get operations
 */
#define IPC_CREAT 01000  /* Create key if key does not exist */
#define IPC_EXCL 02000   /* Fail if key exists */
#define IPC_NOWAIT 04000 /* Return error on wait */

/*
 * Control commands for IPC operations
 */
#define IPC_RMID 0 /* Remove resource */
#define IPC_SET 1  /* Set ipc_perm options */
#define IPC_STAT 2 /* Get ipc_perm options */
#define IPC_INFO 3 /* Get system-wide info */

/*
 * Special key values
 */
#define IPC_PRIVATE ((key_t)0) /* Private key */

    /*
     * Permission structure for IPC operations
     */
    struct ipc_perm
    {
        key_t __key;          /* Key supplied to xxxget() */
        uid_t uid;            /* Effective UID of owner */
        gid_t gid;            /* Effective GID of owner */
        uid_t cuid;           /* Effective UID of creator */
        gid_t cgid;           /* Effective GID of creator */
        mode_t mode;          /* Permissions */
        unsigned short __seq; /* Sequence number */
    };

    /*
     * ftok - Generate IPC key from pathname and project ID
     *
     * Generates a key suitable for use with msgget(), semget(), or shmget().
     *
     * @pathname: Path to an existing file
     * @proj_id: Project identifier (only lower 8 bits used)
     *
     * Returns the generated key, or -1 on error.
     */
    key_t ftok(const char *pathname, int proj_id);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IPC_H */
