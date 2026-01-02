//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/libgen.c
// Purpose: Pathname manipulation functions for ViperOS libc.
// Key invariants: May modify input string; returns static buffers for edge cases.
// Ownership/Lifetime: Library; static buffers for "." and "/".
// Links: user/libc/include/libgen.h
//
//===----------------------------------------------------------------------===//

/**
 * @file libgen.c
 * @brief Pathname manipulation functions for ViperOS libc.
 *
 * @details
 * This file implements POSIX pathname manipulation:
 *
 * - basename: Extract filename portion from a path
 * - dirname: Extract directory portion from a path
 *
 * Both functions may modify the input string by inserting null
 * terminators. For NULL or empty inputs, static buffers containing
 * "." are returned. For root paths, "/" is returned.
 *
 * Note: The POSIX versions (unlike GNU) may modify the input.
 */

#include "../include/libgen.h"
#include "../include/string.h"

/* Static buffers for return values */
static char dot[] = ".";
static char slash[] = "/";

/*
 * basename - Extract filename portion of path
 */
char *basename(char *path)
{
    char *p;
    char *last;

    /* Handle NULL or empty string */
    if (path == NULL || path[0] == '\0')
    {
        return dot;
    }

    /* Find length and strip trailing slashes */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
    {
        path[len - 1] = '\0';
        len--;
    }

    /* Path is all slashes */
    if (path[0] == '/' && path[1] == '\0')
    {
        return slash;
    }

    /* Find last slash */
    last = NULL;
    for (p = path; *p; p++)
    {
        if (*p == '/')
        {
            last = p;
        }
    }

    /* No slash found - whole path is basename */
    if (last == NULL)
    {
        return path;
    }

    /* Return component after last slash */
    return last + 1;
}

/*
 * dirname - Extract directory portion of path
 */
char *dirname(char *path)
{
    char *p;
    char *last_slash;

    /* Handle NULL or empty string */
    if (path == NULL || path[0] == '\0')
    {
        return dot;
    }

    /* Strip trailing slashes (but keep root slash) */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
    {
        len--;
        path[len] = '\0';
    }

    /* Find last slash */
    last_slash = NULL;
    for (p = path; *p; p++)
    {
        if (*p == '/')
        {
            last_slash = p;
        }
    }

    /* No slash - current directory */
    if (last_slash == NULL)
    {
        return dot;
    }

    /* Root directory */
    if (last_slash == path)
    {
        /* Preserve the root slash */
        path[1] = '\0';
        return path;
    }

    /* Strip trailing slashes from result */
    while (last_slash > path && last_slash[-1] == '/')
    {
        last_slash--;
    }

    /* Terminate at the slash */
    *last_slash = '\0';

    /* If result is empty, return current directory */
    if (path[0] == '\0')
    {
        return dot;
    }

    return path;
}
