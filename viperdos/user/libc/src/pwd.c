//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/pwd.c
// Purpose: Password database access for ViperDOS libc.
// Key invariants: Single-user system; only root(0) and viper(1000) users.
// Ownership/Lifetime: Library; static password entries.
// Links: user/libc/include/pwd.h
//
//===----------------------------------------------------------------------===//

/**
 * @file pwd.c
 * @brief Password database access for ViperDOS libc.
 *
 * @details
 * This file implements POSIX password database functions:
 *
 * - getpwnam/getpwnam_r: Get password entry by username
 * - getpwuid/getpwuid_r: Get password entry by user ID
 * - getpwent/setpwent/endpwent: Enumerate all password entries
 *
 * ViperDOS is a single-user system with two built-in users:
 * - root (uid 0): Superuser account
 * - viper (uid 1000): Default user account
 *
 * No /etc/passwd file is read; all data is hardcoded.
 * Reentrant (_r) versions use caller-provided buffers.
 */

#include "../include/pwd.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Static password entry for non-reentrant functions */
static struct passwd static_pwd;
static char static_buf[256];

/* Default user for ViperDOS (single-user system) */
static const char *default_name __attribute__((unused)) = "viper";
static const char *default_passwd = "x";
static const char *default_gecos = "ViperDOS User";
static const char *default_dir = "/";
static const char *default_shell = "/bin/sh";

/* Password file enumeration state */
static int pwd_index = 0;

/*
 * fill_passwd - Fill a passwd structure with default values
 */
static int fill_passwd(struct passwd *pwd, char *buf, size_t buflen, uid_t uid, const char *name) {
    /* Calculate required buffer size */
    size_t name_len = strlen(name) + 1;
    size_t passwd_len = strlen(default_passwd) + 1;
    size_t gecos_len = strlen(default_gecos) + 1;
    size_t dir_len = strlen(default_dir) + 1;
    size_t shell_len = strlen(default_shell) + 1;
    size_t total = name_len + passwd_len + gecos_len + dir_len + shell_len;

    if (buflen < total) {
        return ERANGE;
    }

    /* Fill buffer with strings */
    char *p = buf;

    pwd->pw_name = p;
    strcpy(p, name);
    p += name_len;

    pwd->pw_passwd = p;
    strcpy(p, default_passwd);
    p += passwd_len;

    pwd->pw_gecos = p;
    strcpy(p, default_gecos);
    p += gecos_len;

    pwd->pw_dir = p;
    strcpy(p, default_dir);
    p += dir_len;

    pwd->pw_shell = p;
    strcpy(p, default_shell);

    pwd->pw_uid = uid;
    pwd->pw_gid = uid; /* Same as uid for simplicity */

    return 0;
}

/**
 * @brief Get password entry by username.
 *
 * @details
 * Searches the password database for an entry with a matching username
 * and returns a pointer to a structure containing the user information.
 *
 * ViperDOS recognizes two usernames:
 * - "root": Superuser (uid 0)
 * - "viper": Default user (uid 1000)
 *
 * The returned structure contains:
 * - pw_name: Username
 * - pw_passwd: Encrypted password ("x" in ViperDOS)
 * - pw_uid/pw_gid: User/group IDs
 * - pw_gecos: User information field
 * - pw_dir: Home directory
 * - pw_shell: Login shell
 *
 * @warning The returned pointer points to static storage that is
 * overwritten by subsequent calls to getpwnam(), getpwuid(), or getpwent().
 * Use getpwnam_r() for thread-safe access.
 *
 * @param name Username to search for.
 * @return Pointer to passwd structure, or NULL if not found.
 *
 * @see getpwnam_r, getpwuid, getpwent
 */
struct passwd *getpwnam(const char *name) {
    struct passwd *result;

    if (getpwnam_r(name, &static_pwd, static_buf, sizeof(static_buf), &result) != 0) {
        return NULL;
    }

    return result;
}

/**
 * @brief Get password entry by user ID.
 *
 * @details
 * Searches the password database for an entry with a matching numeric
 * user ID and returns a pointer to a structure containing the user info.
 *
 * ViperDOS recognizes two user IDs:
 * - 0: root (superuser)
 * - 1000: viper (default user)
 *
 * Any other UID is treated as the viper user for compatibility.
 *
 * @warning The returned pointer points to static storage that is
 * overwritten by subsequent calls. Use getpwuid_r() for thread-safe access.
 *
 * @param uid Numeric user ID to search for.
 * @return Pointer to passwd structure, or NULL if not found.
 *
 * @see getpwuid_r, getpwnam, getpwent
 */
struct passwd *getpwuid(uid_t uid) {
    struct passwd *result;

