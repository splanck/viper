//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/grp.c
// Purpose: Group database access for ViperDOS libc.
// Key invariants: Single-user system; only root(0), users(100), viper(1000).
// Ownership/Lifetime: Library; static group entries.
// Links: user/libc/include/grp.h
//
//===----------------------------------------------------------------------===//

/**
 * @file grp.c
 * @brief Group database access for ViperDOS libc.
 *
 * @details
 * This file implements POSIX group database functions:
 *
 * - getgrnam/getgrnam_r: Get group entry by name
 * - getgrgid/getgrgid_r: Get group entry by group ID
 * - getgrent/setgrent/endgrent: Enumerate all group entries
 * - getgrouplist: Get list of groups for a user
 * - initgroups: Initialize supplementary group list
 *
 * ViperDOS has the following built-in groups:
 * - root (gid 0): Superuser group
 * - wheel (gid 0): Administrative group (includes root, viper)
 * - users (gid 100): Standard users group
 * - viper (gid 1000): Primary group for viper user
 *
 * No /etc/group file is read; all data is hardcoded.
 */

#include "../include/grp.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Static group entry for non-reentrant functions */
static struct group static_grp;
static char static_buf[256];
static char *static_members[4];

/* Group file enumeration state */
static int grp_index = 0;

/*
 * fill_group - Fill a group structure with values
 */
static int fill_group(
    struct group *grp, char *buf, size_t buflen, gid_t gid, const char *name, char **members) {
    /* Calculate required buffer size */
    size_t name_len = strlen(name) + 1;
    size_t passwd_len = 2; /* "x\0" */
    size_t total = name_len + passwd_len;

    if (buflen < total) {
        return ERANGE;
    }

    /* Fill buffer with strings */
    char *p = buf;

    grp->gr_name = p;
    strcpy(p, name);
    p += name_len;

    grp->gr_passwd = p;
    strcpy(p, "x");
    p += passwd_len;

    grp->gr_gid = gid;
    grp->gr_mem = members;

    return 0;
}

/*
 * getgrnam - Get group entry by name
 */
struct group *getgrnam(const char *name) {
    struct group *result;

    if (getgrnam_r(name, &static_grp, static_buf, sizeof(static_buf), &result) != 0) {
        return NULL;
    }

    return result;
}

/*
 * getgrgid - Get group entry by group ID
 */
struct group *getgrgid(gid_t gid) {
    struct group *result;

    if (getgrgid_r(gid, &static_grp, static_buf, sizeof(static_buf), &result) != 0) {
        return NULL;
    }

    return result;
}

/*
 * getgrnam_r - Get group entry by name (reentrant)
 */
int getgrnam_r(
    const char *name, struct group *grp, char *buf, size_t buflen, struct group **result) {
    if (!name || !grp || !buf || !result) {
        if (result)
            *result = NULL;
        return EINVAL;
    }

    *result = NULL;

    /* ViperDOS groups: root (0), wheel (0), users (100), viper (1000) */
    if (strcmp(name, "root") == 0) {
        static_members[0] = "root";
        static_members[1] = NULL;
        int err = fill_group(grp, buf, buflen, 0, "root", static_members);
        if (err != 0)
            return err;
        *result = grp;
        return 0;
    } else if (strcmp(name, "wheel") == 0) {
        static_members[0] = "root";
        static_members[1] = "viper";
        static_members[2] = NULL;
        int err = fill_group(grp, buf, buflen, 0, "wheel", static_members);
        if (err != 0)
            return err;
        *result = grp;
        return 0;
    } else if (strcmp(name, "users") == 0) {
        static_members[0] = "viper";
        static_members[1] = NULL;
        int err = fill_group(grp, buf, buflen, 100, "users", static_members);
        if (err != 0)
            return err;
        *result = grp;
        return 0;
    } else if (strcmp(name, "viper") == 0) {
        static_members[0] = "viper";
        static_members[1] = NULL;
        int err = fill_group(grp, buf, buflen, 1000, "viper", static_members);
        if (err != 0)
            return err;
        *result = grp;
        return 0;
    }

    /* Group not found */
    return 0;
}

/*
 * getgrgid_r - Get group entry by group ID (reentrant)
 */
int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen, struct group **result) {
    if (!grp || !buf || !result) {
        if (result)
            *result = NULL;
        return EINVAL;
    }

    *result = NULL;

    /* Map gid to group name */
    const char *name = NULL;

    if (gid == 0) {
        name = "root";
        static_members[0] = "root";
        static_members[1] = NULL;
    } else if (gid == 100) {
        name = "users";
        static_members[0] = "viper";
        static_members[1] = NULL;
    } else if (gid == 1000) {
        name = "viper";
        static_members[0] = "viper";
        static_members[1] = NULL;
    } else {
        /* Unknown gid */
        return 0;
    }

    int err = fill_group(grp, buf, buflen, gid, name, static_members);
    if (err != 0)
        return err;

    *result = grp;
    return 0;
}

/*
 * setgrent - Open/rewind the group file
 */
void setgrent(void) {
    grp_index = 0;
}

/*
 * endgrent - Close the group file
 */
void endgrent(void) {
    grp_index = 0;
}

/*
 * getgrent - Get next group entry
 */
struct group *getgrent(void) {
    struct group *result = NULL;

    switch (grp_index) {
        case 0:
            /* Return root entry */
            static_members[0] = "root";
            static_members[1] = NULL;
            if (fill_group(
                    &static_grp, static_buf, sizeof(static_buf), 0, "root", static_members) == 0) {
                result = &static_grp;
            }
            break;

        case 1:
            /* Return users entry */
            static_members[0] = "viper";
            static_members[1] = NULL;
            if (fill_group(
                    &static_grp, static_buf, sizeof(static_buf), 100, "users", static_members) ==
                0) {
                result = &static_grp;
            }
            break;

        case 2:
            /* Return viper entry */
            static_members[0] = "viper";
            static_members[1] = NULL;
            if (fill_group(
                    &static_grp, static_buf, sizeof(static_buf), 1000, "viper", static_members) ==
                0) {
                result = &static_grp;
            }
            break;

        default:
            /* No more entries */
            return NULL;
    }

    if (result)
        grp_index++;

    return result;
}

/*
 * getgrouplist - Get list of groups for a user
 */
int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups) {
    if (!user || !groups || !ngroups || *ngroups < 1) {
        if (ngroups)
            *ngroups = 0;
        return -1;
    }

    int count = 0;
    int max = *ngroups;

    /* Always include the primary group */
    if (count < max) {
        groups[count] = group;
    }
    count++;

    /* For ViperDOS, "viper" user is in users (100) and viper (1000) groups */
    if (strcmp(user, "viper") == 0) {
        if (group != 100) {
            if (count < max)
                groups[count] = 100;
            count++;
        }
        if (group != 1000) {
            if (count < max)
                groups[count] = 1000;
            count++;
        }
    } else if (strcmp(user, "root") == 0) {
        /* Root is only in root group (0) */
        if (group != 0) {
            if (count < max)
                groups[count] = 0;
            count++;
        }
    }

    *ngroups = count;
    return count;
}

/*
 * initgroups - Initialize the supplementary group access list
 */
int initgroups(const char *user, gid_t group) {
    if (!user) {
        errno = EINVAL;
        return -1;
    }

    /* ViperDOS doesn't actually track supplementary groups */
    /* Just succeed silently */
    (void)group;
    return 0;
}
