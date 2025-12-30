/*
 * ViperOS libc - wchar.c
 * Wide character handling implementation
 */

#include "../include/wchar.h"
#include "../include/string.h"
#include "../include/stdlib.h"
#include "../include/ctype.h"
#include "../include/errno.h"

/*
 * Wide character classification
 */
int iswalnum(wint_t wc)
{
    return iswalpha(wc) || iswdigit(wc);
}

int iswalpha(wint_t wc)
{
    return (wc >= 'A' && wc <= 'Z') || (wc >= 'a' && wc <= 'z');
}

int iswblank(wint_t wc)
{
    return wc == ' ' || wc == '\t';
}

int iswcntrl(wint_t wc)
{
    return wc < 0x20 || wc == 0x7F;
}

int iswdigit(wint_t wc)
{
    return wc >= '0' && wc <= '9';
}

int iswgraph(wint_t wc)
{
    return wc > 0x20 && wc != 0x7F;
}

int iswlower(wint_t wc)
{
    return wc >= 'a' && wc <= 'z';
}

int iswprint(wint_t wc)
{
    return wc >= 0x20 && wc != 0x7F;
}

int iswpunct(wint_t wc)
{
    return iswgraph(wc) && !iswalnum(wc);
}

int iswspace(wint_t wc)
{
    return wc == ' ' || wc == '\t' || wc == '\n' ||
           wc == '\r' || wc == '\f' || wc == '\v';
}

int iswupper(wint_t wc)
{
    return wc >= 'A' && wc <= 'Z';
}

int iswxdigit(wint_t wc)
{
    return iswdigit(wc) ||
           (wc >= 'A' && wc <= 'F') ||
           (wc >= 'a' && wc <= 'f');
}

wint_t towlower(wint_t wc)
{
    if (wc >= 'A' && wc <= 'Z')
        return wc + ('a' - 'A');
    return wc;
}

wint_t towupper(wint_t wc)
{
    if (wc >= 'a' && wc <= 'z')
        return wc - ('a' - 'A');
    return wc;
}

/*
 * Wide string functions
 */
wchar_t *wcscpy(wchar_t *dest, const wchar_t *src)
{
    wchar_t *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n)
{
    wchar_t *d = dest;
    while (n && (*d++ = *src++))
        n--;
    while (n--)
        *d++ = L'\0';
    return dest;
}

wchar_t *wcscat(wchar_t *dest, const wchar_t *src)
{
    wchar_t *d = dest;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dest;
}

wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n)
{
    wchar_t *d = dest;
    while (*d)
        d++;
    while (n-- && (*d++ = *src++))
        ;
    if (n == (size_t)-1)
        *d = L'\0';
    return dest;
}

size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p)
        p++;
    return p - s;
}

int wcscmp(const wchar_t *s1, const wchar_t *s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
    if (n == 0)
        return 0;
    while (--n && *s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

int wcscoll(const wchar_t *s1, const wchar_t *s2)
{
    /* In C locale, wcscoll is the same as wcscmp */
    return wcscmp(s1, s2);
}

size_t wcsxfrm(wchar_t *dest, const wchar_t *src, size_t n)
{
    /* In C locale, wcsxfrm just copies */
    size_t len = wcslen(src);
    if (n > 0)
    {
        size_t copy = (len < n - 1) ? len : n - 1;
        wmemcpy(dest, src, copy);
        dest[copy] = L'\0';
    }
    return len;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    while (*s)
    {
        if (*s == c)
            return (wchar_t *)s;
        s++;
    }
    return c == L'\0' ? (wchar_t *)s : (wchar_t *)0;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = (wchar_t *)0;
    while (*s)
    {
        if (*s == c)
            last = s;
        s++;
    }
    return c == L'\0' ? (wchar_t *)s : (wchar_t *)last;
}

size_t wcscspn(const wchar_t *s, const wchar_t *reject)
{
    const wchar_t *p = s;
    while (*p)
    {
        const wchar_t *r = reject;
        while (*r)
        {
            if (*p == *r)
                return p - s;
            r++;
        }
        p++;
    }
    return p - s;
}

size_t wcsspn(const wchar_t *s, const wchar_t *accept)
{
    const wchar_t *p = s;
    while (*p)
    {
        const wchar_t *a = accept;
        while (*a && *a != *p)
            a++;
        if (!*a)
            break;
        p++;
    }
    return p - s;
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept)
{
    while (*s)
    {
        const wchar_t *a = accept;
        while (*a)
        {
            if (*s == *a)
                return (wchar_t *)s;
            a++;
        }
        s++;
    }
    return (wchar_t *)0;
}

wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)
{
    if (!*needle)
        return (wchar_t *)haystack;

    size_t needle_len = wcslen(needle);
    while (*haystack)
    {
        if (wcsncmp(haystack, needle, needle_len) == 0)
            return (wchar_t *)haystack;
        haystack++;
    }
    return (wchar_t *)0;
}

wchar_t *wcstok(wchar_t *str, const wchar_t *delim, wchar_t **saveptr)
{
    wchar_t *start;

    if (str == (wchar_t *)0)
        str = *saveptr;

    /* Skip leading delimiters */
    while (*str && wcschr(delim, *str))
        str++;

    if (!*str)
    {
        *saveptr = str;
        return (wchar_t *)0;
    }

    start = str;

    /* Find end of token */
    while (*str && !wcschr(delim, *str))
        str++;

    if (*str)
    {
        *str = L'\0';
        *saveptr = str + 1;
    }
    else
    {
        *saveptr = str;
    }

    return start;
}

/*
 * Wide memory functions
 */
wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, size_t n)
{
    wchar_t *d = dest;
    while (n--)
        *d++ = *src++;
    return dest;
}

wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, size_t n)
{
    if (dest < src)
    {
        return wmemcpy(dest, src, n);
    }
    else if (dest > src)
    {
        wchar_t *d = dest + n;
        const wchar_t *s = src + n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n)
{
    wchar_t *p = s;
    while (n--)
        *p++ = c;
    return s;
}

int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
    while (n--)
    {
        if (*s1 != *s2)
            return *s1 - *s2;
        s1++;
        s2++;
    }
    return 0;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    while (n--)
    {
        if (*s == c)
            return (wchar_t *)s;
        s++;
    }
    return (wchar_t *)0;
}

/*
 * Multibyte/wide character conversion (UTF-8)
 */
int mbsinit(const mbstate_t *ps)
{
    return ps == (mbstate_t *)0 || ps->__count == 0;
}

size_t mbrlen(const char *s, size_t n, mbstate_t *ps)
{
    static mbstate_t internal_state = {0, 0};
    if (ps == (mbstate_t *)0)
        ps = &internal_state;
    return mbrtowc((wchar_t *)0, s, n, ps);
}

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)
{
    static mbstate_t internal_state = {0, 0};
    if (ps == (mbstate_t *)0)
        ps = &internal_state;

    if (s == (char *)0)
    {
        ps->__count = 0;
        ps->__value = 0;
        return 0;
    }

    if (n == 0)
        return (size_t)-2;

    unsigned char c = (unsigned char)*s;

    /* ASCII */
    if (c < 0x80)
    {
        if (pwc)
            *pwc = c;
        return c ? 1 : 0;
    }

    /* Continuation byte without start byte */
    if (c < 0xC0)
    {
        errno = EILSEQ;
        return (size_t)-1;
    }

    /* Multi-byte sequence */
    int count;
    wchar_t wc;

    if (c < 0xE0)
    {
        count = 2;
        wc = c & 0x1F;
    }
    else if (c < 0xF0)
    {
        count = 3;
        wc = c & 0x0F;
    }
    else if (c < 0xF8)
    {
        count = 4;
        wc = c & 0x07;
    }
    else
    {
        errno = EILSEQ;
        return (size_t)-1;
    }

    if (n < (size_t)count)
        return (size_t)-2;

    for (int i = 1; i < count; i++)
    {
        c = (unsigned char)s[i];
        if ((c & 0xC0) != 0x80)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        wc = (wc << 6) | (c & 0x3F);
    }

    if (pwc)
        *pwc = wc;
    return wc ? count : 0;
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
    static mbstate_t internal_state = {0, 0};
    if (ps == (mbstate_t *)0)
        ps = &internal_state;

    if (s == (char *)0)
        return 1;

    /* ASCII */
    if (wc < 0x80)
    {
        *s = (char)wc;
        return 1;
    }

    /* 2-byte sequence */
    if (wc < 0x800)
    {
        s[0] = (char)(0xC0 | (wc >> 6));
        s[1] = (char)(0x80 | (wc & 0x3F));
        return 2;
    }

    /* 3-byte sequence */
    if (wc < 0x10000)
    {
        s[0] = (char)(0xE0 | (wc >> 12));
        s[1] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[2] = (char)(0x80 | (wc & 0x3F));
        return 3;
    }

    /* 4-byte sequence */
    if (wc < 0x110000)
    {
        s[0] = (char)(0xF0 | (wc >> 18));
        s[1] = (char)(0x80 | ((wc >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[3] = (char)(0x80 | (wc & 0x3F));
        return 4;
    }

    errno = EILSEQ;
    return (size_t)-1;
}

size_t mbsrtowcs(wchar_t *dest, const char **src, size_t len, mbstate_t *ps)
{
    static mbstate_t internal_state = {0, 0};
    if (ps == (mbstate_t *)0)
        ps = &internal_state;

    size_t written = 0;
    const char *s = *src;

    while (len > 0)
    {
        wchar_t wc;
        size_t ret = mbrtowc(&wc, s, MB_LEN_MAX, ps);

        if (ret == 0)
        {
            if (dest)
                *dest = L'\0';
            *src = (char *)0;
            return written;
        }

        if (ret == (size_t)-1 || ret == (size_t)-2)
        {
            *src = s;
            return ret;
        }

        if (dest)
            *dest++ = wc;
        s += ret;
        written++;
        len--;
    }

    *src = s;
    return written;
}

size_t wcsrtombs(char *dest, const wchar_t **src, size_t len, mbstate_t *ps)
{
    static mbstate_t internal_state = {0, 0};
    if (ps == (mbstate_t *)0)
        ps = &internal_state;

    size_t written = 0;
    const wchar_t *s = *src;
    char buf[MB_LEN_MAX];

    while (len > 0)
    {
        size_t ret = wcrtomb(buf, *s, ps);

        if (ret == (size_t)-1)
        {
            *src = s;
            return ret;
        }

        if (*s == L'\0')
        {
            if (dest && len >= ret)
                memcpy(dest, buf, ret);
            *src = (wchar_t *)0;
            return written;
        }

        if (len < ret)
            break;

        if (dest)
        {
            memcpy(dest, buf, ret);
            dest += ret;
        }
        s++;
        written += ret;
        len -= ret;
    }

    *src = s;
    return written;
}

/* Non-restartable versions */
int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
    static mbstate_t state = {0, 0};
    if (s == (char *)0)
    {
        state.__count = 0;
        state.__value = 0;
        return 0;
    }
    size_t ret = mbrtowc(pwc, s, n, &state);
    if (ret == (size_t)-1 || ret == (size_t)-2)
        return -1;
    return (int)ret;
}

int wctomb(char *s, wchar_t wc)
{
    static mbstate_t state = {0, 0};
    if (s == (char *)0)
    {
        state.__count = 0;
        state.__value = 0;
        return 0;
    }
    size_t ret = wcrtomb(s, wc, &state);
    if (ret == (size_t)-1)
        return -1;
    return (int)ret;
}

size_t mbstowcs(wchar_t *dest, const char *src, size_t n)
{
    return mbsrtowcs(dest, &src, n, (mbstate_t *)0);
}

size_t wcstombs(char *dest, const wchar_t *src, size_t n)
{
    return wcsrtombs(dest, &src, n, (mbstate_t *)0);
}

int mblen(const char *s, size_t n)
{
    return mbtowc((wchar_t *)0, s, n);
}

/*
 * Wide character numeric conversions
 */
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base)
{
    /* Convert to narrow string and use strtol */
    char buf[64];
    size_t i = 0;
    const wchar_t *p = nptr;

    /* Skip whitespace */
    while (iswspace(*p))
        p++;

    /* Copy number characters */
    if (*p == L'+' || *p == L'-')
        buf[i++] = (char)*p++;

    if (base == 0 || base == 16)
    {
        if (*p == L'0')
        {
            buf[i++] = (char)*p++;
            if (*p == L'x' || *p == L'X')
            {
                buf[i++] = (char)*p++;
                if (base == 0)
                    base = 16;
            }
            else if (base == 0)
            {
                base = 8;
            }
        }
        else if (base == 0)
        {
            base = 10;
        }
    }

    while (i < sizeof(buf) - 1)
    {
        wchar_t c = *p;
        if (iswdigit(c))
            buf[i++] = (char)c;
        else if ((unsigned)c >= L'a' && (unsigned)c <= L'z')
            buf[i++] = (char)c;
        else if ((unsigned)c >= L'A' && (unsigned)c <= L'Z')
            buf[i++] = (char)c;
        else
            break;
        p++;
    }
    buf[i] = '\0';

    char *end;
    long result = strtol(buf, &end, base);

    if (endptr)
        *endptr = (wchar_t *)(nptr + (end - buf));

    return result;
}

unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base)
{
    return (unsigned long)wcstol(nptr, endptr, base);
}

