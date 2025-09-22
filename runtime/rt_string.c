// File: runtime/rt_string.c
// Purpose: Implements string manipulation utilities for the BASIC runtime.
// Key invariants: Strings use reference counts; operations trap on invalid inputs.
// Ownership/Lifetime: Caller manages returned strings and reference counts.
// Links: docs/class-catalog.md

#include "rt_internal.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rt_string rt_empty_string(void)
{
    static struct rt_string_impl empty = {INT64_MAX, 0, 0, ""};
    return &empty;
}

/**
 * Purpose: Increment reference count of runtime string.
 *
 * Parameters:
 *   s - String to reference; may be NULL.
 *
 * Returns: Input string pointer @p s.
 *
 * Side effects: Increments @p s->refcnt unless @p s is NULL or the shared
 * empty string.
 */
rt_string rt_string_ref(rt_string s)
{
    if (s && s->refcnt != INT64_MAX)
        s->refcnt++;
    return s;
}

/**
 * Purpose: Decrement reference count and free when reaching zero.
 *
 * Parameters:
 *   s - String to release; may be NULL.
 *
 * Side effects: Frees string data and struct when the count drops to zero.
 */
void rt_string_unref(rt_string s)
{
    if (!s || s->refcnt == INT64_MAX)
        return;
    if (--s->refcnt == 0)
    {
        if (s->capacity > 0 && s->data)
            free((void *)s->data);
        free(s);
    }
}

rt_string rt_const_cstr(const char *c)
{
    if (!c)
        return NULL;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = (int64_t)strlen(c);
    s->capacity = 0;
    s->data = c;
    return s;
}

int64_t rt_len(rt_string s)
{
    return s ? s->size : 0;
}

/**
 * Purpose: Concatenate two runtime strings into a new string.
 *
 * Parameters:
 *   a - Left operand; may be NULL.
 *   b - Right operand; may be NULL.
 *
 * Returns: Newly allocated runtime string containing the concatenation of a
 * and b.
 *
 * Side effects: Allocates memory; may terminate via rt_trap on failure.
 */
rt_string rt_concat(rt_string a, rt_string b)
{
    int64_t asz = a ? a->size : 0;
    int64_t bsz = b ? b->size : 0;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = asz + bsz;
    s->capacity = s->size;
    char *buf = (char *)rt_alloc(s->size + 1);
    if (a && a->data)
        memcpy(buf, a->data, asz);
    if (b && b->data)
        memcpy(buf + asz, b->data, bsz);
    buf[s->size] = '\0';
    s->data = buf;

    int64_t a_refcnt = (a && a->refcnt != INT64_MAX) ? a->refcnt : INT64_MAX;
    if (a)
        rt_string_unref(a);
    if (b)
    {
        if (b == a && a_refcnt != INT64_MAX && a_refcnt <= 1)
        {
            // The first unref already released the sole reference; avoid double free.
        }
        else
        {
            rt_string_unref(b);
        }
    }
    return s;
}

/**
 * Purpose: Extract a substring from a runtime string.
 *
 * Parameters:
 *   s     - Source string; must not be NULL.
 *   start - Starting index (0-based); negative values clamp to 0.
 *   len   - Maximum number of characters to copy; negative values yield 0.
 *
 * Returns: Newly allocated substring, or a reference to s if start is 0 and
 * len equals the string's length.
 *
 * Side effects: May increment reference count of s or allocate new memory.
 */
rt_string rt_substr(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("rt_substr: null");
    if (start < 0)
        start = 0;
    if (len < 0)
        len = 0;
    if (start > s->size)
        start = s->size;
    int64_t avail = s->size - start;
    if (len > avail)
        len = avail;
    if (len == 0)
        return rt_empty_string();
    if (start == 0 && len == s->size)
    {
        return rt_string_ref(s);
    }
    // O(len) time, one allocation and copy of len bytes.
    rt_string r = (rt_string)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = len;
    r->capacity = len;
    char *data = (char *)rt_alloc(len + 1);
    memcpy(data, s->data + start, len);
    data[len] = '\0';
    r->data = data;
    return r;
}

/**
 * Purpose: Return the leftmost n characters of a string.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *   n - Number of characters to take; must be non-negative.
 *
 * Returns: Newly allocated string containing the prefix, or a reference to s
 * if n covers the entire string.
 *
 * Side effects: May allocate memory or increment reference count of s.
 */
