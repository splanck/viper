#ifndef _STDDEF_H
#define _STDDEF_H

#ifdef __cplusplus
extern "C" {
#endif

/* size_t - unsigned type for sizes */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

/* ssize_t - signed size type */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

/* ptrdiff_t - signed type for pointer differences */
#ifndef _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
typedef long ptrdiff_t;
#endif

/* wchar_t - wide character type */
#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
#ifndef __cplusplus
typedef int wchar_t;
#endif
#endif

/* NULL pointer constant */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* offsetof - offset of member in struct */
#define offsetof(type, member) __builtin_offsetof(type, member)

/* max_align_t - type with strictest alignment */
typedef long double max_align_t;

#ifdef __cplusplus
}
#endif

#endif /* _STDDEF_H */
