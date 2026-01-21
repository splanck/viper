//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/unistd.c
// Purpose: POSIX-like system calls and process control for ViperDOS.
// Key invariants: Syscall wrappers with fsd/netd routing for file descriptors.
// Ownership/Lifetime: Library; wraps kernel syscalls.
// Links: user/libc/include/unistd.h
//
//===----------------------------------------------------------------------===//

/**
 * @file unistd.c
 * @brief POSIX-like system calls and process control for ViperDOS libc.
 *
 * @details
 * This file implements standard UNIX/POSIX system call wrappers:
 *
 * - File I/O: read, write, close, lseek, dup, dup2
 * - File system: access, unlink, rmdir, rename, symlink, readlink
 * - Process control: fork, execve, exit, getpid, getppid
 * - Process groups: getpgrp, setpgid, setsid
 * - User/group IDs: getuid, geteuid, getgid, getegid, setuid, setgid
 * - Working directory: getcwd, chdir
 * - Sleep/timing: sleep, usleep
 * - System info: sysconf, isatty, gethostname, sethostname
 *
 * File descriptor operations are routed through the appropriate backend:
 * - FDs 0-2 (stdin/stdout/stderr): Direct kernel syscalls with termios support
 * - FD range 100-199: Routed to fsd (filesystem daemon)
 * - FD range 200-299: Routed to netd (network daemon) via socket layer
 * - Other FDs: Direct kernel syscalls
 */

#include "../include/unistd.h"
#include "../include/termios.h"

/* Syscall helpers - defined in syscall.S */
extern long __syscall0(long num);
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall5(long num, long arg0, long arg1, long arg2, long arg3, long arg4);

/* libc ↔ fsd bridge */
extern int __viper_fsd_is_available(void);
extern int __viper_fsd_is_fd(int fd);
extern int __viper_fsd_prepare_path(const char *in, char *out, size_t out_cap);
extern ssize_t __viper_fsd_read(int fd, void *buf, size_t count);
extern ssize_t __viper_fsd_write(int fd, const void *buf, size_t count);
extern int __viper_fsd_close(int fd);
extern long __viper_fsd_lseek(int fd, long offset, int whence);
extern int __viper_fsd_dup(int oldfd);
extern int __viper_fsd_dup2(int oldfd, int newfd);
extern int __viper_fsd_unlink(const char *abs_path);
extern int __viper_fsd_rmdir(const char *abs_path);
extern int __viper_fsd_chdir(const char *path);
extern int __viper_fsd_getcwd(char *buf, size_t size);
extern int __viper_fsd_rename(const char *abs_old, const char *abs_new);

/* libc ↔ socket FD bridge */
extern int __viper_socket_is_fd(int fd);
extern int __viper_socket_close(int fd);
extern int __viper_socket_dup(int oldfd);
extern int __viper_socket_dup2(int oldfd, int newfd);

/* libc ↔ consoled bridge */
extern int __viper_consoled_is_available(void);
extern ssize_t __viper_consoled_write(const void *buf, size_t count);
extern int __viper_consoled_input_available(void);
extern int __viper_consoled_getchar(void);
extern int __viper_consoled_trygetchar(void);

/* Syscall numbers from viperdos/syscall_nums.hpp */
#define SYS_TASK_CURRENT 0x02
#define SYS_SBRK 0x0A
#define SYS_SLEEP 0x31
#define SYS_TIME_NOW 0x30
#define SYS_OPEN 0x40
#define SYS_CLOSE 0x41
#define SYS_READ 0x42
#define SYS_WRITE 0x43
#define SYS_LSEEK 0x44
#define SYS_DUP 0x47
#define SYS_DUP2 0x48
#define SYS_GETCWD 0x67
#define SYS_CHDIR 0x68
#define SYS_GETCHAR 0xF1
#define SYS_TTY_WRITE 0x121


