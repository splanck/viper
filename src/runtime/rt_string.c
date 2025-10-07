// File: src/runtime/rt_string.c
// Purpose: Implements string manipulation utilities for the BASIC runtime.
// Key invariants: Strings use reference counts; operations trap on invalid inputs.
// Ownership/Lifetime: Caller manages returned strings and reference counts.
// Links: docs/codemap.md

#include "rt_string.h"
#include "rt_internal.h"
#include "rt_numeric.h"
#include "rt_format.h"
#include "rt_int_format.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t kImmortalRefcnt = SIZE_MAX - 1;

static rt_heap_hdr_t *rt_string_header(rt_string s)
{
    if (!s || !s->heap)
        return NULL;
    assert(s->heap->kind == RT_HEAP_STRING);
    return s->heap;
}

static size_t rt_string_len_bytes(rt_string s)
{
    if (!s)
        return 0;
    if (!s->heap)
        return s->literal_len;
    (void)rt_string_header(s);
    return rt_heap_len(s->data);
}

static int rt_string_is_immortal_hdr(const rt_heap_hdr_t *hdr)
{
    return hdr && hdr->refcnt >= kImmortalRefcnt;
}

static rt_string rt_string_wrap(char *payload)
{
    if (!payload)
        return NULL;
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->data = payload;
    s->heap = hdr;
    s->literal_len = 0;
    s->literal_refs = 0;
    return s;
}

static rt_string rt_string_alloc(size_t len, size_t cap)
{
    if (cap < len + 1)
        cap = len + 1;
    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, cap);
    if (!payload)
    {
        rt_trap("out of memory");
        return NULL;
    }
    payload[len] = '\0';
    return rt_string_wrap(payload);
}

/**
 * Purpose: Allocate a runtime string from arbitrary bytes and explicit length.
 *
 * Parameters:
 *   bytes - Pointer to the UTF-8 byte sequence to copy; may be NULL when
 *           @p len is zero.
 *   len   - Number of bytes to copy from @p bytes.
 *
 * Returns: Newly allocated runtime string containing the copied bytes.
 */
rt_string rt_string_from_bytes(const char *bytes, size_t len)
{
    rt_string s = rt_string_alloc(len, len + 1);
    if (!s)
        return NULL;
    if (len > 0 && bytes)
        memcpy(s->data, bytes, len);
    s->data[len] = '\0';
    return s;
}

static rt_string rt_empty_string(void)
{
    static rt_string empty = NULL;
    if (!empty)
    {
        char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, 0, 1);
        if (!payload)
            rt_trap("rt_empty_string: alloc");
        payload[0] = '\0';
        rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
        assert(hdr);
        assert(hdr->kind == RT_HEAP_STRING);
        hdr->refcnt = kImmortalRefcnt;
        empty = (rt_string)rt_alloc(sizeof(*empty));
        empty->data = payload;
        empty->heap = hdr;
        empty->literal_len = 0;
        empty->literal_refs = 0;
    }
    return empty;
}

/**
 * Purpose: Increment reference count of runtime string.
 *
 * Parameters:
 *   s - String to reference; may be NULL.
 *
 * Returns: Input string pointer @p s.
 *
 * Side effects: Increments heap reference count unless @p s is NULL or the
 * shared empty string.
 */
rt_string rt_string_ref(rt_string s)
{
    if (!s)
        return NULL;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr)
    {
        if (s->literal_refs < SIZE_MAX)
            s->literal_refs++;
        return s;
    }
    if (rt_string_is_immortal_hdr(hdr))
        return s;
    rt_heap_retain(s->data);
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
    if (!s)
        return;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr)
    {
        if (s->literal_refs > 0 && --s->literal_refs == 0)
            free(s);
        return;
    }
    if (rt_string_is_immortal_hdr(hdr))
        return;
    size_t next = rt_heap_release(s->data);
    if (next == 0)
        free(s);
}

void rt_str_release_maybe(rt_string s)
{
    rt_string_unref(s);
}

void rt_str_retain_maybe(rt_string s)
{
    (void)rt_string_ref(s);
}

rt_string rt_str_empty(void)
{
    return rt_empty_string();
}

rt_string rt_const_cstr(const char *c)
{
    if (!c)
        return NULL;
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    s->data = (char *)c;
    s->heap = NULL;
    s->literal_len = strlen(c);
    s->literal_refs = 1;
    return s;
}

