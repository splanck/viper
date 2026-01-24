//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/string.c
// Purpose: String and memory manipulation functions for ViperDOS libc.
// Key invariants: Standard C library semantics; no dependencies.
// Ownership/Lifetime: Library; all functions are stateless.
// Links: user/libc/include/string.h
//
//===----------------------------------------------------------------------===//

/**
 * @file string.c
 * @brief String and memory manipulation functions for ViperDOS libc.
 *
 * @details
 * This file implements standard C string and memory functions including:
 *
 * - Memory operations: memcpy, memset, memmove, memcmp, memchr
 * - String operations: strlen, strcpy, strncpy, strcat, strncat
 * - String comparison: strcmp, strncmp, strcasecmp, strncasecmp
 * - String searching: strchr, strrchr, strstr, strpbrk, strspn, strcspn
 * - Tokenization: strtok, strtok_r
 * - Other: strerror, strdup, strndup
 *
 * All implementations are freestanding (no external dependencies) and
 * follow standard C library semantics.
 */

#include "../include/string.h"

/**
 * @brief Copy memory area.
 *
 * @details
 * Copies n bytes from src to dest. The memory areas must not overlap.
 * For overlapping memory, use memmove() instead.
 *
 * @param dest Destination buffer.
 * @param src Source buffer.
 * @param n Number of bytes to copy.
 * @return Pointer to dest.
 */
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

/**
 * @brief Fill memory with a constant byte.
 *
 * @details Fills the first @p n bytes of the memory area pointed to by @p s
 * with the constant byte @p c (converted to unsigned char). This is commonly
 * used to initialize memory to zero or a specific pattern.
 *
 * Common usage patterns:
 * @code
 * memset(buffer, 0, sizeof(buffer));      // Zero-initialize
 * memset(password, 'X', len);              // Overwrite sensitive data
 * @endcode
 *
 * @param s Pointer to the memory area to fill.
 * @param c Byte value to fill with (only low 8 bits are used).
 * @param n Number of bytes to fill.
 *
 * @return Pointer to the memory area @p s.
 *
 * @note For zeroing memory that may be optimized away, use explicit_bzero()
 *       or volatile pointers when clearing sensitive data.
 */
void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

/**
 * @brief Copy memory area, handling overlapping regions safely.
 *
 * @details Copies @p n bytes from @p src to @p dest. Unlike memcpy(), memmove()
 * correctly handles the case where the source and destination memory regions
 * overlap. The copy is performed as if the bytes were first copied to a
 * temporary buffer, then copied to the destination.
 *
 * Use memmove() when:
 * - Source and destination may overlap
 * - You're shifting data within the same buffer
 * - You're not certain whether regions overlap
 *
 * @param dest Destination memory area.
 * @param src  Source memory area.
 * @param n    Number of bytes to copy.
 *
 * @return Pointer to @p dest.
 *
 * @note For non-overlapping memory regions, memcpy() may be faster.
 * @note The implementation copies forward if dest < src, backward otherwise.
 */
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

/**
 * @brief Compare two memory areas byte-by-byte.
 *
 * @details Compares the first @p n bytes of memory areas @p s1 and @p s2.
 * The comparison is performed using unsigned char values, so this function
 * is suitable for comparing binary data.
 *
 * @param s1 First memory area to compare.
 * @param s2 Second memory area to compare.
 * @param n  Number of bytes to compare.
 *
 * @return An integer less than, equal to, or greater than zero if the first
 *         @p n bytes of @p s1 are found to be less than, equal to, or greater
 *         than the first @p n bytes of @p s2, respectively.
 *
 * @note Returns 0 if n is 0 (zero-length comparison always succeeds).
 * @note Not suitable for comparing strings (use strcmp() for that).
 */
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

/**
 * @brief Calculate the length of a string.
 *
 * @details Calculates the length of the string pointed to by @p s, excluding
 * the terminating null byte. This function scans the entire string, so for
 * very long strings or when the length is known, consider alternatives.
 *
 * @param s Pointer to a null-terminated string.
 *
 * @return The number of bytes in the string, not including the null terminator.
 *
 * @note Behavior is undefined if @p s is not null-terminated.
 * @note For bounded string length, use strnlen() instead.
 *
 * @see strnlen, sizeof
 */