rt_string rt_left(rt_string s, int64_t n)
{
    if (!s)
        rt_trap("LEFT$: null string");
    if (n < 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "LEFT$: len must be >= 0 (got %lld)", (long long)n);
        rt_trap(buf);
    }
    if (n == 0)
        return rt_empty_string();
    if (n >= s->size)
    {
        return rt_string_ref(s);
    }
    // O(n) copy via rt_substr.
    return rt_substr(s, 0, n);
}

/**
 * Purpose: Return the rightmost n characters of a string.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *   n - Number of characters to take; must be non-negative.
 *
 * Returns: Newly allocated string containing the suffix, or a reference to s
 * if n covers the entire string.
 *
 * Side effects: May allocate memory or increment reference count of s.
 */
rt_string rt_right(rt_string s, int64_t n)
{
    if (!s)
        rt_trap("RIGHT$: null string");
    if (n < 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "RIGHT$: len must be >= 0 (got %lld)", (long long)n);
        rt_trap(buf);
    }
    int64_t len = s->size;
    if (n == 0)
        return rt_empty_string();
    if (n >= len)
    {
        return rt_string_ref(s);
    }
    int64_t start = len - n;
    // O(n) copy via rt_substr.
    return rt_substr(s, start, n);
}

/**
 * Purpose: Return a substring starting at index start extending to the end.
 *
 * Parameters:
 *   s     - Source string; must not be NULL.
 *   start - Starting index; must be non-negative.
 *
 * Returns: Newly allocated substring or reference to s if start <= 0.
 *
 * Side effects: May allocate memory or increment reference count of s.
 */
rt_string rt_mid2(rt_string s, int64_t start)
{
    if (!s)
        rt_trap("MID$: null string");
    if (start < 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "MID$: start must be >= 0 (got %lld)", (long long)start);
        rt_trap(buf);
    }
    int64_t len = s->size;
    if (start <= 0)
    {
        return rt_string_ref(s);
    }
    if (start >= len)
        return rt_empty_string();
    int64_t n = len - start;
    // O(n) copy via rt_substr.
    return rt_substr(s, start, n);
}

/**
 * Purpose: Return a substring starting at index start with at most len
 * characters.
 *
 * Parameters:
 *   s     - Source string; must not be NULL.
 *   start - Starting index; must be non-negative.
 *   len   - Number of characters to extract; must be non-negative.
 *
 * Returns: Newly allocated substring, or a reference to s when the entire
 * string is selected.
 *
 * Side effects: May allocate memory or increment reference count of s.
 */
rt_string rt_mid3(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("MID$: null string");
    if (start < 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "MID$: start must be >= 0 (got %lld)", (long long)start);
        rt_trap(buf);
    }
    if (len < 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "MID$: len must be >= 0 (got %lld)", (long long)len);
        rt_trap(buf);
    }
    int64_t slen = s->size;
    if (len == 0 || start >= slen)
        return rt_empty_string();
    if (start == 0 && len >= slen)
    {
        return rt_string_ref(s);
    }
    if (len > slen - start)
        len = slen - start;
    // O(len) copy via rt_substr.
    return rt_substr(s, start, len);
}

/**
 * Purpose: Search for a substring within another string starting at a given
 * position.
 *
 * Parameters:
 *   hay    - Haystack string to search.
 *   start  - 0-based starting position for the search.
 *   needle - Needle string to locate.
 *
 * Returns: 1-based index of the first occurrence, or 0 if not found.
 *
 * Side effects: None.
 */
static int64_t rt_find(rt_string hay, int64_t start, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    if (start < 0)
        start = 0;
    if (start > hay->size)
        start = hay->size;
    for (int64_t i = start; i + needle->size <= hay->size; ++i)
        if (memcmp(hay->data + i, needle->data, (size_t)needle->size) == 0)
            return i + 1;
    return 0;
}

/**
 * Purpose: Find the position of one string within another.
 *
 * Parameters:
 *   hay    - Haystack string to search.
 *   needle - Needle string to locate.
 *
 * Returns: 1-based index of the first occurrence, or 0 if not found. An empty
 * needle returns 1.
 *
 * Side effects: None.
 */
int64_t rt_instr2(rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    if (needle->size == 0)
        return 1;
    return rt_find(hay, 0, needle);
}

/**
 * Purpose: Find the position of a substring starting from a specific 1-based
 * offset.
 *
 * Parameters:
 *   start  - 1-based starting index for the search.
 *   hay    - Haystack string to search.
 *   needle - Needle string to locate.
 *
 * Returns: 1-based index of the first occurrence at or after start, or 0 if
 * not found. An empty needle returns the clamped start + 1 (yielding a result
 * in [1, len + 1]).
 *
 * Side effects: None.
 */
