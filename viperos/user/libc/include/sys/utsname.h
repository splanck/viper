/*
 * ViperOS libc - sys/utsname.h
 * System identification
 */

#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Length of strings in utsname */
#define _UTSNAME_LENGTH 65

    /* System information structure */
    struct utsname
    {
        char sysname[_UTSNAME_LENGTH];  /* Operating system name */
        char nodename[_UTSNAME_LENGTH]; /* Network node hostname */
        char release[_UTSNAME_LENGTH];  /* Operating system release */
        char version[_UTSNAME_LENGTH];  /* Operating system version */
        char machine[_UTSNAME_LENGTH];  /* Hardware identifier */
#ifdef _GNU_SOURCE
        char domainname[_UTSNAME_LENGTH]; /* NIS/YP domain name (GNU extension) */
#endif
    };

    /*
     * uname - Get system identification
     *
     * Returns 0 on success, -1 on error with errno set.
     */
    int uname(struct utsname *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_UTSNAME_H */
