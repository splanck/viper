//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/dirent.c
// Purpose: Directory entry functions for ViperOS libc.
// Key invariants: Static pool of MAX_DIRS open directories; fsd routing.
// Ownership/Lifetime: Library; DIR handles from static pool.
// Links: user/libc/include/dirent.h
//
//===----------------------------------------------------------------------===//

/**
 * @file dirent.c
 * @brief Directory entry functions for ViperOS libc.
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

/* Syscall numbers from include/viperos/syscall_nums.hpp */
#define SYS_READDIR 0x60

/* libc ↔ fsd bridge */
extern int __viper_fsd_is_fd(int fd);
extern int __viper_fsd_readdir(int fd, struct dirent *out_ent);

/* Internal directory stream structure */
struct _DIR
{
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
static struct _DIR *alloc_dir(void)
{
    for (int i = 0; i < MAX_DIRS; i++)
    {
        if (!dir_pool_used[i])
        {
            dir_pool_used[i] = 1;
            return &dir_pool[i];
        }
    }
    return NULL;
}

/* Free a DIR back to the pool */
static void free_dir(struct _DIR *dir)
{
    for (int i = 0; i < MAX_DIRS; i++)
    {
        if (&dir_pool[i] == dir)
        {
            dir_pool_used[i] = 0;
            return;
        }
    }
}

DIR *opendir(const char *name)
{
    if (!name)
        return NULL;

    /* Open the directory */
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL;

    /* Allocate DIR structure */
    struct _DIR *dir = alloc_dir();
    if (!dir)
    {
        close(fd);
        return NULL;
    }

    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_len = 0;
    memset(&dir->entry, 0, sizeof(dir->entry));

    return dir;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp)
        return NULL;

    if (__viper_fsd_is_fd(dirp->fd))
    {
        int rc = __viper_fsd_readdir(dirp->fd, &dirp->entry);
        return (rc > 0) ? &dirp->entry : NULL;
    }

    /* If buffer is empty or exhausted, read more */
    if (dirp->buf_pos >= dirp->buf_len)
    {
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

int closedir(DIR *dirp)
{
    if (!dirp)
        return -1;

    long result = close(dirp->fd);
    free_dir(dirp);

    return (result < 0) ? -1 : 0;
}

void rewinddir(DIR *dirp)
{
    if (!dirp)
        return;

    if (__viper_fsd_is_fd(dirp->fd))
    {
        (void)lseek(dirp->fd, 0, SEEK_SET);
        return;
    }

    dirp->buf_pos = 0;
    dirp->buf_len = 0;
}

int dirfd(DIR *dirp)
{
    if (!dirp)
        return -1;
    return dirp->fd;
}
