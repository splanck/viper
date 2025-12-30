/*
 * ViperOS libc - libgen.h
 * Path name manipulation
 */

#ifndef _LIBGEN_H
#define _LIBGEN_H

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * basename - Extract filename portion of path
     *
     * Returns a pointer to the final component of path.
     * If path is NULL or the empty string, returns ".".
     * If path consists entirely of slashes, returns "/".
     *
     * Note: This function may modify the input string.
     * For thread-safety, use the GNU extension basename_r().
     */
    char *basename(char *path);

    /*
     * dirname - Extract directory portion of path
     *
     * Returns the parent directory of path.
     * If path is NULL or the empty string, returns ".".
     * Trailing slashes are removed.
     *
     * Note: This function may modify the input string.
     * For thread-safety, use the GNU extension dirname_r().
     */
    char *dirname(char *path);

#ifdef __cplusplus
}
#endif

#endif /* _LIBGEN_H */
