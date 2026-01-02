//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/ipc.c
// Purpose: System V IPC key generation for ViperOS libc.
// Key invariants: Key derived from file inode, device, and project ID.
// Ownership/Lifetime: Library; stateless function.
// Links: user/libc/include/sys/ipc.h
//
//===----------------------------------------------------------------------===//

/**
 * @file ipc.c
 * @brief System V IPC key generation for ViperOS libc.
 *
 * @details
 * This file implements System V IPC key generation:
 *
 * - ftok: Generate IPC key from pathname and project ID
 *
 * The ftok() function generates a key suitable for use with
 * msgget(), semget(), and shmget(). The key is computed from
 * the file's inode number, device number, and a project ID.
 * This ensures different files or project IDs produce unique keys.
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