/**
 * @brief Read data from a file descriptor.
 *
 * @details
 * Reads up to count bytes from file descriptor fd into buffer buf.
 * For stdin (fd 0), implements terminal line discipline with support
 * for canonical mode (line editing) and raw mode (immediate return).
 *
 * The function routes reads through the appropriate backend:
 * - stdin: Kernel syscall with termios processing
 * - fsd FDs (100-199): Routed to filesystem daemon
 * - Other FDs: Direct kernel syscall
 *
 * @param fd File descriptor to read from.
 * @param buf Buffer to store read data.
 * @param count Maximum number of bytes to read.
 * @return Number of bytes read, 0 on EOF, or -1 on error.
 */
/**
 * @brief Read a single character from stdin (kernel TTY buffer).
 * @return Character (0-255), or -1 on EOF/error.
 *
 * @details
 * Input comes from the kernel TTY buffer, which is populated by consoled
 * when it receives keyboard events. This blocking call uses tty_read()
 * which sleeps until a character is available.
 */
static int stdin_getchar_blocking(void)
{
    /* Use kernel TTY buffer - blocking read */
    return __viper_consoled_getchar();
}

ssize_t read(int fd, void *buf, size_t count)
{
    if (count == 0)
        return 0;

    /* stdin: implement minimal TTY line discipline in libc using termios. */
    if (fd == STDIN_FILENO)
    {
        static char line_buf[1024];
        static size_t line_len = 0;
        static size_t line_pos = 0;

        struct termios t;
        if (tcgetattr(STDIN_FILENO, &t) != 0)
        {
            /* No termios - just read raw from consoled or kernel */
            if (__viper_consoled_input_available())
            {
                unsigned char *out = (unsigned char *)buf;
                size_t nread = 0;
                while (nread < count)
                {
                    int c = __viper_consoled_getchar();
                    if (c < 0)
                        break;
                    out[nread++] = (unsigned char)c;
                }
                return (ssize_t)nread;
            }
            return __syscall3(SYS_READ, fd, (long)buf, (long)count);
        }

        const int is_canon = (t.c_lflag & ICANON) != 0;
        const int do_echo = (t.c_lflag & ECHO) != 0;
        const int map_crnl = (t.c_iflag & ICRNL) != 0;

        const unsigned char v_eof = t.c_cc[VEOF];
        const unsigned char v_erase = t.c_cc[VERASE];
        const unsigned char v_kill = t.c_cc[VKILL];

        /* If canonical mode, fill a cooked line buffer (supports erase/kill + echo). */
        if (is_canon)
        {
            if (line_pos >= line_len)
            {
                line_len = 0;
                line_pos = 0;

                while (line_len < sizeof(line_buf) - 1)
                {
                    int ch = stdin_getchar_blocking();
                    if (ch < 0)
                    {
                        if (line_len == 0)
                            return 0;
                        break;
                    }
                    unsigned char c = (unsigned char)ch;

                    if (map_crnl && c == '\r')
                        c = '\n';

                    if (c == v_eof)
                    {
                        if (line_len == 0)
                        {
                            return 0;
                        }
                        break;
                    }

                    if (c == v_erase || c == '\b')
                    {
                        if (line_len > 0)
                        {
                            line_len--;
                            if (do_echo)
                            {
                                const char bs[] = {'\b', ' ', '\b'};
                                (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)bs, 3);
                            }
                        }
                        continue;
                    }

                    if (c == v_kill)
                    {
                        if (do_echo)
                        {
                            while (line_len > 0)
                            {
                                line_len--;
                                const char bs[] = {'\b', ' ', '\b'};
                                (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)bs, 3);
                            }
                        }
                        else
                        {
                            line_len = 0;
                        }
                        continue;
                    }

                    if (c == '\n')
                    {
                        line_buf[line_len++] = (char)c;
                        if (do_echo)
                        {
                            const char nl[] = {'\r', '\n'};
                            (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)nl, 2);
                        }
                        break;
                    }

                    line_buf[line_len++] = (char)c;
                    if (do_echo)
                    {
                        (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)&c, 1);
                    }
                }
            }

            size_t avail = line_len - line_pos;
            size_t to_copy = (count < avail) ? count : avail;
            char *out = (char *)buf;
            for (size_t i = 0; i < to_copy; i++)
            {
                out[i] = line_buf[line_pos + i];
            }
            line_pos += to_copy;
            return (ssize_t)to_copy;
        }

        /* Non-canonical mode: return available bytes immediately if VMIN==0. */
        if (t.c_cc[VMIN] == 0)
        {
            unsigned char *out = (unsigned char *)buf;
            size_t nread = 0;

            /* If VTIME is non-zero, block until at least 1 byte arrives (ignore timeout). */
            if (t.c_cc[VTIME] != 0 && nread < count)
            {
                int ch = stdin_getchar_blocking();
                if (ch < 0)
                    return 0;
                unsigned char c = (unsigned char)ch;
                if (map_crnl && c == '\r')
                    c = '\n';
                out[nread++] = c;
                if (do_echo)
                {
                    (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)&c, 1);
                }
            }

            /* Try to get more chars non-blocking.
             * IMPORTANT: Only fall back to kernel if consoled is NOT in use.
             * If consoled is active, kernel's buffer has stale/duplicate input. */
            while (nread < count)
            {
                int ch;
                if (__viper_consoled_input_available())
                {
                    ch = __viper_consoled_trygetchar();
                }
                else if (!__viper_consoled_is_available())
                {
                    /* No consoled at all - use kernel (serial/pre-GUI mode) */
                    long rc = __syscall0(SYS_GETCHAR);
                    ch = (rc >= 0) ? (int)rc : -1;
                }
                else
                {
                    /* Consoled is active but input not ready - don't use kernel */
                    ch = -1;
                }
                if (ch < 0)
                    break;

                unsigned char c = (unsigned char)ch;
                if (map_crnl && c == '\r')
                    c = '\n';
                out[nread++] = c;

                if (do_echo)
                {
                    (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)&c, 1);
                }
            }

            return (ssize_t)nread;
        }
        else
        {
            /* Non-canonical mode with VMIN > 0: block until at least VMIN bytes.
             * Route through consoled if available to avoid reading stale input
             * from the kernel's shared console buffer. */
            unsigned char vmin = t.c_cc[VMIN];
            unsigned char *out = (unsigned char *)buf;
            size_t nread = 0;

            /* Block until we have at least VMIN bytes (or count, whichever is less) */
            size_t min_read = (vmin < count) ? vmin : count;

            while (nread < min_read)
            {
                int ch = stdin_getchar_blocking();
                if (ch < 0)
                {
                    if (nread > 0)
                        break;
                    continue; /* Keep blocking for first char */
                }
                unsigned char c = (unsigned char)ch;
                if (map_crnl && c == '\r')
                    c = '\n';
                out[nread++] = c;
                if (do_echo)
                {
                    (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)&c, 1);
                }
            }

            /* Try to read more up to count (non-blocking).
             * IMPORTANT: Only fall back to kernel if consoled is NOT in use.
             * If consoled is active, kernel's buffer has stale/duplicate input. */
            while (nread < count)
            {
                int ch;
                if (__viper_consoled_input_available())
                {
                    ch = __viper_consoled_trygetchar();
                }
                else if (!__viper_consoled_is_available())
                {
                    /* No consoled at all - use kernel (serial/pre-GUI mode) */
                    long rc = __syscall0(SYS_GETCHAR);
                    ch = (rc >= 0) ? (int)rc : -1;
                }
                else
                {
                    /* Consoled is active but input not ready - don't use kernel */
                    ch = -1;
                }
                if (ch < 0)
                    break;

                unsigned char c = (unsigned char)ch;
                if (map_crnl && c == '\r')
                    c = '\n';
                out[nread++] = c;
                if (do_echo)
                {
                    (void)__syscall3(SYS_WRITE, STDOUT_FILENO, (long)&c, 1);
                }
            }

            return (ssize_t)nread;
        }
    }

    if (__viper_fsd_is_fd(fd))
    {
        return __viper_fsd_read(fd, buf, count);
    }

    return __syscall3(SYS_READ, fd, (long)buf, (long)count);
}

