#include "../include/unistd.h"

/* Syscall helpers - defined in syscall.S */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Syscall numbers from viperos/syscall_nums.hpp */
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

ssize_t read(int fd, void *buf, size_t count)
{
    return __syscall3(SYS_READ, fd, (long)buf, (long)count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return __syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

int close(int fd)
{
    return (int)__syscall1(SYS_CLOSE, fd);
}

long lseek(int fd, long offset, int whence)
{
    return __syscall3(SYS_LSEEK, fd, offset, whence);
}

int dup(int oldfd)
{
    return (int)__syscall1(SYS_DUP, oldfd);
}

int dup2(int oldfd, int newfd)
{
    return (int)__syscall2(SYS_DUP2, oldfd, newfd);
}

void *sbrk(long increment)
{
    long result = __syscall1(SYS_SBRK, increment);
    if (result < 0)
    {
        return (void *)-1;
    }
    return (void *)result;
}

unsigned int sleep(unsigned int seconds)
{
    __syscall1(SYS_SLEEP, seconds * 1000);
    return 0;
}

int usleep(useconds_t usec)
{
    /* Convert microseconds to milliseconds (rounded up) */
    unsigned long ms = (usec + 999) / 1000;
    if (ms == 0 && usec > 0)
        ms = 1;
    __syscall1(SYS_SLEEP, ms);
    return 0;
}

pid_t getpid(void)
{
    return (pid_t)__syscall1(SYS_TASK_CURRENT, 0);
}

pid_t getppid(void)
{
    /* ViperOS doesn't track parent process yet, return 1 (init) */
    return 1;
}

char *getcwd(char *buf, size_t size)
{
    long result = __syscall2(SYS_GETCWD, (long)buf, (long)size);
    if (result < 0)
    {
        return (char *)0;
    }
    return buf;
}

int chdir(const char *path)
{
    return (int)__syscall1(SYS_CHDIR, (long)path);
}

int isatty(int fd)
{
    /* stdin, stdout, stderr are terminals */
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

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
    /* ViperOS doesn't have full permission model yet */
    (void)mode;
    long result = __syscall2(SYS_STAT, (long)pathname, 0);
    return (result < 0) ? -1 : 0;
}

int unlink(const char *pathname)
{
    return (int)__syscall1(SYS_UNLINK, (long)pathname);
}

int rmdir(const char *pathname)
{
    return (int)__syscall1(SYS_RMDIR, (long)pathname);
}

int mkdir(const char *pathname, unsigned int mode)
{
    (void)mode; /* ViperOS doesn't use mode yet */
    return (int)__syscall1(SYS_MKDIR, (long)pathname);
}

int link(const char *oldpath, const char *newpath)
{
    /* Hard links not implemented yet */
    (void)oldpath;
    (void)newpath;
    return -1; /* ENOSYS */
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
static char hostname_buf[256] = "viperos";

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

/* User/group IDs - ViperOS is single-user, always return 0 (root) */
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

int fsync(int fd)
{
    (void)fd;
    return 0; /* Pretend to succeed */
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