size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++)
    {
        len++;
    }
    return len;
}

/**
 * @brief Copy a string (UNSAFE - prefer strncpy or strlcpy).
 *
 * @details Copies the string pointed to by @p src, including the null terminator,
 * to the buffer pointed to by @p dest. The strings must not overlap, and the
 * destination buffer must be large enough to receive the copy.
 *
 * @warning This function performs no bounds checking. Buffer overflows are a
 *          common source of security vulnerabilities. Use strncpy() or strlcpy()
 *          for safer alternatives.
 *
 * @param dest Destination buffer. Must be large enough for src plus null terminator.
 * @param src  Source string to copy (must be null-terminated).
 *
 * @return Pointer to @p dest.
 *
 * @see strncpy, strlcpy (safer alternatives)
 */
char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

/**
 * @brief Copy a string with length limit.
 *
 * @details Copies at most @p n characters from @p src to @p dest. If @p src is
 * shorter than @p n characters, the remainder of @p dest is filled with null
 * bytes. If @p src is @p n characters or longer, @p dest will NOT be null-terminated.
 *
 * @warning The destination string is NOT guaranteed to be null-terminated!
 *          If strlen(src) >= n, no null terminator is written. Consider using
 *          strlcpy() for guaranteed null-termination.
 *
 * @param dest Destination buffer (at least @p n bytes).
 * @param src  Source string.
 * @param n    Maximum number of characters to copy.
 *
 * @return Pointer to @p dest.
 *
 * @note Pads with null bytes if src is shorter than n (wastes cycles for large n).
 * @note Originally designed for fixed-width database fields, not general strings.
 *
 * @see strlcpy (safer alternative with guaranteed null-termination)
 */
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

/**
 * @brief Compare two strings lexicographically.
 *
 * @details Compares the two strings @p s1 and @p s2 character by character
 * using unsigned char values. The comparison stops at the first difference
 * or at the null terminator.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 *
 * @return An integer less than, equal to, or greater than zero if @p s1 is
 *         found to be less than, equal to, or greater than @p s2.
 *         - < 0: s1 is less than s2
 *         - = 0: s1 equals s2
 *         - > 0: s1 is greater than s2
 *
 * @note Comparison is case-sensitive. Use strcasecmp() for case-insensitive.
 * @note For bounded comparison, use strncmp().
 *
 * @see strncmp, strcasecmp
 */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * @brief Compare at most n characters of two strings.
 *
 * @details Compares at most @p n characters of strings @p s1 and @p s2. The
 * comparison stops at the first difference, a null terminator, or after @p n
 * characters have been compared.
 *
 * Useful for:
 * - Comparing prefixes: strncmp(str, "prefix", 6)
 * - Comparing fixed-length fields
 * - Safely comparing potentially unterminated strings
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @param n  Maximum number of characters to compare.
 *
 * @return An integer less than, equal to, or greater than zero if the first
 *         @p n characters of @p s1 are found to be less than, equal to, or
 *         greater than those of @p s2.
 *
 * @note Returns 0 if n is 0.
 *
 * @see strcmp, strncasecmp
 */
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

/**
 * @brief Concatenate two strings (UNSAFE - prefer strncat or strlcat).
 *
 * @details Appends the @p src string to the @p dest string, overwriting the
 * null terminator at the end of @p dest and adding a new null terminator.
 * The strings must not overlap, and @p dest must have enough space for the
 * result.
 *
 * @warning This function performs no bounds checking. Use strncat() or
 *          strlcat() for safer alternatives.
 *
 * @param dest Destination string to append to. Must have enough space for
 *             strlen(dest) + strlen(src) + 1 bytes.
 * @param src  Source string to append.
 *
 * @return Pointer to @p dest.
 *
 * @see strncat, strlcat (safer alternatives)
 */
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

