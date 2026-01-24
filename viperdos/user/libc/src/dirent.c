//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/dirent.c
// Purpose: Directory entry functions for ViperDOS libc.
// Key invariants: Static pool of MAX_DIRS open directories; fsd routing.
// Ownership/Lifetime: Library; DIR handles from static pool.
// Links: user/libc/include/dirent.h
//
//===----------------------------------------------------------------------===//

/**
 * @file dirent.c
 * @brief Directory entry functions for ViperDOS libc.
 *
 * @details
 * This file implements POSIX directory traversal functions:
 *
 * - opendir: Open a directory stream
 * - readdir: Read directory entries
 * - closedir: Close directory stream
 * - rewinddir: Reset directory stream position
 * - dirfd: Get file descriptor for directory
 *
 * Directory operations are routed through either the kernel VFS or
 * the fsd (filesystem daemon) depending on the path. A static pool
 * of DIR structures is used to avoid dynamic allocation.
 */

#include "../include/dirent.h"
#include "../include/fcntl.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* Syscall helpers */
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Syscall numbers from include/viperdos/syscall_nums.hpp */
#define SYS_READDIR 0x60

/* libc ↔ fsd bridge */
extern int __viper_fsd_is_fd(int fd);
extern int __viper_fsd_readdir(int fd, struct dirent *out_ent);

/* Internal directory stream structure */
struct _DIR {
    int fd;              /* File descriptor for the directory */
    char buffer[2048];   /* Buffer for directory entries */
    int buf_pos;         /* Current position in buffer */
    int buf_len;         /* Amount of data in buffer */
    struct dirent entry; /* Current entry for readdir return */
};

/* Maximum number of open directories (static pool) */
#define MAX_DIRS 8
static struct _DIR dir_pool[MAX_DIRS];
static int dir_pool_used[MAX_DIRS] = {0};

/* Allocate a DIR from the pool */
static struct _DIR *alloc_dir(void) {
    for (int i = 0; i < MAX_DIRS; i++) {
        if (!dir_pool_used[i]) {
            dir_pool_used[i] = 1;
            return &dir_pool[i];
        }
    }
    return NULL;
}

/* Free a DIR back to the pool */
static void free_dir(struct _DIR *dir) {
    for (int i = 0; i < MAX_DIRS; i++) {
        if (&dir_pool[i] == dir) {
            dir_pool_used[i] = 0;
            return;
        }
    }
}

/**
 * @brief Open a directory stream.
 *
 * @details
 * Opens a directory stream for the specified path, returning a DIR pointer
 * that can be used with readdir() to iterate over directory entries.
 *
 * ViperDOS uses a static pool of DIR structures (MAX_DIRS=8), so there
 * is a limit on the number of simultaneously open directories. The
 * directory is opened using the underlying file descriptor, which may
 * route through either the kernel VFS or the fsd (filesystem daemon)
 * depending on the path.
 *
 * @param name Path to the directory to open.
 * @return Pointer to DIR stream on success, or NULL on error.
 *
 * @note The returned DIR must be closed with closedir() when done.
 *
 * @see readdir, closedir, rewinddir
 */
DIR *opendir(const char *name) {
    if (!name)
        return NULL;

    /* Open the directory */
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL;

    /* Allocate DIR structure */
    struct _DIR *dir = alloc_dir();
    if (!dir) {
        close(fd);
        return NULL;
    }

    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_len = 0;
    memset(&dir->entry, 0, sizeof(dir->entry));

    return dir;
}

/**
 * @brief Read the next directory entry.
 *
 * @details
 * Reads the next entry from the directory stream and returns a pointer
 * to a dirent structure containing the entry information:
 *
 * - d_ino: Inode number of the entry
 * - d_type: Type of file (DT_REG, DT_DIR, DT_LNK, etc.)
 * - d_name: Null-terminated filename (up to NAME_MAX characters)
 *
 * The function reads entries from an internal buffer, refilling it
 * via syscall when necessary. For fsd-backed directories, entries
 * are read directly through the fsd interface.
 *
 * @warning The returned pointer points to internal storage that is
 * overwritten by subsequent calls to readdir() on the same stream.
 *
 * @param dirp Directory stream from opendir().
 * @return Pointer to dirent structure, or NULL on end-of-directory or error.
 *
 * @see opendir, closedir, rewinddir
 */
