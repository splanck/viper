//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/stat.c
// Purpose: File status and mode functions for ViperDOS libc.
// Key invariants: Routes to fsd or kernel based on path; umask applied.
// Ownership/Lifetime: Library; umask is process-global.
// Links: user/libc/include/sys/stat.h, user/libc/include/fcntl.h
//
//===----------------------------------------------------------------------===//

/**
 * @file stat.c
 * @brief File status and mode functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX file status and permission functions:
 *
 * - File info: stat, fstat, lstat
 * - Permissions: chmod, fchmod, umask
 * - Directory creation: mkdir
 * - Special files: mkfifo, mknod
 * - File opening: open, creat, openat
 * - File control: fcntl
 *
 * File operations are routed through either the kernel VFS or the fsd
 * (filesystem daemon) based on the path prefix. A process-global umask
 * is applied to permission bits when creating files and directories.
 */

#include "../include/sys/stat.h"
#include "../include/fcntl.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* libc ↔ fsd bridge */
extern int __viper_fsd_is_available(void);
extern int __viper_fsd_is_fd(int fd);
extern int __viper_fsd_prepare_path(const char *in, char *out, size_t out_cap);
extern int __viper_fsd_open(const char *abs_path, int flags);
extern int __viper_fsd_stat(const char *abs_path, struct stat *statbuf);
extern int __viper_fsd_fstat(int fd, struct stat *statbuf);
extern int __viper_fsd_mkdir(const char *abs_path);

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

/**
 * @defgroup filestat File Status Functions
 * @brief Functions for retrieving and modifying file metadata.
 * @{
 */

/**
 * @brief Get file status by pathname.
 *
 * @details
 * Retrieves information about the file specified by pathname and stores
 * it in the stat structure. The information includes:
 *
 * - st_mode: File type and permissions
 * - st_ino: Inode number
 * - st_dev: Device ID
 * - st_nlink: Number of hard links
 * - st_uid/st_gid: Owner user/group IDs
 * - st_size: File size in bytes
 * - st_atime/st_mtime/st_ctime: Access/modification/change times
 *
 * If the path refers to a symbolic link, stat() follows the link and
 * returns information about the target file. Use lstat() to get
 * information about the link itself.
 *
 * @param pathname Path to the file to query.
 * @param statbuf Buffer to receive the file status.
 * @return 0 on success, -1 on error.
 *
 * @see fstat, lstat, chmod
 */
int stat(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf)
        return -1;

    if (__viper_fsd_is_available()) {
        char fsd_path[201];
        int route = __viper_fsd_prepare_path(pathname, fsd_path, sizeof(fsd_path));
        if (route > 0) {
            return __viper_fsd_stat(fsd_path, statbuf);
        }
    }

    return (int)__syscall2(SYS_STAT, (long)pathname, (long)statbuf);
}

/**
 * @brief Get file status by file descriptor.
 *
 * @details
 * Retrieves information about the file referred to by the open file
 * descriptor fd. This is equivalent to stat(), but works on an already
 * open file rather than a pathname.
 *
 * This function is useful when you have an open file descriptor and
 * want to check its attributes without knowing or using the path.
 *
 * @param fd Open file descriptor.
 * @param statbuf Buffer to receive the file status.
 * @return 0 on success, -1 on error.
 *
 * @see stat, lstat
 */
int fstat(int fd, struct stat *statbuf) {
    if (!statbuf)
        return -1;

    if (__viper_fsd_is_fd(fd)) {
        return __viper_fsd_fstat(fd, statbuf);
    }

    return (int)__syscall2(SYS_FSTAT, fd, (long)statbuf);
}

/**
 * @brief Get symbolic link status.
 *
 * @details
 * Identical to stat(), except that if pathname refers to a symbolic link,
 * lstat() returns information about the link itself rather than the file
 * it refers to.
 *
 * @note ViperDOS does not currently distinguish between lstat() and stat().
 * This function calls stat() directly.
 *
 * @param pathname Path to the file or symbolic link to query.
 * @param statbuf Buffer to receive the file status.
 * @return 0 on success, -1 on error.
 *
 * @see stat, fstat, readlink
 */
int lstat(const char *pathname, struct stat *statbuf) {
    /* ViperDOS doesn't distinguish lstat from stat yet */
    /* For symlinks, this should not follow the link */
    return stat(pathname, statbuf);
}

