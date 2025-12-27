#include "../include/unistd.h"

/* Syscall helpers - defined in syscall.S */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Syscall numbers from viperos/syscall_nums.hpp */
#define SYS_OPEN 0x40
#define SYS_CLOSE 0x41
#define SYS_READ 0x42
#define SYS_WRITE 0x43
#define SYS_LSEEK 0x44
#define SYS_DUP 0x47
#define SYS_DUP2 0x48
#define SYS_SBRK 0x0A
#define SYS_SLEEP 0x31

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
