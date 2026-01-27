#ifndef _SYS_FILE_H
#define _SYS_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

/* flock() operation types */
#define LOCK_SH 1 /* Shared lock */
#define LOCK_EX 2 /* Exclusive lock */
#define LOCK_NB 4 /* Don't block when locking */
#define LOCK_UN 8 /* Unlock */

/**
 * @brief Apply or remove an advisory lock on an open file.
 *
 * @param fd File descriptor.
 * @param operation LOCK_SH, LOCK_EX, LOCK_UN, optionally ORed with LOCK_NB.
 * @return 0 on success, -1 on error (errno set).
 */
int flock(int fd, int operation);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FILE_H */