/**
 * @brief Locate the first occurrence of a character in a string.
 *
 * @details Returns a pointer to the first occurrence of the character @p c
 * in the string @p s. The null terminator is considered part of the string,
 * so strchr(s, '\0') returns a pointer to the terminator.
 *
 * @param s String to search.
 * @param c Character to search for (converted to char).
 *
 * @return Pointer to the first occurrence of @p c in @p s, or NULL if the
 *         character is not found (except for '\0' which always succeeds).
 *
 * @note To find the last occurrence, use strrchr().
 *
 * @see strrchr, memchr, strstr
 */
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

/**
 * @brief Locate the last occurrence of a character in a string.
 *
 * @details Returns a pointer to the last occurrence of the character @p c
 * in the string @p s. The null terminator is considered part of the string,
 * so strrchr(s, '\0') returns a pointer to the terminator.
 *
 * Common use case - extract filename from path:
 * @code
 * const char *path = "/usr/bin/ls";
 * const char *filename = strrchr(path, '/');
 * if (filename) filename++;  // Skip the '/'
 * @endcode
 *
 * @param s String to search.
 * @param c Character to search for (converted to char).
 *
 * @return Pointer to the last occurrence of @p c in @p s, or NULL if the
 *         character is not found (except for '\0' which always succeeds).
 *
 * @see strchr, memrchr
 */
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

/**
 * @brief Scan memory for a byte value.
 *
 * @details Scans the initial @p n bytes of the memory area pointed to by @p s
 * for the first instance of @p c (interpreted as an unsigned char).
 *
 * @param s Memory area to search.
 * @param c Byte value to search for (converted to unsigned char).
 * @param n Number of bytes to search.
 *
 * @return Pointer to the matching byte, or NULL if the character does not
 *         occur in the given memory area.
 *
 * @note Unlike strchr(), this function works on raw memory and uses a length
 *       limit rather than stopping at null bytes.
 *
 * @see strchr, memrchr, memmem
 */
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

/**
 * @brief Determine the length of a fixed-size string.
 *
 * @details Returns the number of characters in the string pointed to by @p s,
 * excluding the terminating null byte, but at most @p maxlen. This function
 * never accesses more than @p maxlen bytes from @p s.
 *
 * Safe alternative to strlen() when the string might not be null-terminated
 * or when you want to limit the scan for performance reasons.
 *
 * @param s      String to measure.
 * @param maxlen Maximum number of bytes to examine.
 *
 * @return The lesser of strlen(s) and maxlen. If no null byte is found in
 *         the first maxlen bytes, returns maxlen.
 *
 * @see strlen
 */
size_t strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    while (len < maxlen && s[len])
    {
        len++;
    }
    return len;
}

/**
 * @brief Copy a string with guaranteed null-termination (BSD extension).
 *
 * @details Copies up to size-1 characters from @p src to @p dest, guaranteeing
 * null-termination if size > 0. Returns the total length of the string that
 * would have been created if there was unlimited space.
 *
 * Safer alternative to strcpy() and strncpy():
 * - Always null-terminates (unlike strncpy with full buffer)
 * - Returns total length needed (enables truncation detection)
 * - Doesn't waste time padding with nulls (unlike strncpy)
 *
 * Truncation detection:
 * @code
 * if (strlcpy(dest, src, sizeof(dest)) >= sizeof(dest)) {
 *     // Truncation occurred
 * }
 * @endcode
 *
 * @param dest Destination buffer.
 * @param src  Source string.
 * @param size Size of destination buffer (including space for null terminator).
 *
 * @return Length of @p src (not the number of characters copied). If this
 *         is >= size, truncation occurred.
 *
 * @note BSD extension, not part of standard C.
 * @note If size is 0, dest is not modified and strlen(src) is returned.
 */
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

/**
 * @brief Compare two strings ignoring case.
 *
 * @details Compares the two strings @p s1 and @p s2, ignoring the case of
 * ASCII letters (A-Z are treated as equivalent to a-z). The comparison
 * is performed using the lowercase equivalent of each character.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 *
 * @return An integer less than, equal to, or greater than zero if @p s1 is
 *         found to be less than, equal to, or greater than @p s2 (ignoring case).
 *
 * @note Only ASCII letters are case-folded; locale is not considered.
 * @note POSIX extension, not standard C.
 *
 * @see strncasecmp, strcmp
 */
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

