/*
 * ViperOS libc - sys/shm.h
 * Shared memory definitions
 */

#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include "ipc.h"
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Permission flags for shmat()
 */
#define SHM_RDONLY 010000 /* Attach read-only */
#define SHM_RND 020000    /* Round attach address to SHMLBA */
#define SHM_REMAP 040000  /* Take-over region on attach */
#define SHM_EXEC 0100000  /* Allow execution */

/*
 * Command definitions for shmctl()
 */
#define SHM_LOCK 11   /* Lock segment (prevent swapping) */
#define SHM_UNLOCK 12 /* Unlock segment */
#define SHM_STAT 13   /* Get info for specific segment */
#define SHM_INFO 14   /* Get system-wide info */

/*
 * Shared memory lower boundary alignment
 */
#define SHMLBA 4096 /* Page size alignment */

    /*
     * Shared memory information structure
     */
    struct shmid_ds
    {
        struct ipc_perm shm_perm;  /* Operation permission structure */
        size_t shm_segsz;          /* Segment size in bytes */
        time_t shm_atime;          /* Last attach time */
        time_t shm_dtime;          /* Last detach time */
        time_t shm_ctime;          /* Last change time */
        pid_t shm_cpid;            /* Creator's process ID */
        pid_t shm_lpid;            /* Last operator's process ID */
        unsigned short shm_nattch; /* Number of current attaches */
    };

    /*
     * System-wide shared memory info (for SHM_INFO)
     */
    struct shminfo
    {
        unsigned long shmmax; /* Maximum segment size */
        unsigned long shmmin; /* Minimum segment size */
        unsigned long shmmni; /* Maximum number of segments */
        unsigned long shmseg; /* Max segments per process */
        unsigned long shmall; /* Max total shared memory */
    };

    /*
     * shmget - Get or create shared memory segment
     *
     * @key: Key for segment (or IPC_PRIVATE)
     * @size: Size of segment in bytes
     * @shmflg: Flags (IPC_CREAT, IPC_EXCL, permissions)
     *
     * Returns shared memory identifier, or -1 on error.
     */
    int shmget(key_t key, size_t size, int shmflg);

    /*
     * shmat - Attach shared memory segment
     *
     * @shmid: Shared memory identifier from shmget()
     * @shmaddr: Address to attach at (NULL for automatic)
     * @shmflg: Flags (SHM_RDONLY, SHM_RND, etc.)
     *
     * Returns address of attached segment, or (void *)-1 on error.
     */
    void *shmat(int shmid, const void *shmaddr, int shmflg);

    /*
     * shmdt - Detach shared memory segment
     *
     * @shmaddr: Address of attached segment (from shmat)
     *
     * Returns 0 on success, -1 on error.
     */
    int shmdt(const void *shmaddr);

    /*
     * shmctl - Shared memory control operations
     *
     * @shmid: Shared memory identifier
     * @cmd: Control command (IPC_STAT, IPC_SET, IPC_RMID, etc.)
     * @buf: Pointer to shmid_ds structure
     *
     * Returns 0 or value on success, -1 on error.
     */
    int shmctl(int shmid, int cmd, struct shmid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SHM_H */
