/*
 * ViperOS libc - utsname.c
 * System identification implementation
 */

#include "../include/sys/utsname.h"
#include "../include/errno.h"
#include "../include/string.h"

/* Syscall helpers */
extern long __syscall1(long num, long arg0);

/* Syscall number for uname */
#define SYS_UNAME 0xE8

/*
 * uname - Get system identification
 *
 * Fills in the utsname structure with system information.
 * If the kernel syscall fails, provides default values.
 */
int uname(struct utsname *buf)
{
    if (!buf)
    {
        errno = EFAULT;
        return -1;
    }

    /* Try the kernel syscall first */
    long result = __syscall1(SYS_UNAME, (long)buf);

    if (result < 0)
    {
        /* Kernel syscall not implemented, provide static values */
        strncpy(buf->sysname, "ViperOS", _UTSNAME_LENGTH - 1);
        buf->sysname[_UTSNAME_LENGTH - 1] = '\0';

        strncpy(buf->nodename, "viper", _UTSNAME_LENGTH - 1);
        buf->nodename[_UTSNAME_LENGTH - 1] = '\0';

        strncpy(buf->release, "0.1.0", _UTSNAME_LENGTH - 1);
        buf->release[_UTSNAME_LENGTH - 1] = '\0';

        strncpy(buf->version, "#1 SMP", _UTSNAME_LENGTH - 1);
        buf->version[_UTSNAME_LENGTH - 1] = '\0';

        strncpy(buf->machine, "aarch64", _UTSNAME_LENGTH - 1);
        buf->machine[_UTSNAME_LENGTH - 1] = '\0';

#ifdef _GNU_SOURCE
        strncpy(buf->domainname, "(none)", _UTSNAME_LENGTH - 1);
        buf->domainname[_UTSNAME_LENGTH - 1] = '\0';
#endif
    }

    return 0;
}
