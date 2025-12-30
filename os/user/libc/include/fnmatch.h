/*
 * ViperOS libc - fnmatch.h
 * Filename pattern matching
 */

#ifndef _FNMATCH_H
#define _FNMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Flags for fnmatch() */
#define FNM_PATHNAME  0x01  /* No wildcard can match '/' */
#define FNM_NOESCAPE  0x02  /* Disable backslash escaping */
#define FNM_PERIOD    0x04  /* Leading '.' must be matched explicitly */
#define FNM_LEADING_DIR 0x08 /* Match leading directory part only */
#define FNM_CASEFOLD  0x10  /* Case-insensitive matching (GNU extension) */

/* Return value for no match */
#define FNM_NOMATCH   1

/*
 * fnmatch - Match filename or pathname against pattern
 *
 * Pattern matching syntax:
 *   *      Matches any sequence of characters (except '/' if FNM_PATHNAME)
 *   ?      Matches any single character (except '/' if FNM_PATHNAME)
 *   [...]  Matches any character in the set
 *   [!...] Matches any character not in the set
 *   [^...] Same as [!...]
 *   \c     Matches character c literally (if FNM_NOESCAPE not set)
 *
 * Returns:
 *   0          Pattern matches string
 *   FNM_NOMATCH Pattern does not match
 */
int fnmatch(const char *pattern, const char *string, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _FNMATCH_H */