/**
 * @brief Write data to a file descriptor.
 *
 * @details
 * Writes up to count bytes from buffer buf to file descriptor fd.
 * Routes writes through the appropriate backend based on FD type.
 *
 * For stdout (fd 1) and stderr (fd 2), also routes output to consoled
 * if available, so programs display in the GUI console window.
 *
 * @param fd File descriptor to write to.
 * @param buf Buffer containing data to write.
 * @param count Number of bytes to write.
 * @return Number of bytes written, or -1 on error.
 */
ssize_t write(int fd, const void *buf, size_t count)
{
    if (__viper_fsd_is_fd(fd))
    {
        return __viper_fsd_write(fd, buf, count);
    }

    /* For stdout/stderr, route through consoled for GUI display.
     * If consoled is not available, fall back to kernel TTY. */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
        if (__viper_consoled_is_available())
        {
            return __viper_consoled_write(buf, count);
        }
        /* Fallback to kernel TTY if consoled not available */
        return __syscall2(SYS_TTY_WRITE, (long)buf, (long)count);
    }

    return __syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

/**
 * @brief Close a file descriptor.
 *
 * @details
 * Closes the file descriptor fd, releasing any associated resources.
 * Routes the close to the appropriate backend (fsd, socket, or kernel).
 *
 * @param fd File descriptor to close.
 * @return 0 on success, -1 on error.
 */
