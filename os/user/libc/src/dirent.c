#include "../include/dirent.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall2(long num, long arg0, long arg1);
extern long __syscall3(long num, long arg0, long arg1, long arg2);

/* Syscall numbers from include/viperos/syscall_nums.hpp */
#define SYS_OPEN 0x40
#define SYS_CLOSE 0x41
#define SYS_READDIR 0x60

/* Open flags */
#define O_RDONLY 0x0000

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
    long fd = __syscall2(SYS_OPEN, (long)name, O_RDONLY);
    if (fd < 0)
        return NULL;

    /* Allocate DIR structure */
    struct _DIR *dir = alloc_dir();
    if (!dir)
    {
        __syscall2(SYS_CLOSE, fd, 0);
        return NULL;
    }

    dir->fd = (int)fd;
    dir->buf_pos = 0;
    dir->buf_len = 0;
    memset(&dir->entry, 0, sizeof(dir->entry));

    return dir;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp)
        return NULL;

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

    long result = __syscall2(SYS_CLOSE, dirp->fd, 0);
    free_dir(dirp);

    return (result < 0) ? -1 : 0;
}

void rewinddir(DIR *dirp)
{
    if (!dirp)
        return;

    /* Close and reopen to reset position */
    /* This is a simple implementation - real systems track the path */
    dirp->buf_pos = 0;
    dirp->buf_len = 0;
}

int dirfd(DIR *dirp)
{
    if (!dirp)
        return -1;
    return dirp->fd;
}