/**
 * @brief Change file permissions.
 *
 * @details
 * Changes the permission bits of the file specified by pathname.
 * The mode argument specifies the new permissions using the standard
 * Unix permission bits:
 *
 * - S_IRUSR, S_IWUSR, S_IXUSR: Owner read/write/execute
 * - S_IRGRP, S_IWGRP, S_IXGRP: Group read/write/execute
 * - S_IROTH, S_IWOTH, S_IXOTH: Other read/write/execute
 * - S_ISUID, S_ISGID, S_ISVTX: Set-user-ID, set-group-ID, sticky bit
 *
 * @param pathname Path to the file to modify.
 * @param mode New permission mode.
 * @return 0 on success, -1 on error.
 *
 * @see fchmod, stat, umask
 */
int chmod(const char *pathname, mode_t mode) {
    if (!pathname)
        return -1;
    return (int)__syscall2(SYS_CHMOD, (long)pathname, mode);
}

/**
 * @brief Change file permissions by file descriptor.
 *
 * @details
 * Changes the permission bits of the file referred to by the open file
 * descriptor fd. This is equivalent to chmod(), but works on an already
 * open file.
 *
 * @param fd Open file descriptor.
 * @param mode New permission mode.
 * @return 0 on success, -1 on error.
 *
 * @see chmod, fstat
 */
int fchmod(int fd, mode_t mode) {
    return (int)__syscall2(SYS_FCHMOD, fd, mode);
}

/**
 * @brief Create a directory.
 *
 * @details
 * Creates a new directory with the specified pathname and permission mode.
 * The actual permissions are modified by the process's umask, so the
 * effective mode is (mode & ~umask).
 *
 * The directory is created with entries "." and ".." automatically.
 * Parent directories must already exist; mkdir() does not create
 * intermediate directories.
 *
 * @param pathname Path of the directory to create.
 * @param mode Permission mode for the new directory.
 * @return 0 on success, -1 on error.
 *
 * @see rmdir, umask, stat
 */
int mkdir(const char *pathname, mode_t mode) {
    if (!pathname)
        return -1;
    /* Apply umask to mode */
    mode_t effective_mode = mode & ~current_umask;

    if (__viper_fsd_is_available()) {
        char fsd_path[201];
        int route = __viper_fsd_prepare_path(pathname, fsd_path, sizeof(fsd_path));
        if (route > 0) {
            (void)effective_mode; /* fsd currently ignores mode */
            return __viper_fsd_mkdir(fsd_path);
        }
    }

    return (int)__syscall2(SYS_MKDIR, (long)pathname, effective_mode);
}

/**
 * @brief Set the file mode creation mask.
 *
 * @details
 * Sets the process's file mode creation mask (umask) to the specified value
 * and returns the previous mask. The umask is used by open(), creat(),
 * mkdir(), and mkfifo() to modify the permissions of newly created files.
 *
 * When creating a file or directory with a specified mode, the actual
 * permissions are (mode & ~umask). For example, with umask 022, a file
 * created with mode 0666 will have permissions 0644.
 *
 * The default umask in ViperDOS is 022 (octal), which removes write
 * permission for group and others on new files.
 *
 * @param mask New umask value (only the permission bits 0777 are used).
 * @return Previous umask value.
 *
 * @see open, mkdir, chmod
 */
mode_t umask(mode_t mask) {
    mode_t old = current_umask;
    current_umask = mask & 0777;
    return old;
}

/**
 * @brief Create a named pipe (FIFO).
 *
 * @details
 * Creates a named pipe (FIFO) special file with the specified pathname.
 * A FIFO is similar to a pipe, but is accessed via the filesystem rather
 * than through anonymous pipe() file descriptors.
 *
 * Any process can open a FIFO for reading or writing. Data written to
 * the FIFO is buffered until read, providing inter-process communication.
 *
 * The permission mode is modified by the process's umask.
 *
 * @param pathname Path of the FIFO to create.
 * @param mode Permission mode for the FIFO.
 * @return 0 on success, -1 on error.
 *
 * @see pipe, mknod, open
 */
int mkfifo(const char *pathname, mode_t mode) {
    if (!pathname)
        return -1;
    /* mkfifo is mknod with S_IFIFO type */
    mode_t effective_mode = (mode & ~current_umask) | S_IFIFO;
    return (int)__syscall2(SYS_MKFIFO, (long)pathname, effective_mode);
}

