//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/ftw.c
// Purpose: File tree walking functions for ViperOS libc.
// Key invariants: Recursive directory traversal; caller callback at each node.
// Ownership/Lifetime: Library; allocates cwd buffer during FTW_CHDIR.
// Links: user/libc/include/ftw.h
//
//===----------------------------------------------------------------------===//

/**
 * @file ftw.c
 * @brief File tree walking functions for ViperOS libc.
 *
 * @details
 * This file implements POSIX file tree walk functions:
 *
 * - ftw: Walk a file tree with callback
 * - nftw: Extended file tree walk with more options
 *
 * Both functions recursively traverse a directory tree, calling a
 * user-provided function for each file and directory encountered.
 * The nftw() version supports additional flags:
 * - FTW_DEPTH: Call function for directory after its contents
 * - FTW_PHYS: Do not follow symlinks (use lstat)
 * - FTW_CHDIR: Change to each directory during traversal
 * - FTW_MOUNT: Stay on same filesystem (not implemented)
 */

#include "../include/ftw.h"
#include "../include/dirent.h"
#include "../include/errno.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* Maximum path length */
#define PATH_MAX_FTW 4096

/*
 * ftw_walk - Internal recursive walker for ftw
 */
static int ftw_walk(const char *path,
                    int (*fn)(const char *, const struct stat *, int),
                    int nopenfd,
                    int depth)
{
    struct stat sb;
    int type;
    int result;
    DIR *dir;
    struct dirent *entry;

    (void)nopenfd; /* Not strictly enforced in this implementation */

    /* Get file info */
    if (lstat(path, &sb) != 0)
    {
        type = FTW_NS;
        /* Call function even on stat failure */
        return fn(path, &sb, type);
    }

    /* Determine type */
    if (S_ISDIR(sb.st_mode))
    {
        type = FTW_D;
    }
    else if (S_ISLNK(sb.st_mode))
    {
        type = FTW_SL;
    }
    else
    {
        type = FTW_F;
    }

    /* Call function for non-directory or pre-order directory */
    if (type != FTW_D)
    {
        return fn(path, &sb, type);
    }

    /* It's a directory - call function first (pre-order) */
    result = fn(path, &sb, type);
    if (result != 0)
    {
        return result;
    }

    /* Open directory */
    dir = opendir(path);
    if (!dir)
    {
        /* Can't read directory */
        return fn(path, &sb, FTW_DNR);
    }

    /* Walk directory entries */
    while ((entry = readdir(dir)) != NULL)
    {
        /* Skip . and .. */
        if (entry->d_name[0] == '.')
        {
            if (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))
            {
                continue;
            }
        }

        /* Build full path */
        char fullpath[PATH_MAX_FTW];
        size_t pathlen = strlen(path);
        size_t namelen = strlen(entry->d_name);

        if (pathlen + 1 + namelen >= PATH_MAX_FTW)
        {
            continue; /* Path too long, skip */
        }

        memcpy(fullpath, path, pathlen);
        if (pathlen > 0 && path[pathlen - 1] != '/')
        {
            fullpath[pathlen++] = '/';
        }
        memcpy(fullpath + pathlen, entry->d_name, namelen + 1);

        /* Recurse */
        result = ftw_walk(fullpath, fn, nopenfd, depth + 1);
        if (result != 0)
        {
            closedir(dir);
            return result;
        }
    }

    closedir(dir);
    return 0;
}

/*
 * ftw - File tree walk
 */
int ftw(const char *path,
        int (*fn)(const char *fpath, const struct stat *sb, int typeflag),
        int nopenfd)
{
    if (!path || !fn)
    {
        errno = EINVAL;
        return -1;
    }

    return ftw_walk(path, fn, nopenfd, 0);
}

/*
 * nftw_walk - Internal recursive walker for nftw
 */