long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base)
{
    return (long long)wcstol(nptr, endptr, base);
}

unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base)
{
    return (unsigned long long)wcstoul(nptr, endptr, base);
}

double wcstod(const wchar_t *nptr, wchar_t **endptr)
{
    /* Convert to narrow string and use strtod */
    char buf[128];
    size_t i = 0;
    const wchar_t *p = nptr;

    while (iswspace(*p))
        p++;

    while (i < sizeof(buf) - 1 && *p && !iswspace(*p))
    {
        if (*p < 128)
            buf[i++] = (char)*p;
        p++;
    }
    buf[i] = '\0';

    char *end;
    double result = strtod(buf, &end);

    if (endptr)
        *endptr = (wchar_t *)(nptr + (end - buf));

    return result;
}

float wcstof(const wchar_t *nptr, wchar_t **endptr)
{
    return (float)wcstod(nptr, endptr);
}

long double wcstold(const wchar_t *nptr, wchar_t **endptr)
{
    return (long double)wcstod(nptr, endptr);
}

/*
 * Wide character I/O (simplified)
 */
wint_t fgetwc(FILE *stream)
{
    int c = fgetc(stream);
    if (c == EOF)
        return WEOF;

    /* Handle UTF-8 */
    if (c < 0x80)
        return (wint_t)c;

    /* Multi-byte UTF-8 */
    wchar_t wc;
    char buf[4];
    buf[0] = (char)c;

    int count;
    if (c < 0xE0)
        count = 2;
    else if (c < 0xF0)
        count = 3;
    else
        count = 4;

    for (int i = 1; i < count; i++)
    {
        c = fgetc(stream);
        if (c == EOF)
            return WEOF;
        buf[i] = (char)c;
    }

    mbrtowc(&wc, buf, count, (mbstate_t *)0);
    return (wint_t)wc;
}

