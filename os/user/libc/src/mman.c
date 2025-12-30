/*
 * ViperOS libc - mman.c
 * Memory management implementation
 */

#include "../include/sys/mman.h"
#include "../include/errno.h"
#include "../include/fcntl.h"

/* Syscall helpers */
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall6(long num, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5);

/* Syscall numbers */
#define SYS_MMAP 0xE0
#define SYS_MUNMAP 0xE1
#define SYS_MPROTECT 0xE2
#define SYS_MSYNC 0xE3
#define SYS_MADVISE 0xE4
#define SYS_MLOCK 0xE5
#define SYS_MUNLOCK 0xE6

/*
 * mmap - Map files or devices into memory
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    long result = __syscall6(
        SYS_MMAP, (long)addr, (long)length, (long)prot, (long)flags, (long)fd, (long)offset);
    if (result < 0 && result > -4096)
    {
        errno = (int)(-result);
        return MAP_FAILED;
    }
    return (void *)result;
}

/*
 * munmap - Unmap a mapped region
 */
int munmap(void *addr, size_t length)
{
    long result = __syscall3(SYS_MUNMAP, (long)addr, (long)length, 0);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/*
 * mprotect - Set protection on a region of memory
 */
int mprotect(void *addr, size_t length, int prot)
{
    long result = __syscall3(SYS_MPROTECT, (long)addr, (long)length, (long)prot);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/*
 * msync - Synchronize a mapped region
 */
int msync(void *addr, size_t length, int flags)
{
    long result = __syscall3(SYS_MSYNC, (long)addr, (long)length, (long)flags);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/*
 * madvise - Give advice about use of memory
 */
int madvise(void *addr, size_t length, int advice)
{
    long result = __syscall3(SYS_MADVISE, (long)addr, (long)length, (long)advice);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/*
 * posix_madvise - POSIX memory advice
 */
int posix_madvise(void *addr, size_t length, int advice)
{
    /* Same as madvise but returns error code instead of -1 */
    long result = __syscall3(SYS_MADVISE, (long)addr, (long)length, (long)advice);
    if (result < 0)
    {
        return (int)(-result);
    }
    return 0;
}

/*
 * mlock - Lock a range of memory
 */
int mlock(const void *addr, size_t length)
{
    long result = __syscall3(SYS_MLOCK, (long)addr, (long)length, 0);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/*
 * munlock - Unlock a range of memory
 */
int munlock(const void *addr, size_t length)
{
    long result = __syscall3(SYS_MUNLOCK, (long)addr, (long)length, 0);
    if (result < 0)
    {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/*
 * mlockall - Lock all pages of a process
 */
int mlockall(int flags)
{
    (void)flags;
    errno = ENOSYS;
    return -1;
}

/*
 * munlockall - Unlock all pages of a process
 */
int munlockall(void)
{
    errno = ENOSYS;
    return -1;
}

/*
 * mincore - Determine whether pages are resident in memory
 */
int mincore(void *addr, size_t length, unsigned char *vec)
{
    (void)addr;
    (void)length;
    (void)vec;
    errno = ENOSYS;
    return -1;
}

/*
 * shm_open - Open a shared memory object
 */
int shm_open(const char *name, int oflag, mode_t mode)
{
    (void)name;
    (void)oflag;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

/*
 * shm_unlink - Remove a shared memory object
 */
int shm_unlink(const char *name)
{
    (void)name;
    errno = ENOSYS;
    return -1;
}