int64_t rt_instr3(int64_t start, rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    int64_t len = hay->size;
    int64_t pos = start <= 1 ? 0 : start - 1;
    if (pos > len)
        pos = len;
    if (needle->size == 0)
        return pos + 1;
    return rt_find(hay, pos, needle);
}

/**
 * Purpose: Remove leading spaces and tabs from a string.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *
 * Returns: Newly allocated trimmed string.
 *
 * Side effects: Allocates memory via rt_substr.
 */
rt_string rt_ltrim(rt_string s)
{
    if (!s)
        rt_trap("rt_ltrim: null");
    int64_t i = 0;
    while (i < s->size && (s->data[i] == ' ' || s->data[i] == '\t'))
        ++i;
    return rt_substr(s, i, s->size - i);
}

/**
 * Purpose: Remove trailing spaces and tabs from a string.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *
 * Returns: Newly allocated trimmed string.
 *
 * Side effects: Allocates memory via rt_substr.
 */
rt_string rt_rtrim(rt_string s)
{
    if (!s)
        rt_trap("rt_rtrim: null");
    int64_t end = s->size;
    while (end > 0 && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, 0, end);
}

/**
 * Purpose: Remove leading and trailing spaces and tabs from a string.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *
 * Returns: Newly allocated trimmed string.
 *
 * Side effects: Allocates memory via rt_substr.
 */
rt_string rt_trim(rt_string s)
{
    if (!s)
        rt_trap("rt_trim: null");
    int64_t start = 0;
    int64_t end = s->size;
    while (start < end && (s->data[start] == ' ' || s->data[start] == '\t'))
        ++start;
    while (end > start && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, start, end - start);
}

/**
 * Purpose: Convert all alphabetic characters in a string to uppercase.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *
 * Returns: Newly allocated uppercase string.
 *
 * Side effects: Allocates memory for the new string.
 */
rt_string rt_ucase(rt_string s)
{
    if (!s)
        rt_trap("rt_ucase: null");
    rt_string r = (rt_string)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = s->size;
    r->capacity = r->size;
    char *data = (char *)rt_alloc(r->size + 1);
    for (int64_t i = 0; i < r->size; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        data[i] = (char)c;
    }
    data[r->size] = '\0';
    r->data = data;
    return r;
}

/**
 * Purpose: Convert all alphabetic characters in a string to lowercase.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *
 * Returns: Newly allocated lowercase string.
 *
 * Side effects: Allocates memory for the new string.
 */
rt_string rt_lcase(rt_string s)
{
    if (!s)
        rt_trap("rt_lcase: null");
    rt_string r = (rt_string)rt_alloc(sizeof(*r));
    r->refcnt = 1;
    r->size = s->size;
    r->capacity = r->size;
    char *data = (char *)rt_alloc(r->size + 1);
    for (int64_t i = 0; i < r->size; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        data[i] = (char)c;
    }
    data[r->size] = '\0';
    r->data = data;
    return r;
}

/**
 * Purpose: Create a one-character string from an ASCII code.
 *
 * Parameters:
 *   code - Integer in the range [0,255] representing the character.
 *
 * Returns: Newly allocated string of length one containing the character.
 *
 * Side effects: Allocates memory; traps on out-of-range codes.
 */
rt_string rt_chr(int64_t code)
{
    if (code < 0 || code > 255)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "CHR$: code must be 0-255 (got %lld)", (long long)code);
        rt_trap(buf);
    }
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = 1;
    s->capacity = 1;
    char *data = (char *)rt_alloc(2);
    data[0] = (char)(unsigned char)code;
    data[1] = '\0';
    s->data = data;
    return s;
}

/**
 * Purpose: Return the ASCII code of the first character of a string.
 *
 * Parameters:
 *   s - Source string; must not be NULL.
 *
 * Returns: Integer value of the first character, or 0 for empty strings.
 *
 * Side effects: None.
 */
int64_t rt_asc(rt_string s)
{
    if (!s)
        rt_trap("rt_asc: null");
    if (s->size <= 0 || !s->data)
        return 0;
    return (int64_t)(unsigned char)s->data[0];
}

/**
 * Purpose: Compare two strings for equality.
 *
 * Parameters:
 *   a - First string.
 *   b - Second string.
 *
 * Returns: Non-zero if equal, zero otherwise.
 *
 * Side effects: None.
 */
