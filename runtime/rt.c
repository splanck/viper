// File: runtime/rt.c
// Purpose: Implements BASIC runtime helpers for strings and I/O.
// Key invariants: Strings use reference counts; print functions do not append newlines.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/class-catalog.md

#include "rt.hpp"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Purpose: Provide a canonical empty runtime string to callers.
 *
 * Parameters: none.
 *
 * Returns: Pointer to a shared empty string instance. The refcount is set to
 * INT64_MAX so the singleton is never freed.
 *
 * Side effects: Initializes a static string on first invocation but performs
 * no dynamic allocation thereafter.
 */
static rt_string rt_empty_string(void)
{
    // Singleton empty string with effectively infinite refcount to prevent
    // deallocation.
    static struct rt_string_impl empty = {INT64_MAX, 0, 0, ""};
    return &empty;
}

/**
 * Purpose: Terminate the program immediately due to a fatal runtime error.
 *
 * Parameters:
 *   msg - Optional message describing the reason for the abort.
 *
 * Returns: Never returns.
 *
 * Side effects: Writes an error message to stderr and exits with status 1.
 */
void rt_abort(const char *msg)
{
    if (msg)
        fprintf(stderr, "runtime trap: %s\n", msg);
    else
        fprintf(stderr, "runtime trap\n");
    exit(1);
}

/**
 * Purpose: Trap handler used by the VM layer. Can be overridden by hosts.
 *
 * Parameters:
 *   msg - Optional message describing the trap condition.
 *
 * Returns: Never returns.
 *
 * Side effects: Delegates to rt_abort by default. Marked weak so embedders may
 * supply their own implementation.
 */
__attribute__((weak)) void vm_trap(const char *msg)
{
    rt_abort(msg);
}

/**
 * Purpose: Entry point for raising runtime traps from helper routines.
 *
 * Parameters:
 *   msg - Message describing the trap condition.
 *
 * Returns: Never returns.
 *
 * Side effects: Forwards the message to the VM trap handler.
 */
void rt_trap(const char *msg)
{
    vm_trap(msg);
}

/**
 * Purpose: Allocate a block of memory for runtime usage.
 *
 * Parameters:
 *   bytes - Number of bytes to allocate. Must be non-negative.
 *
 * Returns: Pointer to the allocated memory on success; does not return on
 * allocation failure or negative size.
 *
 * Side effects: May terminate the program via rt_trap on invalid inputs or
 * allocation failures.
 */
void *rt_alloc(int64_t bytes)
{
    if (bytes < 0)
        return rt_trap("negative allocation"), NULL;
    if ((uint64_t)bytes > SIZE_MAX)
    {
        rt_trap("allocation too large");
        return NULL;
    }
    void *p = malloc((size_t)bytes);
    if (!p)
        rt_trap("out of memory");
    return p;
}

/**
 * Purpose: Wrap a constant C string in the runtime string structure without
 * copying its contents.
 *
 * Parameters:
 *   c - NUL-terminated C string to wrap. Must outlive the returned object.
 *
 * Returns: Runtime string referencing the existing character data, or NULL if
 * c is NULL.
 *
 * Side effects: Allocates a runtime string header but not the underlying data.
 */
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

/**
 * Purpose: Write a runtime string to standard output without a trailing
 * newline.
 *
 * Parameters:
 *   s - String to print; NULL strings are ignored.
 *
 * Returns: void.
 *
 * Side effects: Writes to stdout.
 */
void rt_print_str(rt_string s)
{
    if (s && s->data)
        fwrite(s->data, 1, (size_t)s->size, stdout);
}

/**
 * Purpose: Print a 64-bit integer in decimal form to stdout.
 *
 * Parameters:
 *   v - Value to print.
 *
 * Returns: void.
 *
 * Side effects: Writes to stdout.
 */
void rt_print_i64(int64_t v)
{
    printf("%lld", (long long)v);
}

/**
 * Purpose: Print a double-precision floating-point number to stdout.
 *
 * Parameters:
 *   v - Value to print.
 *
 * Returns: void.
 *
 * Side effects: Writes to stdout.
 */
void rt_print_f64(double v)
{
    printf("%g", v);
}

/**
 * Purpose: Read a single line of input from stdin into a runtime string.
 *
 * Parameters: none.
 *
 * Returns: Newly allocated runtime string containing the line without the
 * trailing newline. Returns NULL on EOF before any characters are read.
 *
 * Side effects: Reads from stdin and performs heap allocations. May terminate
 * via rt_trap on allocation failures.
 */
rt_string rt_input_line(void)
{
    size_t cap = 1024;
    size_t len = 0;
    char *buf = (char *)rt_alloc(cap);
    for (;;)
    {
        int ch = fgetc(stdin);
        if (ch == EOF)
        {
            if (len == 0)
            {
                free(buf);
                return NULL;
            }
            break;
        }
        if (ch == '\n')
            break;
        if (len + 1 >= cap)
        {
            size_t new_cap = cap * 2;
            char *nbuf = (char *)realloc(buf, new_cap);
            if (!nbuf)
            {
                free(buf);
                rt_trap("out of memory");
                return NULL;
            }
            buf = nbuf;
            cap = new_cap;
        }
        buf[len++] = (char)ch;
    }
    buf[len] = '\0';
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->refcnt = 1;
    s->size = (int64_t)len;
    s->capacity = s->size;
    char *data = (char *)rt_alloc(len + 1);
    memcpy(data, buf, len + 1);
    s->data = data;
    free(buf);
    return s;
}

/**
 * Purpose: Return the length of a runtime string.
 *
 * Parameters:
 *   s - Runtime string; may be NULL.
 *
 * Returns: Number of characters in the string, or 0 if s is NULL.
 *
 * Side effects: None.
 */
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
    if (start + len > s->size)
        len = s->size - start;
    if (len == 0)
        return rt_empty_string();
    if (start == 0 && len == s->size)
    {
        s->refcnt++;
        return s;
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
        s->refcnt++;
        return s;
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
        s->refcnt++;
        return s;
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
        s->refcnt++;
        return s;
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
        s->refcnt++;
        return s;
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
    start -= 1;
    if (start < 0)
        start = 0;
    if (start > len)
        start = len;
    if (needle->size == 0)
        return start + 1;
    return rt_find(hay, start, needle);
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
