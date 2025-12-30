/*
 * ViperOS libc - wchar.h
 * Wide character handling
 */

#ifndef _WCHAR_H
#define _WCHAR_H

#include "stddef.h"
#include "stdint.h"
#include "stdio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wide character type - 32-bit to support full Unicode */
#ifndef __WCHAR_TYPE__
typedef int wchar_t;
#endif

/* wint_t - wide integer type */
typedef int wint_t;

/* mbstate_t - multibyte conversion state */
typedef struct {
    unsigned int __count;
    unsigned int __value;
} mbstate_t;

/* Wide character EOF */
#define WEOF ((wint_t)-1)

/* Multibyte/wide character conversion limits */
#define MB_LEN_MAX 4  /* UTF-8 max bytes per character */

/* struct tm forward declaration */
struct tm;

/*
 * Wide character classification (wctype.h functions)
 */
int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);

wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);

/*
 * Wide string functions
 */
wchar_t *wcscpy(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wcscat(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n);

size_t wcslen(const wchar_t *s);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
int wcscoll(const wchar_t *s1, const wchar_t *s2);
size_t wcsxfrm(wchar_t *dest, const wchar_t *src, size_t n);

wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
size_t wcscspn(const wchar_t *s, const wchar_t *reject);
size_t wcsspn(const wchar_t *s, const wchar_t *accept);
wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept);
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle);
wchar_t *wcstok(wchar_t *str, const wchar_t *delim, wchar_t **saveptr);

/*
 * Wide memory functions
 */
wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);

/*
 * Multibyte/wide character conversion
 */
int mbsinit(const mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dest, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dest, const wchar_t **src, size_t len, mbstate_t *ps);

/* Non-restartable versions */
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *dest, const char *src, size_t n);
size_t wcstombs(char *dest, const wchar_t *src, size_t n);

/* Get number of bytes in current multibyte character */
int mblen(const char *s, size_t n);

/*
 * Wide character numeric conversions
 */
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base);
double wcstod(const wchar_t *nptr, wchar_t **endptr);
float wcstof(const wchar_t *nptr, wchar_t **endptr);
long double wcstold(const wchar_t *nptr, wchar_t **endptr);

/*
 * Wide character I/O (simplified stubs)
 */
wint_t fgetwc(FILE *stream);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t fputwc(wchar_t wc, FILE *stream);
wint_t putwc(wchar_t wc, FILE *stream);
wint_t putwchar(wchar_t wc);

wchar_t *fgetws(wchar_t *s, int n, FILE *stream);
int fputws(const wchar_t *s, FILE *stream);

wint_t ungetwc(wint_t wc, FILE *stream);

int fwide(FILE *stream, int mode);

/* Wide character formatted I/O (not implemented) */
int fwprintf(FILE *stream, const wchar_t *format, ...);
int wprintf(const wchar_t *format, ...);
int swprintf(wchar_t *s, size_t n, const wchar_t *format, ...);

int fwscanf(FILE *stream, const wchar_t *format, ...);
int wscanf(const wchar_t *format, ...);
int swscanf(const wchar_t *s, const wchar_t *format, ...);

/*
 * Wide character time formatting
 */
size_t wcsftime(wchar_t *s, size_t maxsize, const wchar_t *format, const struct tm *timeptr);

/*
 * Wide character duplication (extension)
 */
wchar_t *wcsdup(const wchar_t *s);

#ifdef __cplusplus
}
#endif

#endif /* _WCHAR_H */