int close(int fd)
{
    if (__viper_fsd_is_fd(fd))
    {
        return __viper_fsd_close(fd);
    }
    if (__viper_socket_is_fd(fd))
    {
        return __viper_socket_close(fd);
    }
    return (int)__syscall1(SYS_CLOSE, fd);
}

/**
 * @brief Reposition file offset.
 *
 * @details
 * Repositions the file offset of the open file description associated
 * with fd according to the directive whence:
 * - SEEK_SET: offset from beginning of file
 * - SEEK_CUR: offset from current position
 * - SEEK_END: offset from end of file
 *
 * @param fd File descriptor.
 * @param offset New offset (interpretation depends on whence).
 * @param whence How to interpret offset (SEEK_SET, SEEK_CUR, SEEK_END).
 * @return New file offset, or -1 on error.
 */
long lseek(int fd, long offset, int whence)
{
    if (__viper_fsd_is_fd(fd))
    {
        return __viper_fsd_lseek(fd, offset, whence);
    }
    return __syscall3(SYS_LSEEK, fd, offset, whence);
}

/**
 * @brief Duplicate a file descriptor.
 *
 * @details
 * Creates a copy of oldfd using the lowest available file descriptor number.
 * Both descriptors refer to the same open file description.
 *
 * @param oldfd File descriptor to duplicate.
 * @return New file descriptor, or -1 on error.
 */
int dup(int oldfd)
{
    if (__viper_fsd_is_fd(oldfd))
    {
        return __viper_fsd_dup(oldfd);
    }
    if (__viper_socket_is_fd(oldfd))
    {
        return __viper_socket_dup(oldfd);
    }
    return (int)__syscall1(SYS_DUP, oldfd);
}

/**
 * @brief Duplicate a file descriptor to a specific number.
 *
 * @details
 * Creates a copy of oldfd using newfd as the new descriptor number.
 * If newfd is already open, it is closed first. Both descriptors
 * refer to the same open file description after the call.
 *
 * @param oldfd File descriptor to duplicate.
 * @param newfd Desired new file descriptor number.
 * @return newfd on success, or -1 on error.
 */
