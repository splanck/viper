#include "../include/sys/stat.h"
#include "../include/fcntl.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Syscall numbers */
#define SYS_OPEN 0x40
#define SYS_STAT 0x45
#define SYS_FSTAT 0x46
#define SYS_MKDIR 0x61
#define SYS_CHMOD 0x69
#define SYS_FCHMOD 0x6A
#define SYS_MKNOD 0x6B
#define SYS_MKFIFO 0x6C

/* Current umask value */
static mode_t current_umask = 022;

int stat(const char *pathname, struct stat *statbuf)
{
    if (!pathname || !statbuf)
        return -1;
    return (int)__syscall2(SYS_STAT, (long)pathname, (long)statbuf);
}

int fstat(int fd, struct stat *statbuf)
{
    if (!statbuf)
        return -1;
    return (int)__syscall2(SYS_FSTAT, fd, (long)statbuf);
}

int lstat(const char *pathname, struct stat *statbuf)
{
    /* ViperOS doesn't distinguish lstat from stat yet */
    /* For symlinks, this should not follow the link */
    return stat(pathname, statbuf);
}

int chmod(const char *pathname, mode_t mode)
{
    if (!pathname)
        return -1;
    return (int)__syscall2(SYS_CHMOD, (long)pathname, mode);
}

int fchmod(int fd, mode_t mode)
{
    return (int)__syscall2(SYS_FCHMOD, fd, mode);
}

int mkdir(const char *pathname, mode_t mode)
{
    if (!pathname)
        return -1;
    /* Apply umask to mode */
    mode_t effective_mode = mode & ~current_umask;
    return (int)__syscall2(SYS_MKDIR, (long)pathname, effective_mode);
}

mode_t umask(mode_t mask)
{
    mode_t old = current_umask;
    current_umask = mask & 0777;
    return old;
}

int mkfifo(const char *pathname, mode_t mode)
{
    if (!pathname)
        return -1;
    /* mkfifo is mknod with S_IFIFO type */
    mode_t effective_mode = (mode & ~current_umask) | S_IFIFO;
    return (int)__syscall2(SYS_MKFIFO, (long)pathname, effective_mode);
}

int mknod(const char *pathname, mode_t mode, dev_t dev)
{
    if (!pathname)
        return -1;
    /* Apply umask to permission bits only */
    mode_t effective_mode = (mode & S_IFMT) | ((mode & 0777) & ~current_umask);
    return (int)__syscall3(SYS_MKNOD, (long)pathname, effective_mode, dev);
}

/* fcntl.h implementations */
int open(const char *pathname, int flags, ...)
{
    /* For simplicity, extract mode from varargs only if O_CREAT is set */
    /* In a full implementation, we'd use va_list */
    mode_t mode = 0666; /* Default mode if O_CREAT */

    if (!pathname)
        return -1;

    if (flags & O_CREAT)
    {
        mode = mode & ~current_umask;
    }

    return (int)__syscall3(SYS_OPEN, (long)pathname, flags, mode);
}

int creat(const char *pathname, mode_t mode)
{
    return open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int fcntl(int fd, int cmd, ...)
{
    (void)fd; /* TODO: use fd when implementing syscalls */

    /* Basic fcntl implementation */
    /* Most commands are stubs for now */
    switch (cmd)
    {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            /* Would need to extract arg and call dup syscall */
            return -1; /* Not implemented */

        case F_GETFD:
            return 0; /* Return no flags set */

        case F_SETFD:
            return 0; /* Pretend to succeed */

        case F_GETFL:
            return O_RDWR; /* Return a default */

        case F_SETFL:
            return 0; /* Pretend to succeed */

        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            return -1; /* Locking not implemented */

        case F_GETOWN:
            return 0;

        case F_SETOWN:
            return 0;

        default:
            return -1;
    }
}

int openat(int dirfd, const char *pathname, int flags, ...)
{
    /* If dirfd is AT_FDCWD, behave like open() */
    if (dirfd == AT_FDCWD)
    {
        return open(pathname, flags);
    }

    /* openat with specific dirfd not implemented yet */
    (void)dirfd;
    (void)pathname;
    (void)flags;
    return -1;
}

int posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
    /* Advisory only - ignore */
    (void)fd;
    (void)offset;
    (void)len;
    (void)advice;
    return 0;
}

int posix_fallocate(int fd, off_t offset, off_t len)
{
    /* Not implemented */
    (void)fd;
    (void)offset;
    (void)len;
    return -1;
}
