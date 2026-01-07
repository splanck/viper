//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/glob.c
// Purpose: Pathname pattern matching for ViperOS libc.
// Key invariants: Expands wildcards via fnmatch; sorted results.
// Ownership/Lifetime: Library; caller frees via globfree().
// Links: user/libc/include/glob.h
//
//===----------------------------------------------------------------------===//

/**
 * @file glob.c
 * @brief Pathname pattern matching for ViperOS libc.
 *
 * @details
 * This file implements POSIX pathname globbing:
 *
 * - glob: Expand a pattern to matching pathnames
 * - globfree: Free memory allocated by glob
 *
 * Supports shell-style wildcards (*, ?, [...]) via fnmatch().
 * Results are returned in a glob_t structure with an array of
 * matching pathnames sorted alphabetically.
 *
 * Flags: GLOB_APPEND, GLOB_DOOFFS, GLOB_ERR, GLOB_MARK, GLOB_NOCHECK,
 * GLOB_NOSORT, GLOB_NOESCAPE, GLOB_PERIOD, GLOB_TILDE, GLOB_ONLYDIR.
 * Directory pattern magic is not fully supported.
 */

#include "../include/glob.h"
#include "../include/dirent.h"
#include "../include/errno.h"
#include "../include/fnmatch.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/sys/stat.h"

/* Initial path vector size */
#define INITIAL_PATHV_SIZE 16

/*
 * glob_add_path - Add a path to glob results
 */
static int glob_add_path(glob_t *pglob, const char *path)
{
    /* Check if we need to grow the array */
    size_t needed = pglob->gl_offs + pglob->gl_pathc + 2;
    if (needed > pglob->gl_pathalloc)
    {
        size_t new_size = pglob->gl_pathalloc * 2;
        if (new_size < INITIAL_PATHV_SIZE)
        {
            new_size = INITIAL_PATHV_SIZE;
        }
        if (new_size < needed)
        {
            new_size = needed;
        }

        char **new_pathv = (char **)realloc(pglob->gl_pathv, new_size * sizeof(char *));
        if (!new_pathv)
        {
            return GLOB_NOSPACE;
        }
        pglob->gl_pathv = new_pathv;
        pglob->gl_pathalloc = new_size;
    }

    /* Copy the path */
    char *pathcopy = strdup(path);
    if (!pathcopy)
    {
        return GLOB_NOSPACE;
    }

    pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = pathcopy;
    pglob->gl_pathc++;
    pglob->gl_pathv[pglob->gl_offs + pglob->gl_pathc] = NULL;

    return 0;
}

/*
 * has_magic - Check if pattern contains glob metacharacters
 */
static int has_magic(const char *pattern)
{
    for (const char *p = pattern; *p; p++)
    {
        switch (*p)
        {
            case '*':
            case '?':
            case '[':
                return 1;
            case '\\':
                if (p[1])
                    p++;
                break;
        }
    }
    return 0;
}

/*
 * glob_dir - Glob in a specific directory
 */
static int glob_dir(const char *dirname,
                    const char *pattern,
                    int flags,
                    int (*errfunc)(const char *, int),
                    glob_t *pglob)
{
    DIR *dir;
    struct dirent *entry;
    int fnmatch_flags = 0;

    /* Convert glob flags to fnmatch flags */
    if (flags & GLOB_NOESCAPE)
    {
        fnmatch_flags |= FNM_NOESCAPE;
    }
    if (!(flags & GLOB_PERIOD))
    {
        fnmatch_flags |= FNM_PERIOD;
    }

    /* Open directory */
    dir = opendir(dirname[0] ? dirname : ".");
    if (!dir)
    {
        if (errfunc)
        {
            if (errfunc(dirname, errno))
            {
                return GLOB_ABORTED;
            }
        }
        if (flags & GLOB_ERR)
        {
            return GLOB_ABORTED;
        }
        return 0;
    }

    /* Read entries */
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

        /* Match against pattern */
        if (fnmatch(pattern, entry->d_name, fnmatch_flags) == 0)
        {
            /* Build full path */
            char fullpath[1024];
            if (dirname[0])
            {
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, entry->d_name);
            }
            else
            {
                strncpy(fullpath, entry->d_name, sizeof(fullpath) - 1);
                fullpath[sizeof(fullpath) - 1] = '\0';
            }

            /* Check if directory and GLOB_ONLYDIR */
            if (flags & GLOB_ONLYDIR)
            {
                struct stat st;
                if (stat(fullpath, &st) != 0 || !S_ISDIR(st.st_mode))
                {
                    continue;
                }
            }

            /* Add / suffix if GLOB_MARK and directory */
            if (flags & GLOB_MARK)
            {
                struct stat st;
                if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
                {
                    size_t len = strlen(fullpath);
                    if (len < sizeof(fullpath) - 1 && fullpath[len - 1] != '/')
                    {
                        fullpath[len] = '/';
                        fullpath[len + 1] = '\0';
                    }
                }
            }

            int err = glob_add_path(pglob, fullpath);
            if (err)
            {
                closedir(dir);
                return err;
            }
        }
    }

    closedir(dir);
    return 0;
}

/*
 * Compare function for sorting paths
 */