    if (getpwuid_r(uid, &static_pwd, static_buf, sizeof(static_buf), &result) != 0) {
        return NULL;
    }

    return result;
}

/**
 * @brief Get password entry by username (reentrant version).
 *
 * @details
 * Thread-safe version of getpwnam(). Instead of using static storage,
 * the caller provides buffers for the result. Multiple threads can
 * safely call this function concurrently.
 *
 * The buf parameter must be large enough to hold all string fields
 * (username, password, gecos, home directory, shell). The result
 * pointer is set to pwd on success, or NULL if the user is not found.
 *
 * @param name Username to search for.
 * @param pwd Caller-provided passwd structure to fill in.
 * @param buf Buffer for storing string data.
 * @param buflen Size of the buffer.
 * @param result Set to pwd on success, NULL if not found.
 * @return 0 on success, EINVAL on invalid args, ERANGE if buffer too small.
 *
 * @see getpwnam, getpwuid_r
 */
int getpwnam_r(
    const char *name, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result) {
    if (!name || !pwd || !buf || !result) {
        if (result)
            *result = NULL;
        return EINVAL;
    }

    *result = NULL;

    /* ViperDOS is single-user - only "viper" and "root" exist */
    if (strcmp(name, "viper") == 0) {
        int err = fill_passwd(pwd, buf, buflen, 1000, "viper");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    } else if (strcmp(name, "root") == 0) {
        int err = fill_passwd(pwd, buf, buflen, 0, "root");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    }

    /* User not found */
    return 0;
}

/**
 * @brief Get password entry by user ID (reentrant version).
 *
 * @details
 * Thread-safe version of getpwuid(). Instead of using static storage,
 * the caller provides buffers for the result. Multiple threads can
 * safely call this function concurrently.
 *
 * @param uid Numeric user ID to search for.
 * @param pwd Caller-provided passwd structure to fill in.
 * @param buf Buffer for storing string data.
 * @param buflen Size of the buffer.
 * @param result Set to pwd on success, NULL if not found.
 * @return 0 on success, EINVAL on invalid args, ERANGE if buffer too small.
 *
 * @see getpwuid, getpwnam_r
 */
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result) {
    if (!pwd || !buf || !result) {
        if (result)
            *result = NULL;
        return EINVAL;
    }

    *result = NULL;

    /* ViperDOS has uid 0 = root, uid 1000 = viper */
    if (uid == 0) {
        int err = fill_passwd(pwd, buf, buflen, 0, "root");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    } else if (uid == 1000 || uid == (uid_t)-1) {
        /* Treat any other uid as viper for compatibility */
        int err = fill_passwd(pwd, buf, buflen, 1000, "viper");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    }

    /* User not found */
    return 0;
}

/**
 * @brief Open or rewind the password database.
 *
 * @details
 * Resets the password database to the beginning, so that the next call
 * to getpwent() will return the first entry. If the database was already
 * open, it is rewound to the beginning.
 *
 * In ViperDOS, this simply resets the enumeration index since there
 * is no actual password file to open.
 *
 * @see endpwent, getpwent
 */
void setpwent(void) {
    pwd_index = 0;
}

/**
 * @brief Close the password database.
 *
 * @details
 * Closes the password database after enumeration is complete. This
 * should be called when you're done iterating with getpwent() to
 * release any resources.
 *
 * In ViperDOS, this simply resets the enumeration index.
 *
 * @see setpwent, getpwent
 */
void endpwent(void) {
    pwd_index = 0;
}

/**
 * @brief Get next password entry.
 *
 * @details
 * Returns the next entry from the password database. Call setpwent()
 * first to start from the beginning. After the last entry, returns NULL.
 *
 * ViperDOS has only two entries:
 * 1. root (uid 0)
 * 2. viper (uid 1000)
 *
 * @warning The returned pointer points to static storage that is
 * overwritten by subsequent calls. This function is not thread-safe.
 *
 * @return Pointer to passwd structure, or NULL when done.
 *
 * @see setpwent, endpwent, getpwnam, getpwuid
 */
struct passwd *getpwent(void) {
    struct passwd *result = NULL;

    switch (pwd_index) {
        case 0:
            /* Return root entry */
            if (fill_passwd(&static_pwd, static_buf, sizeof(static_buf), 0, "root") == 0) {
                result = &static_pwd;
            }
            break;

        case 1:
            /* Return viper entry */
            if (fill_passwd(&static_pwd, static_buf, sizeof(static_buf), 1000, "viper") == 0) {
                result = &static_pwd;
            }
            break;

        default:
            /* No more entries */
            return NULL;
    }

    if (result)
        pwd_index++;

    return result;
}
