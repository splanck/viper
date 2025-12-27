#include "../include/string.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s)
    {
        while (n--)
        {
            *d++ = *s++;
        }
    }
    else
    {
        d += n;
        s += n;
        while (n--)
        {
            *--d = *--s;
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (n--)
    {
        if (*p1 != *p2)
        {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++)
    {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n && (*d++ = *src++))
    {
        n--;
    }
    while (n--)
    {
        *d++ = '\0';
    }
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s1 == *s2)
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d)
    {
        d++;
    }
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c)
        {
            return (char *)s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)0;
}

char *strrchr(const char *s, int c)
{
    const char *last = (char *)0;
    while (*s)
    {
        if (*s == (char)c)
        {
            last = s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--)
    {
        if (*p == (unsigned char)c)
        {
            return (void *)p;
        }
        p++;
    }
    return (void *)0;
}

size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (len < maxlen && s[len])
    {
        len++;
    }
    return len;
}

size_t strlcpy(char *dest, const char *src, size_t size)
{
    size_t src_len = strlen(src);
    if (size > 0)
    {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
    }
    return src_len;
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2)
        {
            return (unsigned char)c1 - (unsigned char)c2;
        }
        s1++;
        s2++;
    }
    char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
    char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
    return (unsigned char)c1 - (unsigned char)c2;
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s2)
    {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2)
        {
            return (unsigned char)c1 - (unsigned char)c2;
        }
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
    char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
    return (unsigned char)c1 - (unsigned char)c2;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d)
    {
        d++;
    }
    while (n-- && *src)
    {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

size_t strlcat(char *dest, const char *src, size_t size)
{
    size_t dest_len = strnlen(dest, size);
    size_t src_len = strlen(src);

    if (dest_len >= size)
    {
        return size + src_len;
    }

    size_t copy_len = (src_len >= size - dest_len) ? size - dest_len - 1 : src_len;
    memcpy(dest + dest_len, src, copy_len);
    dest[dest_len + copy_len] = '\0';

    return dest_len + src_len;
}

char *strstr(const char *haystack, const char *needle)
{
    if (*needle == '\0')
    {
        return (char *)haystack;
    }

    size_t needle_len = strlen(needle);
    while (*haystack)
    {
        if (*haystack == *needle)
        {
            if (strncmp(haystack, needle, needle_len) == 0)
            {
                return (char *)haystack;
            }
        }
        haystack++;
    }
    return (char *)0;
}

char *strpbrk(const char *s, const char *accept)
{
    while (*s)
    {
        const char *a = accept;
        while (*a)
        {
            if (*s == *a)
            {
                return (char *)s;
            }
            a++;
        }
        s++;
    }
    return (char *)0;
}

size_t strspn(const char *s, const char *accept)
{
    size_t count = 0;
    while (*s)
    {
        const char *a = accept;
        int found = 0;
        while (*a)
        {
            if (*s == *a)
            {
                found = 1;
                break;
            }
            a++;
        }
        if (!found)
        {
            break;
        }
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t count = 0;
    while (*s)
    {
        const char *r = reject;
        while (*r)
        {
            if (*s == *r)
            {
                return count;
            }
            r++;
        }
        count++;
        s++;
    }
    return count;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *token;

    if (str == (char *)0)
    {
        str = *saveptr;
    }

    /* Skip leading delimiters */
    str += strspn(str, delim);
    if (*str == '\0')
    {
        *saveptr = str;
        return (char *)0;
    }

    /* Find end of token */
    token = str;
    str = strpbrk(token, delim);
    if (str == (char *)0)
    {
        /* No more delimiters */
        *saveptr = token + strlen(token);
    }
    else
    {
        *str = '\0';
        *saveptr = str + 1;
    }

    return token;
}

/* Forward declaration for malloc - implemented in stdlib.c */
extern void *malloc(size_t size);

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *dup = (char *)malloc(len);
    if (dup)
    {
        memcpy(dup, s, len);
    }
    return dup;
}

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *dup = (char *)malloc(len + 1);
    if (dup)
    {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}