/**
 * @brief Compare at most n characters of two strings, ignoring case.
 *
 * @details Compares at most @p n characters of @p s1 and @p s2, treating
 * uppercase and lowercase ASCII letters as equivalent. Comparison stops at
 * the first difference, a null terminator, or after @p n characters.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @param n  Maximum number of characters to compare.
 *
 * @return An integer less than, equal to, or greater than zero if the first
 *         @p n characters of @p s1 are found to be less than, equal to, or
 *         greater than those of @p s2 (ignoring case).
 *
 * @note Returns 0 if n is 0.
 * @note POSIX extension.
 *
 * @see strcasecmp, strncmp
 */
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

/**
 * @brief Concatenate at most n characters from a string.
 *
 * @details Appends at most @p n characters from @p src to @p dest, plus a
 * terminating null byte. If @p src is less than @p n characters, only the
 * characters up to and including the null terminator are appended.
 *
 * @warning The @p n parameter limits characters from src, NOT the total
 *          destination size. You must ensure dest has room for
 *          strlen(dest) + min(strlen(src), n) + 1 bytes.
 *
 * @param dest Destination string (must be null-terminated).
 * @param src  Source string to append from.
 * @param n    Maximum number of characters to append from src.
 *
 * @return Pointer to @p dest.
 *
 * @note Unlike strncpy(), strncat() always null-terminates.
 * @note Consider using strlcat() for clearer size semantics.
 *
 * @see strlcat, strcat
 */
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

/**
 * @brief Concatenate strings with total size limit (BSD extension).
 *
 * @details Appends @p src to @p dest, guaranteeing null-termination and never
 * writing more than @p size bytes total. Returns the total length of the
 * string that would have been created if there was unlimited space.
 *
 * Unlike strncat(), the @p size parameter is the total size of the destination
 * buffer, not the number of characters to append. This makes it easier to use
 * correctly.
 *
 * Truncation detection:
 * @code
 * if (strlcat(dest, src, sizeof(dest)) >= sizeof(dest)) {
 *     // Truncation occurred
 * }
 * @endcode
 *
 * @param dest Destination buffer (must be null-terminated).
 * @param src  Source string to append.
 * @param size Total size of destination buffer.
 *
 * @return The total length that would have been created: strlen(dest) + strlen(src).
 *         If this is >= size, truncation occurred.
 *
 * @note BSD extension.
 * @note If size <= strlen(dest), nothing is appended and size + strlen(src) is returned.
 */
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

/**
 * @brief Locate a substring within a string.
 *
 * @details Finds the first occurrence of the substring @p needle in the
 * string @p haystack. The terminating null bytes are not compared.
 *
 * @param haystack String to search in.
 * @param needle   Substring to search for.
 *
 * @return Pointer to the beginning of the located substring within haystack,
 *         or NULL if the substring is not found. If needle is an empty string,
 *         haystack is returned.
 *
 * @note This is a simple O(n*m) implementation. For repeated searches or
 *       very long strings, consider Boyer-Moore or KMP algorithms.
 *
 * @see strchr, memmem
 */
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

/**
 * @brief Search a string for any of a set of characters.
 *
 * @details Locates the first occurrence in @p s of any character in the
 * string @p accept. This is useful for finding delimiters or special characters.
 *
 * Example - find first vowel:
 * @code
 * char *p = strpbrk("hello", "aeiou");  // Returns pointer to 'e'
 * @endcode
 *
 * @param s      String to search.
 * @param accept Set of characters to search for (as a string).
 *
 * @return Pointer to the first character in @p s that matches any character
 *         in @p accept, or NULL if no such character is found.
 *
 * @see strchr, strspn, strcspn
 */
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

