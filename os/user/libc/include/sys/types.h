#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* Basic types */
    typedef long ssize_t;
    typedef unsigned long size_t;
    typedef long off_t;
    typedef int pid_t;
    typedef unsigned int uid_t;
    typedef unsigned int gid_t;
    typedef unsigned int id_t;
    typedef unsigned int mode_t;
    typedef unsigned long dev_t;
    typedef unsigned long ino_t;
    typedef unsigned long nlink_t;
    typedef long blksize_t;
    typedef long blkcnt_t;
    typedef long time_t;
    typedef unsigned int useconds_t;
    typedef long suseconds_t;
    typedef int clockid_t;

/* Fixed-width types if not already defined */
#ifndef _STDINT_TYPES_DEFINED
#define _STDINT_TYPES_DEFINED
    typedef signed char int8_t;
    typedef unsigned char uint8_t;
    typedef signed short int16_t;
    typedef unsigned short uint16_t;
    typedef signed int int32_t;
    typedef unsigned int uint32_t;
    typedef signed long long int64_t;
    typedef unsigned long long uint64_t;
    typedef long intptr_t;
    typedef unsigned long uintptr_t;
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TYPES_H */
