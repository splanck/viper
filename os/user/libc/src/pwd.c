/*
 * ViperOS libc - pwd.c
 * Password file access implementation
 */

#include "../include/pwd.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Static password entry for non-reentrant functions */
static struct passwd static_pwd;
static char static_buf[256];

/* Default user for ViperOS (single-user system) */
static const char *default_name __attribute__((unused)) = "viper";
static const char *default_passwd = "x";
static const char *default_gecos = "ViperOS User";
static const char *default_dir = "/";
static const char *default_shell = "/bin/sh";

/* Password file enumeration state */
static int pwd_index = 0;

/*
 * fill_passwd - Fill a passwd structure with default values
 */
static int fill_passwd(struct passwd *pwd, char *buf, size_t buflen, uid_t uid, const char *name)
{
    /* Calculate required buffer size */
    size_t name_len = strlen(name) + 1;
    size_t passwd_len = strlen(default_passwd) + 1;
    size_t gecos_len = strlen(default_gecos) + 1;
    size_t dir_len = strlen(default_dir) + 1;
    size_t shell_len = strlen(default_shell) + 1;
    size_t total = name_len + passwd_len + gecos_len + dir_len + shell_len;

    if (buflen < total)
    {
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

/*
 * getpwnam - Get password entry by name
 */
struct passwd *getpwnam(const char *name)
{
    struct passwd *result;

    if (getpwnam_r(name, &static_pwd, static_buf, sizeof(static_buf), &result) != 0)
    {
        return NULL;
    }

    return result;
}

/*
 * getpwuid - Get password entry by user ID
 */
struct passwd *getpwuid(uid_t uid)
{
    struct passwd *result;

    if (getpwuid_r(uid, &static_pwd, static_buf, sizeof(static_buf), &result) != 0)
    {
        return NULL;
    }

    return result;
}

/*
 * getpwnam_r - Get password entry by name (reentrant)
 */
int getpwnam_r(
    const char *name, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result)
{
    if (!name || !pwd || !buf || !result)
    {
        if (result)
            *result = NULL;
        return EINVAL;
    }

    *result = NULL;

    /* ViperOS is single-user - only "viper" and "root" exist */
    if (strcmp(name, "viper") == 0)
    {
        int err = fill_passwd(pwd, buf, buflen, 1000, "viper");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    }
    else if (strcmp(name, "root") == 0)
    {
        int err = fill_passwd(pwd, buf, buflen, 0, "root");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    }

    /* User not found */
    return 0;
}

/*
 * getpwuid_r - Get password entry by user ID (reentrant)
 */
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result)
{
    if (!pwd || !buf || !result)
    {
        if (result)
            *result = NULL;
        return EINVAL;
    }

    *result = NULL;

    /* ViperOS has uid 0 = root, uid 1000 = viper */
    if (uid == 0)
    {
        int err = fill_passwd(pwd, buf, buflen, 0, "root");
        if (err != 0)
            return err;
        *result = pwd;
        return 0;
    }
    else if (uid == 1000 || uid == (uid_t)-1)
    {
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

/*
 * setpwent - Open/rewind the password file
 */
void setpwent(void)
{
    pwd_index = 0;
}

/*
 * endpwent - Close the password file
 */
void endpwent(void)
{
    pwd_index = 0;
}

/*
 * getpwent - Get next password entry
 */
struct passwd *getpwent(void)
{
    struct passwd *result = NULL;

    switch (pwd_index)
    {
        case 0:
            /* Return root entry */
            if (fill_passwd(&static_pwd, static_buf, sizeof(static_buf), 0, "root") == 0)
            {
                result = &static_pwd;
            }
            break;

        case 1:
            /* Return viper entry */
            if (fill_passwd(&static_pwd, static_buf, sizeof(static_buf), 1000, "viper") == 0)
            {
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