/**
 * @brief Get length of prefix consisting only of accepted characters.
 *
 * @details Calculates the length of the initial segment of @p s which consists
 * entirely of characters in @p accept. Useful for skipping over allowed
 * characters.
 *
 * Example - count leading digits:
 * @code
 * size_t n = strspn("12345abc", "0123456789");  // Returns 5
 * @endcode
 *
 * @param s      String to examine.
 * @param accept Set of allowed characters (as a string).
 *
 * @return The number of bytes in the initial segment of @p s which consist
 *         only of characters from @p accept.
 *
 * @see strcspn, strpbrk
 */
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

/**
 * @brief Get length of prefix not containing rejected characters.
 *
 * @details Calculates the length of the initial segment of @p s which does not
 * contain any character from @p reject. Useful for finding the position of
 * the first delimiter.
 *
 * Example - find length before first delimiter:
 * @code
 * size_t n = strcspn("hello, world", ", ");  // Returns 5
 * @endcode
 *
 * @param s      String to examine.
 * @param reject Set of characters to stop at (as a string).
 *
 * @return The number of bytes in the initial segment of @p s which do not
 *         contain any character from @p reject. If no character from reject
 *         is found, returns strlen(s).
 *
 * @see strspn, strpbrk, strtok
 */
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

/**
 * @brief Extract tokens from a string (reentrant version).
 *
 * @details Breaks string @p str into a sequence of tokens, each delimited by
 * a character from @p delim. On the first call, @p str should point to the
 * string to tokenize. On subsequent calls, @p str should be NULL to continue
 * tokenizing the same string.
 *
 * This reentrant version uses @p saveptr to maintain state between calls,
 * making it thread-safe and allowing nested tokenization.
 *
 * Example usage:
 * @code
 * char str[] = "hello,world,foo";
 * char *saveptr;
 * char *token = strtok_r(str, ",", &saveptr);
 * while (token != NULL) {
 *     printf("Token: %s\n", token);
 *     token = strtok_r(NULL, ",", &saveptr);
 * }
 * @endcode
 *
 * @param str     String to tokenize (first call) or NULL (subsequent calls).
 * @param delim   String of delimiter characters.
 * @param saveptr Pointer to a char* used internally to maintain position.
 *
 * @return Pointer to the next token, or NULL if there are no more tokens.
 *
 * @warning This function modifies the original string by inserting null bytes.
 * @note Leading delimiters are skipped.
 *
 * @see strtok (non-reentrant version), strpbrk, strspn
 */
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

/**
 * @brief Duplicate a string (allocates memory).
 *
 * @details Allocates sufficient memory for a copy of @p s, copies the string,
 * and returns a pointer to the copy. The memory should be freed with free()
 * when no longer needed.
 *
 * @param s String to duplicate.
 *
 * @return Pointer to a newly allocated copy of the string, or NULL if memory
 *         allocation fails.
 *
 * @note The caller is responsible for freeing the returned string.
 * @note POSIX function, not standard C (until C23).
 *
 * @see strndup, malloc, free
 */
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

/**
 * @brief Duplicate at most n characters of a string (allocates memory).
 *
 * @details Allocates memory and copies at most @p n characters from @p s,
 * always adding a terminating null byte. If @p s is shorter than @p n
 * characters, only the existing characters are copied.
 *
 * Useful for extracting substrings:
 * @code
 * char *first_word = strndup(sentence, strcspn(sentence, " "));
 * @endcode
 *
 * @param s String to duplicate.
 * @param n Maximum number of characters to copy.
 *
 * @return Pointer to a newly allocated string of at most n+1 bytes, or NULL
 *         if memory allocation fails.
 *
 * @note The result is always null-terminated.
 * @note The caller must free the returned string.
 *
 * @see strdup, malloc, free
 */
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

/**
 * @defgroup strerror_impl Error String Functions
 * @brief Functions for converting errno values to human-readable strings.
 * @{
 */