int dup2(int oldfd, int newfd)
{
    if (__viper_fsd_is_fd(oldfd))
    {
        return __viper_fsd_dup2(oldfd, newfd);
    }
    if (__viper_socket_is_fd(oldfd))
    {
        return __viper_socket_dup2(oldfd, newfd);
    }
    if (__viper_fsd_is_fd(newfd))
    {
        return -7; /* VERR_NOT_SUPPORTED */
    }
    if (__viper_socket_is_fd(newfd))
    {
        return -7; /* VERR_NOT_SUPPORTED */
    }
    return (int)__syscall2(SYS_DUP2, oldfd, newfd);
}

/**
 * @brief Change the program break (data segment size).
 *
 * @details
 * Increases or decreases the program's data segment by increment bytes.
 * Used internally by malloc() to obtain memory from the kernel.
 *
 * @param increment Number of bytes to add (or subtract if negative).
 * @return Previous program break on success, (void*)-1 on failure.
 */
void *sbrk(long increment)
{
    long result = __syscall1(SYS_SBRK, increment);
    if (result < 0)
    {
        return (void *)-1;
    }
    return (void *)result;
}

/**
 * @brief Suspend execution for seconds.
 *
 * @details
 * Causes the calling process to sleep for the specified number of seconds.
 * The actual sleep time may be shorter if a signal is delivered.
 *
 * @param seconds Number of seconds to sleep.
 * @return 0 if sleep completed, or remaining seconds if interrupted.
 */
unsigned int sleep(unsigned int seconds)
{
    __syscall1(SYS_SLEEP, seconds * 1000);
    return 0;
}

/**
 * @brief Suspend execution for microseconds.
 *
 * @details
 * Causes the calling process to sleep for the specified number of
 * microseconds. The actual granularity is milliseconds (rounded up).
 *
 * @param usec Number of microseconds to sleep.
 * @return 0 on success, -1 on error.
 */
int usleep(useconds_t usec)
{
    /* Convert microseconds to milliseconds (rounded up) */
    unsigned long ms = (usec + 999) / 1000;
    if (ms == 0 && usec > 0)
        ms = 1;
    __syscall1(SYS_SLEEP, ms);
    return 0;
}

/**
 * @brief Get process ID.
 *
 * @details
 * Returns the process ID of the calling process. In ViperDOS, this
 * corresponds to the task ID.
 *
 * @return Process ID of the calling process.
 */
pid_t getpid(void)
{
    return (pid_t)__syscall1(SYS_TASK_CURRENT, 0);
}

/**
 * @brief Get parent process ID.
 *
 * @details
 * Returns the process ID of the parent process. ViperDOS doesn't
 * currently track parent processes, so this always returns 1 (init).
 *
 * @return Parent process ID (always 1 in current implementation).
 */
pid_t getppid(void)
{
    /* ViperDOS doesn't track parent process yet, return 1 (init) */
    return 1;
}

/**
 * @brief Get current working directory.
 *
 * @details
 * Copies the absolute pathname of the current working directory to
 * the buffer buf, which has size bytes available.
 *
 * @param buf Buffer to store the pathname.
 * @param size Size of the buffer in bytes.
 * @return buf on success, NULL on error.
 */
char *getcwd(char *buf, size_t size)
{
    /* Try fsd first if available */
    if (__viper_fsd_is_available())
    {
        int result = __viper_fsd_getcwd(buf, size);
        if (result >= 0)
        {
            return buf;
        }
    }

    /* Fall back to kernel syscall */
    long result = __syscall2(SYS_GETCWD, (long)buf, (long)size);
    if (result < 0)
    {
        return (char *)0;
    }
    return buf;
}

/**
 * @brief Change current working directory.
 *
 * @details
 * Changes the current working directory to the specified path.
 * The path can be absolute or relative to the current directory.
 * Routes to fsd for user paths, kernel for /sys paths.
 *
 * @param path Path to the new working directory.
 * @return 0 on success, -1 on error.
 */
