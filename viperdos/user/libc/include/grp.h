/*
 * ViperDOS libc - grp.h
 * Group file access
 */

#ifndef _GRP_H
#define _GRP_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Group entry structure */
    struct group
    {
        char *gr_name;   /* Group name */
        char *gr_passwd; /* Group password (usually 'x' or empty) */
        gid_t gr_gid;    /* Group ID */
        char **gr_mem;   /* NULL-terminated array of member usernames */
    };

    /*
     * getgrnam - Get group entry by name
     *
     * Returns a pointer to a static group structure, or NULL if not found.
     * Not thread-safe.
     */
    struct group *getgrnam(const char *name);

    /*
     * getgrgid - Get group entry by group ID
     *
     * Returns a pointer to a static group structure, or NULL if not found.
     * Not thread-safe.
     */
    struct group *getgrgid(gid_t gid);

    /*
     * getgrnam_r - Get group entry by name (reentrant)
     *
     * Returns 0 on success, or an error number on failure.
     * On success, *result points to grp; on failure or not found, *result is NULL.
     */
    int getgrnam_r(
        const char *name, struct group *grp, char *buf, size_t buflen, struct group **result);

    /*
     * getgrgid_r - Get group entry by group ID (reentrant)
     *
     * Returns 0 on success, or an error number on failure.
     * On success, *result points to grp; on failure or not found, *result is NULL.
     */
    int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen, struct group **result);

    /*
     * Group file enumeration functions
     */

    /* Open/rewind the group file */
    void setgrent(void);

    /* Close the group file */
    void endgrent(void);

    /* Get next group entry */
    struct group *getgrent(void);

    /*
     * getgrouplist - Get list of groups for a user
     *
     * Returns number of groups (may exceed *ngroups if buffer too small).
     * On return, *ngroups contains actual number of groups stored.
     */
    int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups);

    /*
     * initgroups - Initialize the supplementary group access list
     *
     * Returns 0 on success, -1 on failure with errno set.
     */
    int initgroups(const char *user, gid_t group);

#ifdef __cplusplus
}
#endif

#endif /* _GRP_H */