/** Error messages indexed by errno value. */
static const char *error_messages[] = {
    "Success",                          /* 0 */
    "Operation not permitted",          /* EPERM 1 */
    "No such file or directory",        /* ENOENT 2 */
    "No such process",                  /* ESRCH 3 */
    "Interrupted system call",          /* EINTR 4 */
    "I/O error",                        /* EIO 5 */
    "No such device or address",        /* ENXIO 6 */
    "Argument list too long",           /* E2BIG 7 */
    "Exec format error",                /* ENOEXEC 8 */
    "Bad file descriptor",              /* EBADF 9 */
    "No child processes",               /* ECHILD 10 */
    "Resource temporarily unavailable", /* EAGAIN 11 */
    "Out of memory",                    /* ENOMEM 12 */
    "Permission denied",                /* EACCES 13 */
    "Bad address",                      /* EFAULT 14 */
    "Block device required",            /* ENOTBLK 15 */
    "Device or resource busy",          /* EBUSY 16 */
    "File exists",                      /* EEXIST 17 */
    "Cross-device link",                /* EXDEV 18 */
    "No such device",                   /* ENODEV 19 */
    "Not a directory",                  /* ENOTDIR 20 */
    "Is a directory",                   /* EISDIR 21 */
    "Invalid argument",                 /* EINVAL 22 */
    "File table overflow",              /* ENFILE 23 */
    "Too many open files",              /* EMFILE 24 */
    "Not a typewriter",                 /* ENOTTY 25 */
    "Text file busy",                   /* ETXTBSY 26 */
    "File too large",                   /* EFBIG 27 */
    "No space left on device",          /* ENOSPC 28 */
    "Illegal seek",                     /* ESPIPE 29 */
    "Read-only file system",            /* EROFS 30 */
    "Too many links",                   /* EMLINK 31 */
    "Broken pipe",                      /* EPIPE 32 */
    "Math argument out of domain",      /* EDOM 33 */
    "Math result not representable",    /* ERANGE 34 */
    "Resource deadlock would occur",    /* EDEADLK 35 */
    "File name too long",               /* ENAMETOOLONG 36 */
    "No record locks available",        /* ENOLCK 37 */
    "Function not implemented",         /* ENOSYS 38 */
    "Directory not empty",              /* ENOTEMPTY 39 */
    "Too many symbolic links",          /* ELOOP 40 */
};

/** Number of defined error messages. */
#define NUM_ERROR_MESSAGES (sizeof(error_messages) / sizeof(error_messages[0]))

/** Buffer for generating "Unknown error N" messages. */
static char unknown_error_buf[32];

/**
 * @brief Get string describing error number.
 *
 * @details Returns a pointer to a string that describes the error code passed
 * in @p errnum. The string should not be modified by the application. It may
 * be overwritten by subsequent calls to strerror().
 *
 * Common usage with perror-style output:
 * @code
 * FILE *f = fopen("missing.txt", "r");
 * if (!f) {
 *     fprintf(stderr, "Error: %s\n", strerror(errno));
 * }
 * @endcode
 *
 * @param errnum Error number (typically from errno).
 *
 * @return Pointer to error description string. For unknown error numbers,
 *         returns a string in the form "Unknown error N".
 *
 * @warning The returned string may be statically allocated and overwritten
 *          by subsequent calls. Do not modify or free it.
 * @note Not thread-safe for unknown error numbers (uses static buffer).
 *       Use strerror_r() for thread-safety.
 *
 * @see perror, errno
 */
char *strerror(int errnum)
{
    if (errnum >= 0 && (size_t)errnum < NUM_ERROR_MESSAGES)
    {
        return (char *)error_messages[errnum];
    }

    /* Handle unknown error */
    char *p = unknown_error_buf;
    const char *prefix = "Unknown error ";
    while (*prefix)
        *p++ = *prefix++;

    /* Convert number to string */
    int n = errnum;
    if (n < 0)
    {
        *p++ = '-';
        n = -n;
    }
    char digits[12];
    int i = 0;
    do
    {
        digits[i++] = '0' + (n % 10);
        n /= 10;
    } while (n > 0);
    while (i > 0)
        *p++ = digits[--i];
    *p = '\0';

    return unknown_error_buf;
}

