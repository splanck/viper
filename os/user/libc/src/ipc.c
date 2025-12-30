/*
 * ViperOS libc - ipc.c
 * Inter-process communication implementation
 */

#include "../include/sys/ipc.h"
#include "../include/errno.h"
#include "../include/sys/stat.h"

/*
 * ftok - Generate IPC key from pathname and project ID
 *
 * This is a simple implementation that combines the inode number,
 * device number, and project ID to create a unique key.
 */
key_t ftok(const char *pathname, int proj_id)
{
    struct stat st;

    if (!pathname)
    {
        errno = EINVAL;
        return (key_t)-1;
    }

    if (stat(pathname, &st) < 0)
    {
        /* errno set by stat() */
        return (key_t)-1;
    }

    /*
     * Generate key by combining:
     * - Lower 8 bits of proj_id
     * - Lower 8 bits of device number
     * - Lower 16 bits of inode number
     */
    return (key_t)(((proj_id & 0xFF) << 24) | ((st.st_dev & 0xFF) << 16) | (st.st_ino & 0xFFFF));
}
