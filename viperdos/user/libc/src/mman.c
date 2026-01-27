//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/mman.c
// Purpose: Memory mapping functions for ViperDOS libc.
// Key invariants: Page-aligned mappings; kernel manages VMAs.
// Ownership/Lifetime: Library; mappings persist until unmapped.
// Links: user/libc/include/sys/mman.h
//
//===----------------------------------------------------------------------===//

/**
 * @file mman.c
 * @brief Memory mapping functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX memory mapping functions:
 *
 * - mmap/munmap: Map/unmap memory regions
 * - mprotect: Change memory protection
 * - msync: Synchronize mapped memory with backing store
 * - madvise/posix_madvise: Advise kernel about memory usage
 * - mlock/munlock: Lock/unlock memory pages
 * - shm_open/shm_unlink: Shared memory (not implemented)
 *
 * Memory mappings are managed by the kernel's virtual memory system.
 * All mappings must be page-aligned (4KB on ViperDOS).
 */

#include "../include/sys/mman.h"
#include "../include/errno.h"
#include "../include/fcntl.h"

/* Syscall helpers */
extern long __syscall3(long num, long arg0, long arg1, long arg2);
extern long __syscall6(long num, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5);

/* Syscall numbers (0x150 block) */
#define SYS_MMAP 0x150
#define SYS_MUNMAP 0x151
#define SYS_MPROTECT 0x152
#define SYS_MSYNC 0x153
#define SYS_MADVISE 0x154
#define SYS_MLOCK 0x155
#define SYS_MUNLOCK 0x156

/**
 * @brief Map files or devices into memory.
 *
 * @details
 * Creates a new mapping in the virtual address space of the calling process.
 * The mapping can be backed by a file or be anonymous (not backed by any file).
 *
 * Protection flags (prot):
 * - PROT_NONE: Pages cannot be accessed
 * - PROT_READ: Pages can be read
 * - PROT_WRITE: Pages can be written
 * - PROT_EXEC: Pages can be executed
 *
 * Mapping flags (flags):
 * - MAP_SHARED: Updates visible to other processes mapping the same region
 * - MAP_PRIVATE: Copy-on-write private mapping
 * - MAP_FIXED: Place mapping at exactly the specified address
 * - MAP_ANONYMOUS: Not backed by any file (fd is ignored)
 *
 * For file mappings, the fd argument is an open file descriptor. The offset
 * argument specifies where in the file the mapping begins.
 *
 * @param addr Preferred address for the mapping (or NULL for kernel choice).
 * @param length Length of the mapping in bytes (rounded up to page size).
 * @param prot Memory protection (PROT_READ, PROT_WRITE, PROT_EXEC).
 * @param flags Mapping type and options (MAP_SHARED, MAP_PRIVATE, etc.).
 * @param fd File descriptor for file mapping (ignored if MAP_ANONYMOUS).
 * @param offset Offset in the file (must be page-aligned).
 * @return Pointer to mapped area on success, or MAP_FAILED on error.
 *
 * @see munmap, mprotect, msync
 */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    long result = __syscall6(
        SYS_MMAP, (long)addr, (long)length, (long)prot, (long)flags, (long)fd, (long)offset);
    if (result < 0 && result > -4096) {
        errno = (int)(-result);
        return MAP_FAILED;
    }
    return (void *)result;
}

/**
 * @brief Unmap a mapped memory region.
 *
 * @details
 * Removes the mapping for the specified address range. After this call,
 * references to addresses within the range will generate SIGSEGV.
 *
 * The addr and length should match values returned by or used with mmap().
 * All pages containing any part of the indicated range are unmapped.
 *
 * If the region contains modified pages of a MAP_SHARED mapping backed
 * by a file, those modifications are written to the file (as if msync()
 * had been called).
 *
 * @param addr Start address of the region to unmap (must be page-aligned).
 * @param length Length of the region in bytes.
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see mmap, msync
 */
