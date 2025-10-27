//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Collects the BASIC runtime's string manipulation primitives.  The helpers in
// this translation unit implement reference counting, substring extraction,
// trimming, case conversion, and search operations that must remain byte-for-byte
// compatible with the VM to guarantee deterministic program behaviour.
// Centralising the logic keeps ownership rules and edge-case handling
// consistent across native and interpreted runtimes.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime string manipulation utilities for BASIC.
/// @details Defines constructors, retain/release helpers, concatenation, slicing,
///          and query operations that operate on @ref rt_string handles.  Each
///          function mirrors the VM implementation's behaviour, including error
///          messages and ownership semantics, so BASIC programs behave
///          identically regardless of execution backend.

#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t kImmortalRefcnt = SIZE_MAX - 1;

/// @brief Retrieve the heap header associated with a runtime string.
/// @details Returns @c NULL for literal-backed strings whose storage is not
///          managed by the heap allocator.  The helper also asserts that heap
///          strings carry the expected @c RT_HEAP_STRING tag so callers can rely
///          on the metadata layout.
/// @param s Runtime string handle; may be @c NULL.
/// @return Heap header describing the string storage or @c NULL when not
///         heap-backed.
static rt_heap_hdr_t *rt_string_header(rt_string s)
{
    if (!s || !s->heap)
        return NULL;
    assert(s->heap->kind == RT_HEAP_STRING);
    return s->heap;
}

/// @brief Compute the payload length of a runtime string in bytes.
/// @details Handles literal strings by returning their stored literal length and
///          defers to @ref rt_heap_len for heap-backed values.  @c NULL strings
///          report length zero to simplify optional handling.
/// @param s Runtime string handle; may be @c NULL.
/// @return Number of bytes stored in @p s.
static size_t rt_string_len_bytes(rt_string s)
{
    if (!s)
        return 0;
    if (!s->heap)
        return s->literal_len;
    (void)rt_string_header(s);
    return rt_heap_len(s->data);
}

/// @brief Determine whether a heap header represents an immortal literal.
/// @details Immortal strings never release their payload, which allows the
///          runtime to expose shared empty-string singletons without reference
///          counting churn.
/// @param hdr Heap header describing a string allocation.
/// @return Non-zero when the header should not decrement reference counts.
static int rt_string_is_immortal_hdr(const rt_heap_hdr_t *hdr)
{
    return hdr && hdr->refcnt >= kImmortalRefcnt;
}

/// @brief Wrap a heap-allocated payload in an @ref rt_string handle.
/// @details Allocates the handle structure, associates it with the heap header,
///          and initialises literal metadata fields to zero.  The helper assumes
///          @p payload came from @ref rt_heap_alloc with the string kind tag.
/// @param payload Heap-managed UTF-8 buffer.
/// @return Newly allocated runtime string handle.
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

/// @brief Allocate a heap-backed string with the requested length and capacity.
/// @details Ensures the buffer always includes a trailing null terminator, clamps
///          capacity to at least @p len + 1, and raises traps on overflow or
///          allocation failure so callers never observe partially constructed
///          handles.
/// @param len Number of meaningful bytes the string should contain.
/// @param cap Desired capacity in bytes (excluding the terminator).
/// @return Newly allocated runtime string handle.
static rt_string rt_string_alloc(size_t len, size_t cap)
{
    if (len >= SIZE_MAX)
    {
        rt_trap("rt_string_alloc: length overflow");
        return NULL;
    }
    size_t required = len + 1;
    if (cap < required)
        cap = required;
    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, cap);
    if (!payload)
    {
        rt_trap("out of memory");
        return NULL;
    }
    payload[len] = '\0';
    return rt_string_wrap(payload);
}

/// @brief Return the immortal empty string singleton.
/// @details Lazily allocates a one-byte heap payload with an immortal reference
///          count so the runtime can hand out cheap references to the empty
///          string without repeated allocations.
/// @return Runtime handle referencing an empty string.
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

/// @brief Create a runtime string from a byte span.
/// @details Allocates a heap-backed string, copies @p len bytes from @p bytes,
///          and appends a trailing @c '\0'.  Null input pointers are allowed when
///          @p len is zero.
/// @param bytes Pointer to raw bytes to copy.
/// @param len Number of bytes to duplicate.
/// @return Newly allocated runtime string handle.
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

/// @brief Increment the reference count for a runtime string when needed.
/// @details Heap-backed strings increase their shared reference count, while
///          literal-backed strings maintain a separate @c literal_refs counter.
///          Immortal strings bypass reference tracking entirely.
/// @param s Runtime string to retain; may be @c NULL.
/// @return The same handle for convenience.
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