/** @} */ /* end of filestat group */

/**
 * @brief Create a special or ordinary file.
 *
 * @details
 * Creates a filesystem node (file, device special file, or named pipe)
 * with the specified pathname, mode, and device number. The mode specifies
 * both the file type and permission bits:
 *
 * File type (one of):
 * - S_IFREG: Regular file (dev is ignored)
 * - S_IFCHR: Character special device
 * - S_IFBLK: Block special device
 * - S_IFIFO: FIFO (named pipe, dev is ignored)
 *
 * For S_IFCHR and S_IFBLK, the dev parameter specifies the device
 * major and minor numbers.
 *
 * Permission bits are masked by the process's umask.
 *
 * @param pathname Path of the node to create.
 * @param mode File type and permissions (S_IFREG|0644, S_IFCHR|0666, etc.).
 * @param dev Device number for character/block special files.
 * @return 0 on success, -1 on error.
 *
 * @see mkfifo, mkdir, stat
 */
int mknod(const char *pathname, mode_t mode, dev_t dev) {
    if (!pathname)
        return -1;
    /* Apply umask to permission bits only */
    mode_t effective_mode = (mode & S_IFMT) | ((mode & 0777) & ~current_umask);
    return (int)__syscall3(SYS_MKNOD, (long)pathname, effective_mode, dev);
}

/**
 * @defgroup fileio File I/O Operations
 * @brief Functions for opening, controlling, and manipulating files.
 * @{
 */

/**
 * @brief Open a file.
 *
 * @details
 * Opens the file specified by pathname according to the flags parameter.
 * Returns a file descriptor that can be used with read(), write(), and
 * other I/O operations.
 *
 * Common flags:
 * - O_RDONLY: Open for reading only
 * - O_WRONLY: Open for writing only
 * - O_RDWR: Open for reading and writing
 * - O_CREAT: Create file if it doesn't exist (requires mode argument)
 * - O_TRUNC: Truncate file to zero length
 * - O_APPEND: Append to end of file on each write
 * - O_EXCL: With O_CREAT, fail if file exists
 *
 * If O_CREAT is specified, a third argument (mode_t mode) is required
 * to specify the permissions for the new file. The actual permissions
 * are (mode & ~umask).
 *
 * File paths are routed through either the kernel VFS or the fsd
 * (filesystem daemon) depending on the path prefix.
 *
 * @param pathname Path to the file to open.
 * @param flags Opening flags (O_RDONLY, O_WRONLY, O_RDWR, etc.).
 * @param ... Optional mode argument if O_CREAT is specified.
 * @return Non-negative file descriptor on success, -1 on error.
 *
 * @see close, read, write, creat
 */
int open(const char *pathname, int flags, ...) {
    /* For simplicity, extract mode from varargs only if O_CREAT is set */
    /* In a full implementation, we'd use va_list */
    mode_t mode = 0666; /* Default mode if O_CREAT */

    if (!pathname)
        return -1;

    if (flags & O_CREAT) {
        mode = mode & ~current_umask;
    }

    if (__viper_fsd_is_available()) {
        char fsd_path[201];
        int route = __viper_fsd_prepare_path(pathname, fsd_path, sizeof(fsd_path));
        if (route > 0) {
            (void)mode; /* fsd currently ignores mode */
            return __viper_fsd_open(fsd_path, flags);
        }
    }

    return (int)__syscall3(SYS_OPEN, (long)pathname, flags, mode);
}

/**
 * @brief Create and open a file for writing.
 *
 * @details
 * Creates a new file or truncates an existing file and opens it for
 * writing. This is equivalent to:
 *
 *     open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode)
 *
 * The file is created with the specified mode, modified by the process's
 * umask. If the file already exists, it is truncated to zero length.
 *
 * @param pathname Path to the file to create.
 * @param mode Permission mode for the new file.
 * @return Non-negative file descriptor on success, -1 on error.
 *
 * @see open, close
 */