int munmap(void *addr, size_t length) {
    long result = __syscall3(SYS_MUNMAP, (long)addr, (long)length, 0);
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/**
 * @brief Change memory protection of a region.
 *
 * @details
 * Changes the access protections for the memory pages containing any
 * part of the address range [addr, addr+length). The addr must be
 * page-aligned.
 *
 * Protection flags (prot):
 * - PROT_NONE: No access allowed
 * - PROT_READ: Read access allowed
 * - PROT_WRITE: Write access allowed
 * - PROT_EXEC: Execute access allowed
 *
 * If an access is attempted that violates the protection, SIGSEGV
 * is generated.
 *
 * @param addr Start address of the region (must be page-aligned).
 * @param length Length of the region in bytes.
 * @param prot New protection flags.
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see mmap, munmap
 */
int mprotect(void *addr, size_t length, int prot) {
    long result = __syscall3(SYS_MPROTECT, (long)addr, (long)length, (long)prot);
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/**
 * @brief Synchronize a memory-mapped region with its backing store.
 *
 * @details
 * Flushes changes made to a file-backed memory mapping back to the
 * underlying file. This ensures that modifications are visible to
 * other processes reading the file.
 *
 * Synchronization flags:
 * - MS_ASYNC: Schedule the write but return immediately
 * - MS_SYNC: Block until the write is complete
 * - MS_INVALIDATE: Invalidate other mappings of the same file
 *
 * For anonymous mappings (MAP_ANONYMOUS), msync() has no effect.
 *
 * @param addr Start address of the region (must be page-aligned).
 * @param length Length of the region in bytes.
 * @param flags Synchronization flags (MS_ASYNC, MS_SYNC, MS_INVALIDATE).
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see mmap, munmap
 */
int msync(void *addr, size_t length, int flags) {
    long result = __syscall3(SYS_MSYNC, (long)addr, (long)length, (long)flags);
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/**
 * @brief Advise the kernel about expected memory usage.
 *
 * @details
 * Provides hints to the kernel about how the specified memory region
 * will be used. The kernel may use this to optimize paging and caching.
 *
 * Common advice values:
 * - MADV_NORMAL: Default behavior (moderate read-ahead)
 * - MADV_RANDOM: Random access expected (reduce read-ahead)
 * - MADV_SEQUENTIAL: Sequential access expected (increase read-ahead)
 * - MADV_WILLNEED: Will need this data soon (trigger read-ahead)
 * - MADV_DONTNEED: Won't need this data soon (may drop pages)
 *
 * @param addr Start address of the region (should be page-aligned).
 * @param length Length of the region in bytes.
 * @param advice Memory usage hint (MADV_NORMAL, MADV_RANDOM, etc.).
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see posix_madvise, mmap
 */
int madvise(void *addr, size_t length, int advice) {
    long result = __syscall3(SYS_MADVISE, (long)addr, (long)length, (long)advice);
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/**
 * @brief POSIX-compliant memory advice.
 *
 * @details
 * Like madvise(), but with POSIX-specified behavior. The main difference
 * is the return value: posix_madvise() returns the error code directly
 * (not -1 with errno), and only supports POSIX-defined advice values.
 *
 * POSIX advice values:
 * - POSIX_MADV_NORMAL: Default behavior
 * - POSIX_MADV_RANDOM: Random access expected
 * - POSIX_MADV_SEQUENTIAL: Sequential access expected
 * - POSIX_MADV_WILLNEED: Will need this data soon
 * - POSIX_MADV_DONTNEED: Won't need this data soon
 *
 * @param addr Start address of the region.
 * @param length Length of the region in bytes.
 * @param advice Memory usage hint.
 * @return 0 on success, error code on failure (not -1).
 *
 * @see madvise
 */
int posix_madvise(void *addr, size_t length, int advice) {
    /* Same as madvise but returns error code instead of -1 */
    long result = __syscall3(SYS_MADVISE, (long)addr, (long)length, (long)advice);
    if (result < 0) {
        return (int)(-result);
    }
    return 0;
}

/**
 * @brief Lock memory pages in RAM.
 *
 * @details
 * Locks the specified range of virtual address space into RAM, preventing
 * it from being paged out to swap. This is useful for real-time applications
 * or security-sensitive data that should not be written to disk.
 *
 * Locked pages remain in memory until explicitly unlocked with munlock()
 * or munlockall(), or until the process exits.
 *
 * @note Resource limits may restrict the amount of memory that can be locked.
 *
 * @param addr Start address of the region (should be page-aligned).
 * @param length Length of the region in bytes.
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see munlock, mlockall, munlockall
 */
int mlock(const void *addr, size_t length) {
    long result = __syscall3(SYS_MLOCK, (long)addr, (long)length, 0);
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/**
 * @brief Unlock memory pages.
 *
 * @details
 * Unlocks the specified range of virtual address space, allowing the
 * kernel to page it out to swap if necessary. This reverses the effect
 * of a previous mlock() call.
 *
 * @param addr Start address of the region.
 * @param length Length of the region in bytes.
 * @return 0 on success, -1 on error (sets errno).
 *
 * @see mlock, mlockall, munlockall
 */
int munlock(const void *addr, size_t length) {
    long result = __syscall3(SYS_MUNLOCK, (long)addr, (long)length, 0);
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return 0;
}

/**
 * @brief Lock all pages of the process address space.
 *
 * @details
 * Locks all pages mapped into the process address space into RAM.
 * This includes code, data, stack, shared libraries, and memory mappings.
 *
 * Flags:
 * - MCL_CURRENT: Lock all pages currently mapped
 * - MCL_FUTURE: Lock all pages that become mapped in the future
 *
 * @note Not implemented in ViperDOS.
 *
 * @param flags Locking flags (MCL_CURRENT, MCL_FUTURE, or both).
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see munlockall, mlock, munlock
 */
int mlockall(int flags) {
    (void)flags;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Unlock all pages of the process address space.
 *
 * @details
 * Unlocks all pages in the process address space, reversing the effect
 * of mlockall() or multiple mlock() calls.
 *
 * @note Not implemented in ViperDOS.
 *
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see mlockall, mlock, munlock
 */
int munlockall(void) {
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Determine whether pages are resident in memory.
 *
 * @details
 * Returns a vector indicating whether each page of the specified region
 * is currently resident in RAM (1) or not (0). This can be used to
 * determine if accessing a page will cause a page fault.
 *
 * The vec array must be large enough to hold one byte per page in the
 * specified region.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param addr Start address of the region (must be page-aligned).
 * @param length Length of the region in bytes.
 * @param vec Array to receive residency status (1 byte per page).
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see mlock, madvise
 */
int mincore(void *addr, size_t length, unsigned char *vec) {
    (void)addr;
    (void)length;
    (void)vec;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Open a POSIX shared memory object.
 *
 * @details
 * Creates or opens a named shared memory object. The object can be used
 * with mmap() to create shared mappings between processes. Unlike files,
 * shared memory objects reside in memory and are not backed by a filesystem.
 *
 * The name should begin with a slash and contain no other slashes.
 *
 * Opening flags (oflag):
 * - O_RDONLY: Open for reading only
 * - O_RDWR: Open for reading and writing
 * - O_CREAT: Create if it doesn't exist
 * - O_EXCL: Fail if it already exists (with O_CREAT)
 * - O_TRUNC: Truncate to zero length
 *
 * @note Not implemented in ViperDOS.
 *
 * @param name Name of the shared memory object (must start with '/').
 * @param oflag Opening flags (O_RDONLY, O_RDWR, O_CREAT, etc.).
 * @param mode Permission mode if creating (modified by umask).
 * @return File descriptor on success, -1 on error (sets errno to ENOSYS).
 *
 * @see shm_unlink, mmap
 */
int shm_open(const char *name, int oflag, mode_t mode) {
    (void)name;
    (void)oflag;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Remove a POSIX shared memory object.
 *
 * @details
 * Removes a named shared memory object. The object is not actually deleted
 * until all processes that have it mapped call munmap() or exit.
 *
 * After shm_unlink(), the name is removed and new shm_open() calls will
 * not find the object, but existing mappings remain valid.
 *
 * @note Not implemented in ViperDOS.
 *
 * @param name Name of the shared memory object to remove.
 * @return 0 on success, -1 on error (sets errno to ENOSYS).
 *
 * @see shm_open, munmap
 */
int shm_unlink(const char *name) {
    (void)name;
    errno = ENOSYS;
    return -1;
}
