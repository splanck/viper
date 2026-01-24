/*
 * ViperDOS libc - pwd.h
 * Password file access
 */

#ifndef _PWD_H
#define _PWD_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Password entry structure */
struct passwd {
    char *pw_name;   /* Username */
    char *pw_passwd; /* User password (usually 'x') */
    uid_t pw_uid;    /* User ID */
    gid_t pw_gid;    /* Group ID */
    char *pw_gecos;  /* User information */
    char *pw_dir;    /* Home directory */
    char *pw_shell;  /* Shell program */
};

/*
 * getpwnam - Get password entry by name
 *
 * Returns a pointer to a static passwd structure, or NULL if not found.
 * Not thread-safe.
 */
struct passwd *getpwnam(const char *name);

/*
 * getpwuid - Get password entry by user ID
 *
 * Returns a pointer to a static passwd structure, or NULL if not found.
 * Not thread-safe.
 */
struct passwd *getpwuid(uid_t uid);

/*
 * getpwnam_r - Get password entry by name (reentrant)
 *
 * Returns 0 on success, or an error number on failure.
 * On success, *result points to pwd; on failure or not found, *result is NULL.
 */
int getpwnam_r(
    const char *name, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result);

/*
 * getpwuid_r - Get password entry by user ID (reentrant)
 *
 * Returns 0 on success, or an error number on failure.
 * On success, *result points to pwd; on failure or not found, *result is NULL.
 */
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result);

/*
 * Password file enumeration functions
 */

/* Open/rewind the password file */
void setpwent(void);

/* Close the password file */
void endpwent(void);

/* Get next password entry */
struct passwd *getpwent(void);

#ifdef __cplusplus
}
#endif

#endif /* _PWD_H */