int creat(const char *pathname, mode_t mode) {
    return open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

/**
 * @brief Perform file descriptor control operations.
 *
 * @details
 * Performs various control operations on an open file descriptor.
 * The operation is determined by the cmd argument:
 *
 * - F_DUPFD: Duplicate fd to lowest available fd >= arg
 * - F_GETFD: Get file descriptor flags (FD_CLOEXEC)
 * - F_SETFD: Set file descriptor flags
 * - F_GETFL: Get file status flags (O_RDONLY, O_NONBLOCK, etc.)
 * - F_SETFL: Set file status flags
 * - F_GETLK/F_SETLK/F_SETLKW: File locking operations
 * - F_GETOWN/F_SETOWN: Get/set async I/O ownership
 *
 * @note Most operations are stubs in ViperDOS. F_GETFD returns 0,
 * F_GETFL returns O_RDWR, and F_SETFD/F_SETFL pretend to succeed.
 * Locking operations (F_GETLK/F_SETLK/F_SETLKW) are not implemented.
 *
 * @param fd File descriptor to operate on.
 * @param cmd Operation to perform.
 * @param ... Command-specific argument.
 * @return Command-specific result on success, -1 on error.
 *
 * @see open, dup, dup2
 */
int fcntl(int fd, int cmd, ...) {
    (void)fd; /* TODO: use fd when implementing syscalls */

    /* Basic fcntl implementation */
    /* Most commands are stubs for now */
    switch (cmd) {
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

/**
 * @brief Open a file relative to a directory file descriptor.
 *
 * @details
 * Opens a file relative to the directory specified by dirfd. If pathname
 * is absolute, dirfd is ignored. If pathname is relative:
 *
 * - If dirfd is AT_FDCWD, pathname is interpreted relative to the
 *   current working directory (equivalent to open())
 * - Otherwise, pathname is interpreted relative to the directory
 *   referred to by dirfd
 *
 * @note ViperDOS only supports AT_FDCWD. Opening relative to other
 * directory file descriptors is not implemented.
 *
 * @param dirfd Directory file descriptor (or AT_FDCWD).
 * @param pathname Path to the file to open.
 * @param flags Opening flags (same as open()).
 * @param ... Optional mode argument if O_CREAT is specified.
 * @return Non-negative file descriptor on success, -1 on error.
 *
 * @see open, fstatat
 */
int openat(int dirfd, const char *pathname, int flags, ...) {
    /* If dirfd is AT_FDCWD, behave like open() */
    if (dirfd == AT_FDCWD) {
        return open(pathname, flags);
    }

    /* openat with specific dirfd not implemented yet */
    (void)dirfd;
    (void)pathname;
    (void)flags;
    return -1;
}

/**
 * @brief Advise the kernel about file access patterns.
 *
 * @details
 * Provides a hint to the kernel about the expected access pattern for
 * the file region specified by offset and len. The kernel may use this
 * to optimize read-ahead and caching behavior.
 *
 * Common advice values:
 * - POSIX_FADV_NORMAL: Default access pattern
 * - POSIX_FADV_SEQUENTIAL: Sequential access from lower to higher offsets
 * - POSIX_FADV_RANDOM: Random access pattern
 * - POSIX_FADV_WILLNEED: Data will be needed soon (trigger read-ahead)
 * - POSIX_FADV_DONTNEED: Data won't be needed soon (can drop from cache)
 *
 * @note This is advisory only. ViperDOS ignores this hint.
 *
 * @param fd File descriptor.
 * @param offset Start of the region to advise about.
 * @param len Length of the region (0 means to end of file).
 * @param advice Access pattern hint.
 * @return 0 on success (always succeeds in ViperDOS).
 *
 * @see posix_fallocate, madvise
 */
int posix_fadvise(int fd, off_t offset, off_t len, int advice) {
    /* Advisory only - ignore */
    (void)fd;
    (void)offset;
    (void)len;
    (void)advice;
    return 0;
}

/**
 * @brief Pre-allocate space for a file.
 *
 * @details
 * Ensures that disk space is allocated for the file region specified
 * by offset and len. After a successful call, subsequent writes to
 * this region are guaranteed not to fail due to lack of disk space.
 *
 * Unlike ftruncate(), posix_fallocate() does not change the file size
 * if offset + len is less than the current file size. It only allocates
 * disk blocks for the specified region.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param fd File descriptor.
 * @param offset Start of the region to allocate.
 * @param len Length of the region to allocate.
 * @return 0 on success, error number on failure (-1 in ViperDOS).
 *
 * @see posix_fadvise, ftruncate
 */
int posix_fallocate(int fd, off_t offset, off_t len) {
    /* Not implemented */
    (void)fd;
    (void)offset;
    (void)len;
    return -1;
}

/** @} */ /* end of fileio group */