int64_t rt_len(rt_string s)
{
    return (int64_t)rt_string_len_bytes(s);
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
    size_t len_a = rt_string_len_bytes(a);
    size_t len_b = rt_string_len_bytes(b);
    size_t total = len_a + len_b;

    rt_string out = rt_string_alloc(total, total + 1);
    if (!out)
        return NULL;

    if (a && a->data && len_a > 0)
        memcpy(out->data, a->data, len_a);
    if (b && b->data && len_b > 0)
        memcpy(out->data + len_a, b->data, len_b);

    out->data[total] = '\0';
    return out;
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
    size_t slen = rt_string_len_bytes(s);
    if ((uint64_t)start > slen)
        start = (int64_t)slen;
    size_t start_idx = (size_t)start;
    size_t avail = slen - start_idx;
    size_t copy_len = (size_t)len;
    if (copy_len > avail)
        copy_len = avail;
    if (copy_len == 0)
        return rt_empty_string();
    if (start_idx == 0 && copy_len == slen)
    {
        return rt_string_ref(s);
    }
    return rt_string_from_bytes(s->data + start_idx, copy_len);
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
        char numbuf[32];
        rt_i64_to_cstr(n, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "LEFT$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t slen = rt_string_len_bytes(s);
    if (n == 0)
        return rt_empty_string();
    if ((size_t)n >= slen)
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
        char numbuf[32];
        rt_i64_to_cstr(n, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "RIGHT$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t len = rt_string_len_bytes(s);
    if (n == 0)
        return rt_empty_string();
    if ((size_t)n >= len)
    {
        return rt_string_ref(s);
    }
    size_t start = len - (size_t)n;
    // O(n) copy via rt_substr.
    return rt_substr(s, (int64_t)start, n);
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
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t len = rt_string_len_bytes(s);
    if (start <= 0)
    {
        return rt_string_ref(s);
    }
    if ((size_t)start >= len)
        return rt_empty_string();
    size_t n = len - (size_t)start;
    // O(n) copy via rt_substr.
    return rt_substr(s, start, (int64_t)n);
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
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    if (len < 0)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(len, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t slen = rt_string_len_bytes(s);
    if (len == 0 || (size_t)start >= slen)
        return rt_empty_string();
    if (start == 0 && (size_t)len >= slen)
    {
        return rt_string_ref(s);
    }
    if ((size_t)len > slen - (size_t)start)
        len = (int64_t)(slen - (size_t)start);
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
    size_t hay_len = rt_string_len_bytes(hay);
    size_t needle_len = rt_string_len_bytes(needle);
    if ((uint64_t)start > hay_len)
        start = (int64_t)hay_len;
    size_t start_idx = (size_t)start;
    if (needle_len > hay_len - start_idx)
        return 0;
    for (size_t i = start_idx; i + needle_len <= hay_len; ++i)
        if (memcmp(hay->data + i, needle->data, needle_len) == 0)
            return (int64_t)(i + 1);
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
    size_t needle_len = rt_string_len_bytes(needle);
    if (needle_len == 0)
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
    size_t len = rt_string_len_bytes(hay);
    int64_t pos = start <= 1 ? 0 : start - 1;
    if ((uint64_t)pos > len)
        pos = (int64_t)len;
    size_t needle_len = rt_string_len_bytes(needle);
    if (needle_len == 0)
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
    size_t slen = rt_string_len_bytes(s);
    size_t i = 0;
    while (i < slen && (s->data[i] == ' ' || s->data[i] == '\t'))
        ++i;
    return rt_substr(s, (int64_t)i, (int64_t)(slen - i));
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
    size_t end = rt_string_len_bytes(s);
    while (end > 0 && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, 0, (int64_t)end);
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
    size_t slen = rt_string_len_bytes(s);
    size_t start = 0;
    size_t end = slen;
    while (start < end && (s->data[start] == ' ' || s->data[start] == '\t'))
        ++start;
    while (end > start && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, (int64_t)start, (int64_t)(end - start));
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
    size_t len = rt_string_len_bytes(s);
    rt_string r = rt_string_alloc(len, len + 1);
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        r->data[i] = (char)c;
    }
    r->data[len] = '\0';
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
    size_t len = rt_string_len_bytes(s);
    rt_string r = rt_string_alloc(len, len + 1);
    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)s->data[i];
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        r->data[i] = (char)c;
    }
    r->data[len] = '\0';
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
        char numbuf[32];
        rt_i64_to_cstr(code, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "CHR$: code must be 0-255 (got %s)", numbuf);
        rt_trap(buf);
    }
    char ch = (char)(unsigned char)code;
    return rt_string_from_bytes(&ch, 1);
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
    size_t len = rt_string_len_bytes(s);
    if (len == 0 || !s->data)
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
    if (a == b)
        return 1;
    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    if (alen != blen)
        return 0;
    return memcmp(a->data, b->data, alen) == 0;
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
    size_t len = rt_string_len_bytes(s);
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
    char stack_buf[32];
    char *buf = stack_buf;
    size_t cap = sizeof(stack_buf);
    char *heap_buf = NULL;

    size_t written = rt_i64_to_cstr(v, buf, cap);
    if (written == 0 && buf[0] == '\0')
        rt_trap("rt_int_to_str: format");

    while (written + 1 >= cap)
    {
        if (cap > SIZE_MAX / 2)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: overflow");
        }
        size_t new_cap = cap * 2;
        char *new_buf = (char *)malloc(new_cap);
        if (!new_buf)
        {
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: alloc");
        }
        size_t new_written = rt_i64_to_cstr(v, new_buf, new_cap);
        if (new_written == 0 && new_buf[0] == '\0')
        {
            free(new_buf);
            if (heap_buf)
                free(heap_buf);
            rt_trap("rt_int_to_str: format");
        }
        if (heap_buf)
            free(heap_buf);
        heap_buf = new_buf;
        buf = new_buf;
        cap = new_cap;
        written = new_written;
    }

    rt_string s = rt_string_from_bytes(buf, written);
    if (heap_buf)
        free(heap_buf);
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
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/**
 * Purpose: Allocate a runtime string for a double value using canonical formatting.
 *
 * Parameters:
 *   v - Double-precision floating-point value to format.
 *
 * Returns: Newly allocated string containing the decimal representation of @p v.
 *
 * Side effects: Allocates heap storage for the resulting string; traps on failure.
 */