/// @brief Decrement the reference count for a runtime string when appropriate.
/// @details Releases heap-backed strings through @ref rt_heap_release and frees
///          literal handles when their @c literal_refs counter reaches zero.
///          Immortal strings are never released.
/// @param s Runtime string to release; may be @c NULL.
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

/// @brief Release a runtime string handle when it may be heap-backed.
/// @details Thin wrapper around @ref rt_string_unref used by generated code that
///          works with optional references.
/// @param s Runtime string to release; may be @c NULL.
void rt_str_release_maybe(rt_string s)
{
    rt_string_unref(s);
}

/// @brief Retain a runtime string handle when it may be heap-backed.
/// @details Forwards to @ref rt_string_ref and intentionally ignores the return
///          value because generated code commonly retains temporaries solely for
///          balancing reference counts.
/// @param s Runtime string to retain; may be @c NULL.
void rt_str_retain_maybe(rt_string s)
{
    (void)rt_string_ref(s);
}

/// @brief Retrieve the shared empty string singleton.
/// @details Equivalent to calling @ref rt_empty_string directly but exposed via
///          the public ABI.
/// @return Runtime handle referencing an empty string.
rt_string rt_str_empty(void)
{
    return rt_empty_string();
}

/// @brief Report the length of a runtime string.
/// @details Wraps @ref rt_string_len_bytes and casts to @c int64_t to match BASIC
///          numeric conventions.
/// @param s Runtime string handle; may be @c NULL.
/// @return Length in bytes.
int64_t rt_len(rt_string s)
{
    return (int64_t)rt_string_len_bytes(s);
}

/// @brief Concatenate two runtime strings, releasing the inputs.
/// @details Computes the combined length, allocates a result string, copies both
///          payloads, and releases the input handles if non-null.  Length
///          overflows trigger traps to mirror VM behaviour.
/// @param a Left-hand operand; may be @c NULL.
/// @param b Right-hand operand; may be @c NULL.
/// @return Newly allocated runtime string containing the concatenated bytes.
rt_string rt_concat(rt_string a, rt_string b)
{
    size_t len_a = rt_string_len_bytes(a);
    size_t len_b = rt_string_len_bytes(b);
    if (len_a > SIZE_MAX - len_b)
    {
        rt_trap("rt_concat: length overflow");
        return NULL;
    }
    size_t total = len_a + len_b;
    if (total == SIZE_MAX)
    {
        rt_trap("rt_concat: length overflow");
        return NULL;
    }

    rt_string out = rt_string_alloc(total, total + 1);
    if (!out)
        return NULL;

    if (a && a->data && len_a > 0)
        memcpy(out->data, a->data, len_a);
    if (b && b->data && len_b > 0)
        memcpy(out->data + len_a, b->data, len_b);

    out->data[total] = '\0';

    if (a)
        rt_string_unref(a);
    if (b)
        rt_string_unref(b);

    return out;
}

/// @brief Extract a substring given zero-based start and length.
/// @details Clamps @p start to the string length, ensures @p len is not longer
///          than the available suffix, and reuses the original handle when the
///          entire string is requested.  Empty results reuse the global empty
///          string to avoid allocations.
/// @param s Source string; must be non-null.
/// @param start Zero-based starting index; negative values clamp to zero.
/// @param len Requested number of bytes; negative values clamp to zero.
/// @return Runtime string representing the requested substring.
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
        return rt_string_ref(s);
    return rt_string_from_bytes(s->data + start_idx, copy_len);
}

/// @brief Return the leftmost @p n characters of a string.
/// @details Validates that @p n is non-negative, returns the entire string when
///          @p n exceeds the length, and otherwise delegates to @ref rt_substr.
/// @param s Source string; must be non-null.
/// @param n Number of bytes to keep from the left.
/// @return Runtime string containing the requested prefix.
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
        return rt_string_ref(s);
    return rt_substr(s, 0, n);
}

/// @brief Return the rightmost @p n characters of a string.
/// @details Mirrors @ref rt_left but slices from the end of the string.
/// @param s Source string; must be non-null.
/// @param n Number of bytes to keep from the right.
/// @return Runtime string containing the requested suffix.
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
        return rt_string_ref(s);
    size_t start = len - (size_t)n;
    return rt_substr(s, (int64_t)start, n);
}

/// @brief Implement the two-argument @c MID$ builtin.
/// @details Treats @p start as one-based, clamps it to the string length, and
///          returns a reference to the original string when the slice spans the
///          entire payload.
/// @param s Source string; must be non-null.
/// @param start One-based starting position.
/// @return Runtime string containing the requested suffix.
rt_string rt_mid2(rt_string s, int64_t start)
{
    if (!s)
        rt_trap("MID$: null string");
    if (start < 1)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 1 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t len = rt_string_len_bytes(s);
    if (start == 1)
        return rt_string_ref(s);
    uint64_t start_idx_u = (uint64_t)(start - 1);
    if (start_idx_u >= len)
        return rt_empty_string();
    size_t start_idx = (size_t)start_idx_u;
    size_t n = len - start_idx;
    return rt_substr(s, (int64_t)start_idx, (int64_t)n);
}