int chdir(const char *path)
{
    if (!path)
        return -1;

    /* Check if this is a /sys path (kernel VFS) */
    if (path[0] == '/' && path[1] == 's' && path[2] == 'y' && path[3] == 's' &&
        (path[4] == '\0' || path[4] == '/'))
    {
        return (int)__syscall1(SYS_CHDIR, (long)path);
    }

    /* Try fsd for user paths */
    if (__viper_fsd_is_available())
    {
        int result = __viper_fsd_chdir(path);
        if (result == 0)
        {
            return 0;
        }
        return -1;
    }

    /* Fall back to kernel syscall */
    return (int)__syscall1(SYS_CHDIR, (long)path);
}

/**
 * @brief Test if file descriptor refers to a terminal.
 *
 * @details
 * Checks if the file descriptor refers to a terminal device.
 * In ViperDOS, stdin (0), stdout (1), and stderr (2) are terminals.
 *
 * @param fd File descriptor to test.
 * @return 1 if terminal, 0 otherwise.
 */
int isatty(int fd)
{
    /* stdin, stdout, stderr are terminals */
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

/**
 * @brief Get system configuration values.
 *
 * @details
 * Returns the value of a system configuration option.
 * Supported options:
 * - _SC_CLK_TCK: Clock ticks per second (1000)
 * - _SC_PAGESIZE: System page size (4096)
 *
 * @param name Configuration option name.
 * @return Configuration value, or -1 if unsupported.
 */
long sysconf(int name)
{
    switch (name)
    {
        case _SC_CLK_TCK:
            return 1000; /* 1000 ticks per second (millisecond resolution) */
        case _SC_PAGESIZE:
            return 4096;
        default:
            return -1;
    }
}

/* Additional syscall numbers */
#define SYS_STAT 0x45
#define SYS_MKDIR 0x61
#define SYS_RMDIR 0x62
#define SYS_UNLINK 0x63
#define SYS_RENAME 0x64
#define SYS_SYMLINK 0x65
#define SYS_READLINK 0x66
#define SYS_FORK 0x0B
#define SYS_GETPGID 0xA2
#define SYS_SETPGID 0xA3
#define SYS_SETSID 0xA5
#define SYS_GETPID 0xA0
#define SYS_GETPPID 0xA1

int access(const char *pathname, int mode)
{
    /* Simple implementation: check if file exists by trying to stat it */
    /* ViperDOS doesn't have full permission model yet */
    (void)mode;
    long result = __syscall2(SYS_STAT, (long)pathname, 0);
    return (result < 0) ? -1 : 0;
}

int unlink(const char *pathname)
{
    if (__viper_fsd_is_available())
    {
        char fsd_path[201];
        int route = __viper_fsd_prepare_path(pathname, fsd_path, sizeof(fsd_path));
        if (route > 0)
        {
            return __viper_fsd_unlink(fsd_path);
        }
    }
    return (int)__syscall1(SYS_UNLINK, (long)pathname);
}

int rmdir(const char *pathname)
{
    if (__viper_fsd_is_available())
    {
        char fsd_path[201];
        int route = __viper_fsd_prepare_path(pathname, fsd_path, sizeof(fsd_path));
        if (route > 0)
        {
            return __viper_fsd_rmdir(fsd_path);
        }
    }
    return (int)__syscall1(SYS_RMDIR, (long)pathname);
}

int link(const char *oldpath, const char *newpath)
{
    /* Hard links not implemented yet */
    (void)oldpath;
    (void)newpath;
    return -1; /* ENOSYS */
}

int rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -1;

    if (__viper_fsd_is_available())
    {
        char old_fsd[201];
        char new_fsd[201];
        int r0 = __viper_fsd_prepare_path(oldpath, old_fsd, sizeof(old_fsd));
        int r1 = __viper_fsd_prepare_path(newpath, new_fsd, sizeof(new_fsd));
        if (r0 > 0 && r1 > 0)
        {
            return __viper_fsd_rename(old_fsd, new_fsd);
        }
    }

    return (int)__syscall2(SYS_RENAME, (long)oldpath, (long)newpath);
}

