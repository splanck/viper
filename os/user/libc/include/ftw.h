/*
 * ViperOS libc - ftw.h
 * File tree walk
 */

#ifndef _FTW_H
#define _FTW_H

#include "sys/types.h"
#include "sys/stat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Type flags for callback */
#define FTW_F       0   /* Regular file */
#define FTW_D       1   /* Directory */
#define FTW_DNR     2   /* Directory that cannot be read */
#define FTW_DP      3   /* Directory, all subdirs visited (post-order) */
#define FTW_NS      4   /* stat() failed, stat buffer undefined */
#define FTW_SL      5   /* Symbolic link */
#define FTW_SLN     6   /* Symbolic link pointing to nonexistent file */

/* Flags for nftw() */
#define FTW_PHYS    (1 << 0)    /* Don't follow symbolic links */
#define FTW_MOUNT   (1 << 1)    /* Don't cross filesystem boundaries */
#define FTW_DEPTH   (1 << 2)    /* Post-order traversal */
#define FTW_CHDIR   (1 << 3)    /* chdir to each directory */

/* Structure passed to nftw callback */
struct FTW {
    int base;   /* Offset of basename in pathname */
    int level;  /* Depth relative to start directory */
};

/*
 * ftw - File tree walk (legacy)
 *
 * Walks directory tree rooted at 'path', calling 'fn' for each entry.
 * 'nopenfd' is max number of directories to hold open.
 *
 * fn(path, sb, type) returns:
 *   0 to continue walking
 *   non-zero to stop and return that value
 *
 * Returns 0 on success, -1 on error, or value from fn.
 */
int ftw(const char *path,
        int (*fn)(const char *fpath, const struct stat *sb, int typeflag),
        int nopenfd);

/*
 * nftw - Extended file tree walk
 *
 * Like ftw, but with additional flags and FTW structure.
 *
 * fn(path, sb, type, ftwbuf) returns:
 *   0 to continue walking
 *   non-zero to stop and return that value
 *
 * Returns 0 on success, -1 on error, or value from fn.
 */
int nftw(const char *path,
         int (*fn)(const char *fpath, const struct stat *sb,
                   int typeflag, struct FTW *ftwbuf),
         int nopenfd, int flags);

#ifdef __cplusplus
}
#endif

#endif /* _FTW_H */
