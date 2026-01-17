//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: tests/common/PosixCompat.h
// Purpose: Cross-platform POSIX function compatibility for C tests.
//          Include this instead of <unistd.h> in C test files.
//===----------------------------------------------------------------------===//
#ifndef VIPER_TESTS_POSIX_COMPAT_H
#define VIPER_TESTS_POSIX_COMPAT_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h> // For printf in skip macro
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h> // For struct timespec (defined in UCRT)
#include <windows.h>

// Standard file descriptors
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

// File type macros from POSIX stat
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

// usleep replacement using Windows Sleep (note: Sleep takes milliseconds)
static inline int usleep(unsigned int usec)
{
    Sleep(usec / 1000);
    return 0;
}

// nanosleep replacement using Windows Sleep
// Note: struct timespec is defined in UCRT's time.h
static inline int nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    DWORD ms = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    Sleep(ms);
    return 0;
}

// Define M_PI if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Map POSIX functions to Windows equivalents
// Note: Using macros for these is problematic in C++ because they interfere
// with member functions like std::ofstream::close(). We provide inline
// wrapper functions instead for C++, and macros for C.
#ifdef __cplusplus
// C++ inline wrappers that don't conflict with member functions
static inline int posix_close(int fd)
{
    return _close(fd);
}

static inline int posix_read(int fd, void *buf, unsigned int count)
{
    return _read(fd, buf, count);
}

static inline int posix_write(int fd, const void *buf, unsigned int count)
{
    return _write(fd, buf, count);
}

static inline int posix_unlink(const char *path)
{
    return _unlink(path);
}

static inline int posix_rmdir(const char *path)
{
    return _rmdir(path);
}

static inline char *posix_getcwd(char *buf, int size)
{
    return _getcwd(buf, size);
}

static inline int posix_chdir(const char *path)
{
    return _chdir(path);
}

static inline int posix_access(const char *path, int mode)
{
    return _access(path, mode);
}

static inline int posix_isatty(int fd)
{
    return _isatty(fd);
}

static inline int posix_fileno(FILE *stream)
{
    return _fileno(stream);
}

static inline int posix_dup(int fd)
{
    return _dup(fd);
}

static inline int posix_dup2(int fd1, int fd2)
{
    return _dup2(fd1, fd2);
}

static inline int posix_getpid()
{
    return _getpid();
}

// Only define macros for functions that don't conflict with C++ (no common member function names)
// Note: NOT defining 'pipe' as a macro because it conflicts with std::pair initialization syntax
#define unlink _unlink
#define rmdir _rmdir
#define getcwd _getcwd
#define chdir _chdir
#define access _access
#define isatty _isatty
#define fileno _fileno
#define getpid _getpid

static inline int posix_pipe(int fds[2])
{
    return _pipe(fds, 4096, _O_BINARY);
}

// Standalone pipe function for C++ (don't use macro to avoid std::pair conflicts)
#ifndef pipe
static inline int pipe(int fds[2])
{
    return _pipe(fds, 4096, _O_BINARY);
}
#endif

// setenv/unsetenv replacements for Windows
static inline int setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && getenv(name) != NULL)
        return 0;
    return _putenv_s(name, value);
}

static inline int unsetenv(const char *name)
{
    return _putenv_s(name, "");
}
#else
// C macros
#define close _close
#define read _read
#define write _write
#define unlink _unlink
#define rmdir _rmdir
#define getcwd _getcwd
#define chdir _chdir
#define access _access
#define isatty _isatty
#define fileno _fileno
#define dup _dup
#define dup2 _dup2
#define getpid _getpid
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif

// Access mode macros
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef F_OK
#define F_OK 0
#endif

// ssize_t type
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

// mkstemp implementation for Windows
static inline int mkstemp(char *tpl)
{
    if (_mktemp_s(tpl, strlen(tpl) + 1) != 0)
        return -1;
    int fd = -1;
    if (_sopen_s(
            &fd, tpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE) !=
        0)
        return -1;
    return fd;
}

// pid_t type for Windows (needed for fork-based tests)
#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif

// fork() doesn't exist on Windows - tests using fork should skip
// Use SKIP_TEST_NO_FORK() at the start of main() for fork-based tests
#define VIPER_NO_FORK 1
// SKIP_TEST_NO_FORK returns early and prints skip message
// The rest of main() won't execute on Windows, which may cause unreachable code warnings
#define SKIP_TEST_NO_FORK()                                                                        \
    do                                                                                             \
    {                                                                                              \
        printf("Test skipped: fork() not available on Windows\n");                                 \
        return 0;                                                                                  \
    } while (0)

// Stub for fork (always returns error to force skip path)
static inline pid_t fork(void)
{
    return -1;
}

// Stub for waitpid (no-op on Windows)
static inline pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)status;
    (void)options;
    return -1;
}

// _exit mapping
#define _exit(code) exit(code)

#else
// POSIX systems
#include <unistd.h>
#define VIPER_NO_FORK 0
#define SKIP_TEST_NO_FORK()                                                                        \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

#endif // VIPER_TESTS_POSIX_COMPAT_H