int symlink(const char *target, const char *linkpath)
{
    return (int)__syscall2(SYS_SYMLINK, (long)target, (long)linkpath);
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)
{
    return __syscall3(SYS_READLINK, (long)pathname, (long)buf, (long)bufsiz);
}

/* Static hostname buffer */
static char hostname_buf[256] = "viperdos";

int gethostname(char *name, size_t len)
{
    if (!name || len == 0)
        return -1;

    size_t i = 0;
    while (i < len - 1 && hostname_buf[i])
    {
        name[i] = hostname_buf[i];
        i++;
    }
    name[i] = '\0';
    return 0;
}

int sethostname(const char *name, size_t len)
{
    if (!name)
        return -1;

    size_t i = 0;
    while (i < len && i < sizeof(hostname_buf) - 1 && name[i])
    {
        hostname_buf[i] = name[i];
        i++;
    }
    hostname_buf[i] = '\0';
    return 0;
}

/* User/group IDs - ViperDOS is single-user, always return 0 (root) */
uid_t getuid(void)
{
    return 0;
}

uid_t geteuid(void)
{
    return 0;
}

gid_t getgid(void)
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

int setuid(uid_t uid)
{
    (void)uid;
    return 0; /* Always succeeds in single-user system */
}

int setgid(gid_t gid)
{
    (void)gid;
    return 0;
}

/* Process group operations */
pid_t getpgrp(void)
{
    return (pid_t)__syscall1(SYS_GETPGID, 0);
}

int setpgid(pid_t pid, pid_t pgid)
{
    return (int)__syscall2(SYS_SETPGID, pid, pgid);
}

pid_t setsid(void)
{
    return (pid_t)__syscall1(SYS_SETSID, 0);
}

/* Pipe - not implemented yet */
int pipe(int pipefd[2])
{
    (void)pipefd;
    return -1; /* ENOSYS */
}

/* Execute functions - stubs for now */
int execv(const char *pathname, char *const argv[])
{
    (void)pathname;
    (void)argv;
    return -1; /* ENOSYS */
}

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    (void)pathname;
    (void)argv;
    (void)envp;
    return -1; /* ENOSYS */
}

int execvp(const char *file, char *const argv[])
{
    (void)file;
    (void)argv;
    return -1; /* ENOSYS */
}

/* Fork */
pid_t fork(void)
{
    return (pid_t)__syscall1(SYS_FORK, 0);
}

/* File operations - stubs */
int truncate(const char *path, long length)
{
    (void)path;
    (void)length;
    return -1; /* ENOSYS */
}

int ftruncate(int fd, long length)
{
    (void)fd;
    (void)length;
    return -1; /* ENOSYS */
}

/* fsd backend fsync - implemented in fsd_backend.cpp */
extern int __viper_fsd_is_fd(int fd);
extern int __viper_fsd_fsync(int fd);

#define SYS_FSYNC 0x49

int fsync(int fd)
{
    /* Route fsd-managed file descriptors through the fsd backend */
    if (__viper_fsd_is_fd(fd))
    {
        return __viper_fsd_fsync(fd);
    }
    /* For kernel-managed FDs, use the fsync syscall */
    long ret = __syscall1(SYS_FSYNC, fd);
    if (ret < 0)
    {
        return -1;
    }
    return 0;
}

long pathconf(const char *path, int name)
{
    (void)path;
    (void)name;
    return -1; /* ENOSYS */
}

long fpathconf(int fd, int name)
{
    (void)fd;
    (void)name;
    return -1; /* ENOSYS */
}

unsigned int alarm(unsigned int seconds)
{
    (void)seconds;
    return 0; /* Not implemented */
}

int pause(void)
{
    /* Block forever - in practice would wait for signal */
    __syscall1(SYS_SLEEP, 0x7FFFFFFF);
    return -1;
}