wint_t getwc(FILE *stream)
{
    return fgetwc(stream);
}

wint_t getwchar(void)
{
    return fgetwc(stdin);
}

wint_t fputwc(wchar_t wc, FILE *stream)
{
    char buf[MB_LEN_MAX];
    size_t n = wcrtomb(buf, wc, (mbstate_t *)0);

    if (n == (size_t)-1)
        return WEOF;

    for (size_t i = 0; i < n; i++)
    {
        if (fputc(buf[i], stream) == EOF)
            return WEOF;
    }

    return (wint_t)wc;
}

wint_t putwc(wchar_t wc, FILE *stream)
{
    return fputwc(wc, stream);
}

wint_t putwchar(wchar_t wc)
{
    return fputwc(wc, stdout);
}

wchar_t *fgetws(wchar_t *s, int n, FILE *stream)
{
    if (n <= 0)
        return (wchar_t *)0;

    wchar_t *p = s;
    n--; /* Leave room for null terminator */

    while (n > 0)
    {
        wint_t wc = fgetwc(stream);
        if (wc == WEOF)
        {
            if (p == s)
                return (wchar_t *)0;
            break;
        }
        *p++ = (wchar_t)wc;
        n--;
        if (wc == L'\n')
            break;
    }

    *p = L'\0';
    return s;
}

int fputws(const wchar_t *s, FILE *stream)
{
    while (*s)
    {
        if (fputwc(*s++, stream) == WEOF)
            return -1;
    }
    return 0;
}

wint_t ungetwc(wint_t wc, FILE *stream)
{
    if (wc == WEOF)
        return WEOF;

    /* For simplicity, only support ASCII unget */
    if (wc < 0x80)
    {
        if (ungetc((int)wc, stream) == EOF)
            return WEOF;
        return wc;
    }

    /* Multi-byte not supported */
    return WEOF;
}

int fwide(FILE *stream, int mode)
{
    (void)stream;
    (void)mode;
    /* Always return 0 (no orientation) */
    return 0;
}

/*
 * Wide character formatted I/O stubs
 */
int fwprintf(FILE *stream, const wchar_t *format, ...)
{
    (void)stream;
    (void)format;
    errno = ENOTSUP;
    return -1;
}

int wprintf(const wchar_t *format, ...)
{
    (void)format;
    errno = ENOTSUP;
    return -1;
}

int swprintf(wchar_t *s, size_t n, const wchar_t *format, ...)
{
    (void)s;
    (void)n;
    (void)format;
    errno = ENOTSUP;
    return -1;
}

int fwscanf(FILE *stream, const wchar_t *format, ...)
{
    (void)stream;
    (void)format;
    errno = ENOTSUP;
    return -1;
}

int wscanf(const wchar_t *format, ...)
{
    (void)format;
    errno = ENOTSUP;
    return -1;
}

int swscanf(const wchar_t *s, const wchar_t *format, ...)
{
    (void)s;
    (void)format;
    errno = ENOTSUP;
    return -1;
}

/*
 * Wide character time formatting stub
 */
size_t wcsftime(wchar_t *s, size_t maxsize, const wchar_t *format, const struct tm *timeptr)
{
    (void)s;
    (void)maxsize;
    (void)format;
    (void)timeptr;
    /* Not implemented */
    return 0;
}

/*
 * Wide character duplication
 */
wchar_t *wcsdup(const wchar_t *s)
{
    size_t len = wcslen(s) + 1;
    wchar_t *dup = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (dup)
        wmemcpy(dup, s, len);
    return dup;
}
