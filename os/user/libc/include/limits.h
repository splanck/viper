#ifndef _LIMITS_H
#define _LIMITS_H

/* Number of bits in a char */
#define CHAR_BIT 8

/* Minimum and maximum values for signed char */
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127

/* Maximum value for unsigned char */
#define UCHAR_MAX 255

/* Minimum and maximum values for char (assume signed) */
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX

/* Minimum and maximum values for short */
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535

/* Minimum and maximum values for int (32-bit) */
#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U

/* Minimum and maximum values for long (64-bit on AArch64) */
#define LONG_MIN (-9223372036854775807L - 1)
#define LONG_MAX 9223372036854775807L
#define ULONG_MAX 18446744073709551615UL

/* Minimum and maximum values for long long (64-bit) */
#define LLONG_MIN (-9223372036854775807LL - 1)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

/* Size limits */
#define SIZE_MAX ULONG_MAX
#define SSIZE_MAX LONG_MAX

/* Path limits */
#define PATH_MAX 256
#define NAME_MAX 255

/* MB_LEN_MAX - max bytes in multi-byte char */
#define MB_LEN_MAX 4

#endif /* _LIMITS_H */