int64_t rt_str_eq(rt_string a, rt_string b)
{
    if (!a || !b)
        return 0;
    if (a->size != b->size)
        return 0;
    return memcmp(a->data, b->data, (size_t)a->size) == 0;
}

/**
 * Purpose: Parse a runtime string as a signed 64-bit integer.
 *
 * Parameters:
 *   s - String to parse; must not be NULL.
 *
 * Returns: Parsed integer value.
 *
 * Side effects: May call rt_trap on invalid or out-of-range values. Allocates
 * temporary buffer for parsing.
 */
int64_t rt_to_int(rt_string s)
{
    if (!s)
        rt_trap("rt_to_int: null");
    const char *p = s->data;
    size_t len = (size_t)s->size;
    size_t i = 0;
    while (i < len && isspace((unsigned char)p[i]))
        ++i;
    size_t j = len;
    while (j > i && isspace((unsigned char)p[j - 1]))
        --j;
    if (i == j)
        rt_trap("rt_to_int: empty");
    size_t sz = j - i;
    char *buf = (char *)rt_alloc(sz + 1);
    memcpy(buf, p + i, sz);
    buf[sz] = '\0';
    errno = 0;
    char *endp = NULL;
    long long v = strtoll(buf, &endp, 10);
    if (errno == ERANGE || !endp || *endp != '\0')
    {
        free(buf);
        rt_trap("rt_to_int: invalid");
    }
    free(buf);
    return (int64_t)v;
}

/**
 * Purpose: Convert a 64-bit integer to its decimal string representation.
 *
 * Parameters:
 *   v - Value to convert.
 *
 * Returns: Newly allocated runtime string representing v.
 *
 * Side effects: Allocates memory; traps on formatting failures.
 */
rt_string rt_int_to_str(int64_t v)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
    if (n < 0)
        rt_trap("rt_int_to_str: format");

    const char *src = buf;
    char *tmp = NULL;
    if (n >= (int)sizeof(buf))
    {
        tmp = (char *)malloc((size_t)n + 1);
        if (!tmp)
            rt_trap("rt_int_to_str: alloc");
        int n2 = snprintf(tmp, (size_t)n + 1, "%lld", (long long)v);
        if (n2 < 0 || n2 > n)
        {
            free(tmp);
            rt_trap("rt_int_to_str: format");
        }
        src = tmp;
    }

    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = n;
    s->capacity = n;
    char *data = (char *)rt_alloc((size_t)n + 1);
    memcpy(data, src, (size_t)n + 1);
    s->data = data;

    if (tmp)
        free(tmp);
    return s;
}

/**
 * Purpose: Convert a double-precision floating-point number to a string.
 *
 * Parameters:
 *   v - Value to convert.
 *
 * Returns: Newly allocated runtime string representing v.
 *
 * Side effects: Allocates memory; traps on formatting failures.
 */
rt_string rt_f64_to_str(double v)
{
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%g", v);
    if (n < 0)
        rt_trap("rt_f64_to_str: format");

    const char *src = buf;
    char *tmp = NULL;
    if (n >= (int)sizeof(buf))
    {
        tmp = (char *)malloc((size_t)n + 1);
        if (!tmp)
            rt_trap("rt_f64_to_str: alloc");
        int n2 = snprintf(tmp, (size_t)n + 1, "%g", v);
        if (n2 < 0 || n2 > n)
        {
            free(tmp);
            rt_trap("rt_f64_to_str: format");
        }
        src = tmp;
    }

    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = n;
    s->capacity = n;
    char *data = (char *)rt_alloc((size_t)n + 1);
    memcpy(data, src, (size_t)n + 1);
    s->data = data;

    if (tmp)
        free(tmp);
    return s;
}

/**
 * Purpose: Parse a string as a floating-point number.
 *
 * Parameters:
 *   s - String to parse; must not be NULL.
 *
 * Returns: Parsed double value, or 0.0 if no conversion could be performed.
 *
 * Side effects: May call rt_trap on NULL input.
 */
double rt_val(rt_string s)
{
    if (!s)
        rt_trap("rt_val: null");
    char *endp = NULL;
    double v = strtod(s->data, &endp);
    if (endp == s->data)
        return 0.0;
    return v;
}

/**
 * Purpose: Convert a floating-point value to its string representation.
 *
 * Parameters:
 *   v - Value to convert.
 *
 * Returns: Newly allocated runtime string representing v.
 *
 * Side effects: Allocates memory via rt_f64_to_str.
 */
rt_string rt_str(double v)
{
    return rt_f64_to_str(v);
}
