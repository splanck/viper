#ifndef _FCNTL_H
#define _FCNTL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "sys/types.h"

/* Open flags */
#define O_RDONLY 0x0000  /* Open for reading only */
#define O_WRONLY 0x0001  /* Open for writing only */
#define O_RDWR 0x0002    /* Open for reading and writing */
#define O_ACCMODE 0x0003 /* Mask for access mode */

#define O_CREAT 0x0040      /* Create file if it doesn't exist */
#define O_EXCL 0x0080       /* Error if O_CREAT and file exists */
#define O_NOCTTY 0x0100     /* Don't assign controlling terminal */
#define O_TRUNC 0x0200      /* Truncate file to zero length */
#define O_APPEND 0x0400     /* Append mode */
#define O_NONBLOCK 0x0800   /* Non-blocking mode */
#define O_DSYNC 0x1000      /* Synchronized I/O data integrity */
#define O_SYNC 0x101000     /* Synchronized I/O file integrity */
#define O_RSYNC O_SYNC      /* Synchronized read I/O */
#define O_DIRECTORY 0x10000 /* Must be a directory */
#define O_NOFOLLOW 0x20000  /* Don't follow symlinks */
#define O_CLOEXEC 0x80000   /* Close on exec */

/* fcntl commands */
#define F_DUPFD 0            /* Duplicate file descriptor */
#define F_GETFD 1            /* Get file descriptor flags */
#define F_SETFD 2            /* Set file descriptor flags */
#define F_GETFL 3            /* Get file status flags */
#define F_SETFL 4            /* Set file status flags */
#define F_GETLK 5            /* Get record locking info */
#define F_SETLK 6            /* Set record locking info (non-blocking) */
#define F_SETLKW 7           /* Set record locking info (blocking) */
#define F_SETOWN 8           /* Set owner for SIGIO */
#define F_GETOWN 9           /* Get owner for SIGIO */
#define F_DUPFD_CLOEXEC 1030 /* Duplicate with close-on-exec */

/* File descriptor flags */
#define FD_CLOEXEC 1 /* Close on exec */

    /* flock structure for record locking */
    struct flock
    {
        short l_type;   /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
        short l_whence; /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
        off_t l_start;  /* Starting offset */
        off_t l_len;    /* Length; 0 means lock to EOF */
        pid_t l_pid;    /* Process ID holding lock (F_GETLK only) */
    };

/* Lock types */
#define F_RDLCK 0 /* Read lock */
#define F_WRLCK 1 /* Write lock */
#define F_UNLCK 2 /* Unlock */

/* Advisory flags for posix_fadvise */
#define POSIX_FADV_NORMAL 0
#define POSIX_FADV_RANDOM 1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED 3
#define POSIX_FADV_DONTNEED 4
#define POSIX_FADV_NOREUSE 5

/* AT_* constants for *at() functions */
#define AT_FDCWD -100             /* Use current working directory */
#define AT_SYMLINK_NOFOLLOW 0x100 /* Don't follow symbolic links */
#define AT_REMOVEDIR 0x200        /* Remove directory instead of file */
#define AT_SYMLINK_FOLLOW 0x400   /* Follow symbolic links */
#define AT_EACCESS 0x200          /* Use effective IDs for access check */

    /* Functions */
    int open(const char *pathname, int flags, ...);
    int creat(const char *pathname, mode_t mode);
    int fcntl(int fd, int cmd, ...);
    int openat(int dirfd, const char *pathname, int flags, ...);

    /* Advisory locking */
    int posix_fadvise(int fd, off_t offset, off_t len, int advice);
    int posix_fallocate(int fd, off_t offset, off_t len);

#ifdef __cplusplus
}
#endif

#endif /* _FCNTL_H */
