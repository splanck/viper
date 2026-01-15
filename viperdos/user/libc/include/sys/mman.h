/*
 * ViperDOS libc - sys/mman.h
 * Memory management declarations
 */

#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include "../stddef.h"
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Protection flags */
#define PROT_NONE 0x00  /* No access */
#define PROT_READ 0x01  /* Pages can be read */
#define PROT_WRITE 0x02 /* Pages can be written */
#define PROT_EXEC 0x04  /* Pages can be executed */

/* Map flags */
#define MAP_SHARED 0x01    /* Share changes */
#define MAP_PRIVATE 0x02   /* Changes are private */
#define MAP_FIXED 0x10     /* Interpret addr exactly */
#define MAP_ANONYMOUS 0x20 /* Don't use a file */
#define MAP_ANON MAP_ANONYMOUS

/* Additional map flags (may not all be supported) */
#define MAP_GROWSDOWN 0x0100 /* Stack-like segment */
#define MAP_DENYWRITE 0x0800 /* Deny write access */
#define MAP_LOCKED 0x2000    /* Lock pages in memory */
#define MAP_NORESERVE 0x4000 /* Don't reserve swap space */
#define MAP_POPULATE 0x8000  /* Populate page tables */
#define MAP_NONBLOCK 0x10000 /* Do not block on IO */
#define MAP_STACK 0x20000    /* Stack allocation */
#define MAP_HUGETLB 0x40000  /* Use huge pages */

/* Failed mapping return value */
#define MAP_FAILED ((void *)-1)

/* msync flags */
#define MS_ASYNC 1      /* Sync memory asynchronously */
#define MS_SYNC 4       /* Synchronous memory sync */
#define MS_INVALIDATE 2 /* Invalidate caches */

/* madvise advice values */
#define MADV_NORMAL 0     /* No special treatment */
#define MADV_RANDOM 1     /* Expect random page references */
#define MADV_SEQUENTIAL 2 /* Expect sequential page references */
#define MADV_WILLNEED 3   /* Will need these pages */
#define MADV_DONTNEED 4   /* Don't need these pages */
#define MADV_FREE 8       /* Free pages only if memory pressure */

/* posix_madvise flags */
#define POSIX_MADV_NORMAL MADV_NORMAL
#define POSIX_MADV_RANDOM MADV_RANDOM
#define POSIX_MADV_SEQUENTIAL MADV_SEQUENTIAL
#define POSIX_MADV_WILLNEED MADV_WILLNEED
#define POSIX_MADV_DONTNEED MADV_DONTNEED

/* mlock flags */
#define MCL_CURRENT 1 /* Lock all current mappings */
#define MCL_FUTURE 2  /* Lock all future mappings */
#define MCL_ONFAULT 4 /* Lock pages as they are faulted in */

    /*
     * mmap - Map files or devices into memory
     */
    void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

    /*
     * munmap - Unmap a mapped region
     */
    int munmap(void *addr, size_t length);

    /*
     * mprotect - Set protection on a region of memory
     */
    int mprotect(void *addr, size_t length, int prot);

    /*
     * msync - Synchronize a mapped region
     */
    int msync(void *addr, size_t length, int flags);

    /*
     * madvise - Give advice about use of memory
     */
    int madvise(void *addr, size_t length, int advice);

    /*
     * posix_madvise - POSIX memory advice
     */
    int posix_madvise(void *addr, size_t length, int advice);

    /*
     * mlock - Lock a range of memory
     */
    int mlock(const void *addr, size_t length);

    /*
     * munlock - Unlock a range of memory
     */
    int munlock(const void *addr, size_t length);

    /*
     * mlockall - Lock all pages of a process
     */
    int mlockall(int flags);

    /*
     * munlockall - Unlock all pages of a process
     */
    int munlockall(void);

    /*
     * mincore - Determine whether pages are resident in memory
     */
    int mincore(void *addr, size_t length, unsigned char *vec);

    /*
     * shm_open - Open a shared memory object
     */
    int shm_open(const char *name, int oflag, mode_t mode);

    /*
     * shm_unlink - Remove a shared memory object
     */
    int shm_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MMAN_H */