struct dirent *readdir(DIR *dirp) {
    if (!dirp)
        return NULL;

    if (__viper_fsd_is_fd(dirp->fd)) {
        int rc = __viper_fsd_readdir(dirp->fd, &dirp->entry);
        return (rc > 0) ? &dirp->entry : NULL;
    }

    /* If buffer is empty or exhausted, read more */
    if (dirp->buf_pos >= dirp->buf_len) {
        long result = __syscall3(SYS_READDIR, dirp->fd, (long)dirp->buffer, sizeof(dirp->buffer));
        if (result <= 0)
            return NULL;

        dirp->buf_len = (int)result;
        dirp->buf_pos = 0;
    }

    /* Parse the next entry from buffer */
    /* Buffer format is packed DirEnt structures from kernel:
     * u64 ino, u16 reclen, u8 type, u8 namelen, char name[256]
     */
    if (dirp->buf_pos >= dirp->buf_len)
        return NULL;

    char *ptr = dirp->buffer + dirp->buf_pos;

    /* Read ino (8 bytes) */
    dirp->entry.d_ino = *(unsigned long *)ptr;
    ptr += 8;

    /* Read reclen (2 bytes) */
    unsigned short reclen = *(unsigned short *)ptr;
    ptr += 2;

    /* Read type (1 byte) */
    dirp->entry.d_type = *ptr++;

    /* Read namelen (1 byte) */
    unsigned int namelen = (unsigned char)*ptr++;

    /* Copy name (namelen is u8 from kernel, max 255, which equals NAME_MAX) */
    memcpy(dirp->entry.d_name, ptr, namelen);
    dirp->entry.d_name[namelen] = '\0';

    /* Advance position */
    dirp->buf_pos += reclen;

    return &dirp->entry;
}

/**
 * @brief Close a directory stream.
 *
 * @details
 * Closes the directory stream, releases the underlying file descriptor,
 * and returns the DIR structure to the static pool for reuse.
 *
 * After this call, the DIR pointer is no longer valid and must not be
 * used with readdir() or other directory functions.
 *
 * @param dirp Directory stream to close.
 * @return 0 on success, -1 on error.
 *
 * @see opendir, readdir
 */
int closedir(DIR *dirp) {
    if (!dirp)
        return -1;

    long result = close(dirp->fd);
    free_dir(dirp);

    return (result < 0) ? -1 : 0;
}

/**
 * @brief Reset directory stream position to the beginning.
 *
 * @details
 * Resets the position of the directory stream to the first entry,
 * allowing the directory to be read again from the start. Any buffered
 * entries are discarded.
 *
 * For fsd-backed directories, this performs an lseek() on the underlying
 * file descriptor. For kernel VFS directories, it clears the internal
 * buffer.
 *
 * @param dirp Directory stream to rewind.
 *
 * @see opendir, readdir
 */
void rewinddir(DIR *dirp) {
    if (!dirp)
        return;

    if (__viper_fsd_is_fd(dirp->fd)) {
        (void)lseek(dirp->fd, 0, SEEK_SET);
        return;
    }

    dirp->buf_pos = 0;
    dirp->buf_len = 0;
}

/**
 * @brief Get the file descriptor associated with a directory stream.
 *
 * @details
 * Returns the underlying file descriptor associated with the directory
 * stream. This can be used for operations that require a file descriptor
 * (such as fstat() or fchdir()).
 *
 * The file descriptor remains valid until closedir() is called. It should
 * not be closed separately, as closedir() will close it.
 *
 * @param dirp Directory stream.
 * @return File descriptor on success, -1 if dirp is NULL.
 *
 * @see opendir, fchdir
 */
int dirfd(DIR *dirp) {
    if (!dirp)
        return -1;
    return dirp->fd;
}