static int nftw_walk(const char *path,
                     int (*fn)(const char *, const struct stat *, int, struct FTW *),
                     int nopenfd,
                     int flags,
                     int depth,
                     int base)
{
    struct stat sb;
    int type;
    int result;
    DIR *dir;
    struct dirent *entry;
    struct FTW ftwbuf;

    (void)nopenfd;

    ftwbuf.base = base;
    ftwbuf.level = depth;

    /* Get file info */
    int stat_result;
    if (flags & FTW_PHYS)
    {
        stat_result = lstat(path, &sb);
    }
    else
    {
        stat_result = stat(path, &sb);
    }

    if (stat_result != 0)
    {
        type = FTW_NS;
        return fn(path, &sb, type, &ftwbuf);
    }

    /* Determine type */
    if (S_ISDIR(sb.st_mode))
    {
        type = FTW_D;
    }
    else if (S_ISLNK(sb.st_mode))
    {
        /* Check if symlink target exists */
        struct stat target_sb;
        if (stat(path, &target_sb) != 0)
        {
            type = FTW_SLN;
        }
        else
        {
            type = FTW_SL;
        }
    }
    else
    {
        type = FTW_F;
    }

    /* Call function for non-directory */
    if (type != FTW_D)
    {
        return fn(path, &sb, type, &ftwbuf);
    }

    /* It's a directory */

    /* Pre-order: call function before descending */
    if (!(flags & FTW_DEPTH))
    {
        result = fn(path, &sb, FTW_D, &ftwbuf);
        if (result != 0)
        {
            return result;
        }
    }

    /* Open directory */
    dir = opendir(path);
    if (!dir)
    {
        return fn(path, &sb, FTW_DNR, &ftwbuf);
    }

    /* Change directory if requested */
    char *saved_cwd = NULL;
    if (flags & FTW_CHDIR)
    {
        saved_cwd = getcwd(NULL, 0);
        if (chdir(path) != 0)
        {
            free(saved_cwd);
            closedir(dir);
            return fn(path, &sb, FTW_DNR, &ftwbuf);
        }
    }

    /* Walk directory entries */
    while ((entry = readdir(dir)) != NULL)
    {
        /* Skip . and .. */
        if (entry->d_name[0] == '.')
        {
            if (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))
            {
                continue;
            }
        }

        /* Build full path */
        char fullpath[PATH_MAX_FTW];
        size_t pathlen = strlen(path);
        size_t namelen = strlen(entry->d_name);

        if (pathlen + 1 + namelen >= PATH_MAX_FTW)
        {
            continue;
        }

        memcpy(fullpath, path, pathlen);
        if (pathlen > 0 && path[pathlen - 1] != '/')
        {
            fullpath[pathlen++] = '/';
        }
        memcpy(fullpath + pathlen, entry->d_name, namelen + 1);

        /* Calculate base offset for child */
        int child_base = (int)pathlen;

        /* Recurse */
        result = nftw_walk(fullpath, fn, nopenfd, flags, depth + 1, child_base);
        if (result != 0)
        {
            if (flags & FTW_CHDIR && saved_cwd)
            {
                chdir(saved_cwd);
                free(saved_cwd);
            }
            closedir(dir);
            return result;
        }
    }

    /* Restore directory if we changed it */
    if (flags & FTW_CHDIR && saved_cwd)
    {
        chdir(saved_cwd);
        free(saved_cwd);
    }

    closedir(dir);

    /* Post-order: call function after descending */
    if (flags & FTW_DEPTH)
    {
        ftwbuf.base = base;
        ftwbuf.level = depth;
        result = fn(path, &sb, FTW_DP, &ftwbuf);
        if (result != 0)
        {
            return result;
        }
    }

    return 0;
}

/*
 * nftw - Extended file tree walk
 */
int nftw(const char *path,
         int (*fn)(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf),
         int nopenfd,
         int flags)
{
    if (!path || !fn)
    {
        errno = EINVAL;
        return -1;
    }

    /* Find base offset in path */
    int base = 0;
    size_t len = strlen(path);
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == '/')
        {
            base = (int)i;
            break;
        }
    }

    return nftw_walk(path, fn, nopenfd, flags, 0, base);
}