rt_string rt_str_d_alloc(double v)
{
    char buf[64];
    rt_format_f64(v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/**
 * Purpose: Allocate a runtime string for a float value using canonical formatting.
 *
 * Parameters:
 *   v - Single-precision floating-point value to format.
 *
 * Returns: Newly allocated string containing the decimal representation of @p v.
 *
 * Side effects: Allocates heap storage for the resulting string; traps on failure.
 */
rt_string rt_str_f_alloc(float v)
{
    char buf[64];
    rt_format_f64((double)v, buf, sizeof(buf));
    return rt_string_from_bytes(buf, strlen(buf));
}

/**
 * Purpose: Allocate a runtime string for a 32-bit integer value using canonical formatting.
 *
 * Parameters:
 *   v - 32-bit signed integer to format.
 *
 * Returns: Newly allocated string containing the decimal representation of @p v.
 *
 * Side effects: Allocates heap storage for the resulting string; traps on failure.
 */
rt_string rt_str_i32_alloc(int32_t v)
{
    char buf[32];
    rt_str_from_i32(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
}

/**
 * Purpose: Allocate a runtime string for a 16-bit integer value using canonical formatting.
 *
 * Parameters:
 *   v - 16-bit signed integer to format.
 *
 * Returns: Newly allocated string containing the decimal representation of @p v.
 *
 * Side effects: Allocates heap storage for the resulting string; traps on failure.
 */
rt_string rt_str_i16_alloc(int16_t v)
{
    char buf[16];
    rt_str_from_i16(v, buf, sizeof(buf), NULL);
    return rt_string_from_bytes(buf, strlen(buf));
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
    bool ok = true;
    double value = rt_val_to_double(s->data, &ok);
    if (!ok)
    {
        if (!isfinite(value))
            rt_trap("rt_val: overflow");
        return value;
    }
    return value;
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

const char *rt_string_cstr(rt_string s)
{
    if (!s)
    {
        rt_trap("rt_string_cstr: null string");
        return "";
    }
    if (!s->data)
    {
        rt_trap("rt_string_cstr: null data");
        return "";
    }
    return s->data;
}
