/*
 * ViperDOS libc - glob.h
 * Filename globbing
 */

#ifndef _GLOB_H
#define _GLOB_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Glob result structure */
    typedef struct
    {
        size_t gl_pathc; /* Count of paths matched */
        char **gl_pathv; /* List of matched pathnames */
        size_t gl_offs;  /* Slots to reserve in gl_pathv */

        /* Internal state */
        size_t gl_pathalloc; /* Allocated size of gl_pathv */
        int gl_flags;        /* Flags from glob() call */
    } glob_t;

/* Flags for glob() */
#define GLOB_ERR (1 << 0)      /* Return on read error */
#define GLOB_MARK (1 << 1)     /* Append / to directories */
#define GLOB_NOSORT (1 << 2)   /* Don't sort results */
#define GLOB_DOOFFS (1 << 3)   /* Reserve gl_offs slots */
#define GLOB_NOCHECK (1 << 4)  /* Return pattern if no matches */
#define GLOB_APPEND (1 << 5)   /* Append to existing results */
#define GLOB_NOESCAPE (1 << 6) /* Disable backslash escaping */
#define GLOB_PERIOD (1 << 7)   /* Match . in filename start */

/* GNU extensions */
#define GLOB_BRACE (1 << 10)       /* Expand {a,b} patterns */
#define GLOB_NOMAGIC (1 << 11)     /* Return pattern if no magic chars */
#define GLOB_TILDE (1 << 12)       /* Expand ~ to home directory */
#define GLOB_TILDE_CHECK (1 << 14) /* Like TILDE but error if no home */
#define GLOB_ONLYDIR (1 << 13)     /* Match only directories */

/* Return values for glob() */
#define GLOB_NOSPACE 1 /* Out of memory */
#define GLOB_ABORTED 2 /* Read error (with GLOB_ERR) */
#define GLOB_NOMATCH 3 /* No matches found */
#define GLOB_NOSYS 4   /* Function not implemented */

    /*
     * glob - Find pathnames matching pattern
     *
     * Searches for files matching shell-style pattern.
     * Results are stored in pglob.
     * If GLOB_APPEND is set, adds to existing results.
     * errfunc(path, errno) is called on read errors.
     *
     * Returns 0 on success, or GLOB_* error code.
     */
    int glob(const char *pattern,
             int flags,
             int (*errfunc)(const char *epath, int eerrno),
             glob_t *pglob);

    /*
     * globfree - Free glob results
     *
     * Frees memory allocated by glob().
     */
    void globfree(glob_t *pglob);

#ifdef __cplusplus
}
#endif

#endif /* _GLOB_H */