static int glob_compare(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/*
 * glob - Find pathnames matching pattern
 */
int glob(const char *pattern,
         int flags,
         int (*errfunc)(const char *epath, int eerrno),
         glob_t *pglob)
{
    int result;

    if (!pattern || !pglob)
    {
        return GLOB_ABORTED;
    }

    /* Initialize glob structure if not appending */
    if (!(flags & GLOB_APPEND))
    {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = NULL;
        pglob->gl_pathalloc = 0;
        pglob->gl_offs = (flags & GLOB_DOOFFS) ? pglob->gl_offs : 0;
        pglob->gl_flags = flags;

        /* Allocate initial pathv with offset slots */
        if (pglob->gl_offs > 0)
        {
            pglob->gl_pathv = (char **)calloc(pglob->gl_offs + 1, sizeof(char *));
            if (!pglob->gl_pathv)
            {
                return GLOB_NOSPACE;
            }
            pglob->gl_pathalloc = pglob->gl_offs + 1;
        }
    }

    /* Handle tilde expansion (simplified) */
    const char *actual_pattern = pattern;
    char expanded[1024];

    if ((flags & GLOB_TILDE) && pattern[0] == '~')
    {
        const char *home = getenv("HOME");
        if (home)
        {
            if (pattern[1] == '\0' || pattern[1] == '/')
            {
                snprintf(expanded, sizeof(expanded), "%s%s", home, pattern + 1);
                actual_pattern = expanded;
            }
        }
        else if (flags & GLOB_TILDE_CHECK)
        {
            return GLOB_ABORTED;
        }
    }

    /* Check if pattern has magic characters */
    if (!has_magic(actual_pattern))
    {
        /* No magic - check if file exists */
        struct stat st;
        if (stat(actual_pattern, &st) == 0)
        {
            /* Check GLOB_ONLYDIR */
            if ((flags & GLOB_ONLYDIR) && !S_ISDIR(st.st_mode))
            {
                if (flags & GLOB_NOCHECK)
                {
                    return glob_add_path(pglob, actual_pattern);
                }
                return GLOB_NOMATCH;
            }

            char *path = strdup(actual_pattern);
            if (!path)
            {
                return GLOB_NOSPACE;
            }

            /* Add / suffix if GLOB_MARK and directory */
            if ((flags & GLOB_MARK) && S_ISDIR(st.st_mode))
            {
                size_t len = strlen(path);
                if (path[len - 1] != '/')
                {
                    char *newpath = (char *)malloc(len + 2);
                    if (!newpath)
                    {
                        free(path);
                        return GLOB_NOSPACE;
                    }
                    memcpy(newpath, path, len);
                    newpath[len] = '/';
                    newpath[len + 1] = '\0';
                    free(path);
                    path = newpath;
                }
            }

            result = glob_add_path(pglob, path);
            free(path);
            return result;
        }
        else if (flags & GLOB_NOCHECK)
        {
            return glob_add_path(pglob, actual_pattern);
        }
        else
        {
            return GLOB_NOMATCH;
        }
    }

    /* Split pattern into directory and file parts */
    const char *last_slash = strrchr(actual_pattern, '/');
    char dirname[1024] = "";
    const char *filepattern;

    if (last_slash)
    {
        size_t dirlen = last_slash - actual_pattern;
        if (dirlen >= sizeof(dirname))
        {
            dirlen = sizeof(dirname) - 1;
        }
        memcpy(dirname, actual_pattern, dirlen);
        dirname[dirlen] = '\0';
        filepattern = last_slash + 1;

        /* Handle magic in directory portion (simplified - just first level) */
        if (has_magic(dirname))
        {
            /* For now, don't support magic in directory part */
            /* This would require recursive globbing */
            if (flags & GLOB_NOCHECK)
            {
                return glob_add_path(pglob, actual_pattern);
            }
            return GLOB_NOMATCH;
        }
    }
    else
    {
        filepattern = actual_pattern;
    }

    /* Glob in the directory */
    result = glob_dir(dirname, filepattern, flags, errfunc, pglob);
    if (result != 0)
    {
        return result;
    }

    /* Check if no matches */
    if (pglob->gl_pathc == 0)
    {
        if (flags & GLOB_NOCHECK)
        {
            return glob_add_path(pglob, actual_pattern);
        }
        return GLOB_NOMATCH;
    }

    /* Sort results unless GLOB_NOSORT */
    if (!(flags & GLOB_NOSORT) && pglob->gl_pathc > 1)
    {
        qsort(pglob->gl_pathv + pglob->gl_offs, pglob->gl_pathc, sizeof(char *), glob_compare);
    }

    return 0;
}

/*
 * globfree - Free glob results
 */
void globfree(glob_t *pglob)
{
    if (!pglob)
    {
        return;
    }

    if (pglob->gl_pathv)
    {
        /* Free each path string */
        for (size_t i = pglob->gl_offs; i < pglob->gl_offs + pglob->gl_pathc; i++)
        {
            if (pglob->gl_pathv[i])
            {
                free(pglob->gl_pathv[i]);
            }
        }
        free(pglob->gl_pathv);
    }

    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
    pglob->gl_pathalloc = 0;
}