/**
 * @brief Get the length of an error message string.
 *
 * @details Returns the length of the error message that would be returned by
 * strerror(errnum). This is useful for allocating buffers for strerror_s().
 *
 * @param errnum Error number.
 *
 * @return Length of the corresponding error message (not including null terminator).
 *
 * @note C11 Annex K (bounds-checking interfaces) function.
 *
 * @see strerror
 */
size_t strerrorlen_s(int errnum)
{
    return strlen(strerror(errnum));
}

/** @} */ /* End of strerror_impl group */

/** Static state for non-reentrant strtok(). */
static char *strtok_saveptr = (char *)0;

/**
 * @brief Extract tokens from a string (NON-REENTRANT).
 *
 * @details Breaks string @p str into tokens separated by characters in @p delim.
 * On first call, pass the string to tokenize. On subsequent calls, pass NULL
 * to continue with the same string.
 *
 * @warning This function is NOT thread-safe and cannot be used for nested
 *          tokenization. Use strtok_r() instead.
 *
 * @param str   String to tokenize (first call) or NULL (subsequent calls).
 * @param delim String of delimiter characters.
 *
 * @return Pointer to next token, or NULL if no more tokens.
 *
 * @warning Modifies the original string.
 *
 * @see strtok_r (thread-safe, reentrant version)
 */
char *strtok(char *str, const char *delim)
{
    return strtok_r(str, delim, &strtok_saveptr);
}

/**
 * @brief Scan memory backwards for a byte value.
 *
 * @details Like memchr(), but searches backwards from the end of the memory
 * area. Returns a pointer to the last occurrence of @p c in the first @p n
 * bytes of @p s.
 *
 * @param s Memory area to search.
 * @param c Byte value to search for (converted to unsigned char).
 * @param n Number of bytes to search.
 *
 * @return Pointer to the last matching byte, or NULL if not found.
 *
 * @note GNU extension, not standard C or POSIX.
 *
 * @see memchr, strrchr
 */
void *memrchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--)
    {
        --p;
        if (*p == (unsigned char)c)
        {
            return (void *)p;
        }
    }
    return (void *)0;
}

/**
 * @brief Locate a byte sequence within a memory area.
 *
 * @details Finds the first occurrence of the byte sequence @p needle of length
 * @p needlelen in the memory area @p haystack of length @p haystacklen. Unlike
 * strstr(), this function works on raw memory and uses explicit lengths rather
 * than relying on null terminators.
 *
 * Useful for binary data or strings with embedded nulls.
 *
 * @param haystack    Memory area to search in.
 * @param haystacklen Length of haystack in bytes.
 * @param needle      Byte sequence to search for.
 * @param needlelen   Length of needle in bytes.
 *
 * @return Pointer to the start of the first occurrence of needle, or NULL if
 *         not found. If needlelen is 0, returns haystack.
 *
 * @note GNU extension.
 * @note Uses simple O(n*m) algorithm.
 *
 * @see strstr, memchr
 */
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen)
{
    if (needlelen == 0)
        return (void *)haystack;
    if (haystacklen < needlelen)
        return (void *)0;

    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    const unsigned char *end = h + haystacklen - needlelen + 1;

    while (h < end)
    {
        if (*h == *n && memcmp(h, n, needlelen) == 0)
        {
            return (void *)h;
        }
        h++;
    }
    return (void *)0;
}

/**
 * @brief Reverse a string in place.
 *
 * @details Reverses the characters of string @p str in place. The string is
 * modified directly; no new memory is allocated.
 *
 * @param str String to reverse. Must be modifiable (not a string literal).
 *
 * @return Pointer to the reversed string (same as @p str), or @p str unchanged
 *         if NULL or empty.
 *
 * @note Non-standard function. Originally from Microsoft C library.
 * @note Does not handle multibyte characters correctly; reverses bytes, not
 *       characters.
 *
 * @see strlen
 */
char *strrev(char *str)
{
    if (!str || !*str)
        return str;

    char *start = str;
    char *end = str + strlen(str) - 1;

    while (start < end)
    {
        char tmp = *start;
        *start = *end;
        *end = tmp;
        start++;
        end--;
    }

    return str;
}