/// @brief Implement the three-argument @c MID$ builtin.
/// @details Enforces the one-based @p start contract, clamps @p len to the
///          available suffix, and reuses the original string when the full range
///          is requested.
/// @param s Source string; must be non-null.
/// @param start One-based starting position.
/// @param len Number of bytes to copy.
/// @return Runtime string containing the requested slice.
rt_string rt_mid3(rt_string s, int64_t start, int64_t len)
{
    if (!s)
        rt_trap("MID$: null string");
    if (start < 1)
    {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 1 (got %s)", numbuf);
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
    if (len == 0)
        return rt_empty_string();
    uint64_t start_idx_u = (uint64_t)(start - 1);
    if (start_idx_u >= slen)
        return rt_empty_string();
    size_t start_idx = (size_t)start_idx_u;
    if (start_idx == 0 && (size_t)len >= slen)
        return rt_string_ref(s);
    size_t avail = slen - start_idx;
    if ((uint64_t)len > avail)
        len = (int64_t)avail;
    return rt_substr(s, (int64_t)start_idx, len);
}

/// @brief Find a substring starting from a zero-based offset.
/// @details Performs a simple forward scan while respecting BASIC's 1-based
///          return convention.  Used by @ref rt_instr2 and @ref rt_instr3 to
///          implement @c INSTR.
/// @param hay   Haystack string; may be @c NULL.
/// @param start Zero-based starting offset.
/// @param needle Needle string; may be @c NULL.
/// @return One-based index of the first match, or zero when not found.
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

/// @brief Implement the two-argument form of BASIC's @c INSTR.
/// @details Returns one when searching for the empty string to match historical
///          behaviour; otherwise defers to @ref rt_find.
/// @param hay Haystack string; may be @c NULL.
/// @param needle Needle string; may be @c NULL.
/// @return One-based index of the first match, or zero when not found.
int64_t rt_instr2(rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    size_t needle_len = rt_string_len_bytes(needle);
    if (needle_len == 0)
        return 1;
    return rt_find(hay, 0, needle);
}

/// @brief Implement the three-argument form of BASIC's @c INSTR.
/// @details Treats @p start as one-based, clamps it to the haystack length, and
///          uses @ref rt_find to locate the needle.  Empty needles return the
///          adjusted start offset plus one, matching legacy semantics.
/// @param start One-based starting offset.
/// @param hay   Haystack string; may be @c NULL.
/// @param needle Needle string; may be @c NULL.
/// @return One-based index of the first match, or zero when not found.
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

/// @brief Trim leading spaces and tabs from a string.
/// @details Returns a slice into the original payload when possible by delegating
///          to @ref rt_substr.
/// @param s Source string; must be non-null.
/// @return Runtime string without leading whitespace.
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

/// @brief Trim trailing spaces and tabs from a string.
/// @details Moves a pointer backward until a non-whitespace character is found
///          and slices using @ref rt_substr.
/// @param s Source string; must be non-null.
/// @return Runtime string without trailing whitespace.
rt_string rt_rtrim(rt_string s)
{
    if (!s)
        rt_trap("rt_rtrim: null");
    size_t end = rt_string_len_bytes(s);
    while (end > 0 && (s->data[end - 1] == ' ' || s->data[end - 1] == '\t'))
        --end;
    return rt_substr(s, 0, (int64_t)end);
}

/// @brief Trim both leading and trailing spaces and tabs from a string.
/// @details Computes the new range and slices via @ref rt_substr, ensuring that
///          empty results reuse the shared empty string.
/// @param s Source string; must be non-null.
/// @return Runtime string without surrounding whitespace.
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

/// @brief Convert a string to uppercase using ASCII rules.
/// @details Allocates a new heap-backed string, performs byte-wise conversion,
///          and appends a trailing null terminator.  Non-ASCII bytes are left
///          untouched to match the VM's behaviour.
/// @param s Source string; must be non-null.
/// @return Newly allocated uppercase string.
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

/// @brief Convert a string to lowercase using ASCII rules.
/// @details Mirrors @ref rt_ucase but maps uppercase ASCII bytes to lowercase.
/// @param s Source string; must be non-null.
/// @return Newly allocated lowercase string.
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

/// @brief Compare two runtime strings for byte-wise equality.
/// @details Performs pointer equality and length checks before falling back to
///          @ref memcmp.  Null handles are treated as unequal to any non-null
///          string.
/// @param a First operand; may be @c NULL.
/// @param b Second operand; may be @c NULL.
/// @return @c 1 when the strings are equal; otherwise @c 0.
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
