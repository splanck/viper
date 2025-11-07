//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_string_ops.c
// Purpose: Implement the BASIC runtime's intrinsic string operations and
//          supporting allocation/retention helpers.
// Key invariants: Runtime strings are reference-counted, literal handles are
//                 immutable and may become immortal, and every intrinsic mirrors
//                 the VM's slicing/trimming/case-conversion semantics exactly.
// Links: docs/runtime/strings.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Core string operations for the BASIC runtime.
/// @details Provides allocation helpers, reference management utilities, and
///          implementations of the intrinsic string-manipulation functions.  All
///          routines trap on invalid arguments to produce consistent diagnostics
///          across native and VM execution modes.

#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t kImmortalRefcnt = SIZE_MAX - 1;

/// @brief Retrieve the heap header associated with a runtime string.
/// @details Returns `NULL` for literal strings that are not backed by the shared
///          heap and asserts that heap-backed strings carry the expected kind.
///          Callers use this to peek at reference counts or capacities without
///          duplicating validation logic.
/// @param s Runtime string handle.
/// @return Heap header describing the allocation, or `NULL` for literals.
static rt_heap_hdr_t *rt_string_header(rt_string s)
{
    if (!s || !s->heap)
        return NULL;
    assert(s->heap->kind == RT_HEAP_STRING);
    return s->heap;
}

/// @brief Report the byte length of a runtime string payload.
/// @details Handles both literal strings (which store the length inline) and
///          heap-backed strings (which derive the length from the heap header).
///          Null handles yield zero, allowing callers to treat them as empty.
/// @param s Runtime string handle.
/// @return Number of bytes in the string, excluding the terminator.
static size_t rt_string_len_bytes(rt_string s)
{
    if (!s)
        return 0;
    if (!s->heap)
        return s->literal_len;
    (void)rt_string_header(s);
    return rt_heap_len(s->data);
}

/// @brief Determine whether a heap-backed string should never be freed.
/// @details Immortal strings use a sentinel reference count so they can be
///          shared globally without participating in retain/release bookkeeping.
/// @param hdr Heap header describing the string allocation.
/// @return Non-zero when the header marks an immortal allocation.
static int rt_string_is_immortal_hdr(const rt_heap_hdr_t *hdr)
{
    return hdr && hdr->refcnt >= kImmortalRefcnt;
}

/// @brief Wrap a raw heap payload in a runtime string handle.
/// @details Allocates the small @ref rt_string structure that tracks the payload
///          pointer and associated metadata.  Callers must supply a payload
///          produced by @ref rt_heap_alloc.
/// @param payload Heap-allocated, null-terminated character buffer.
/// @return Runtime string handle owning the payload, or `NULL` on error.
static rt_string rt_string_wrap(char *payload)
{
    if (!payload)
        return NULL;
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    if (!s)
    {
        rt_trap("rt_string_wrap: alloc");
        return NULL;
    }
    s->data = payload;
    s->heap = hdr;
    s->literal_len = 0;
    s->literal_refs = 0;
    return s;
}

/// @brief Allocate a mutable runtime string with the requested length/capacity.
/// @details Uses the shared heap allocator, ensures the capacity accounts for a
///          trailing null terminator, and traps on overflow or allocation
///          failure.  The payload is zero-terminated before being wrapped.
/// @param len Number of bytes initially considered part of the string.
/// @param cap Requested capacity (bytes) excluding the implicit terminator.
/// @return Newly allocated runtime string handle, or `NULL` on failure.
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

/// @brief Return a shared handle representing the empty string.
/// @details Lazily initialises an immortal heap allocation so every caller
///          receives the same handle.  The immortal reference count avoids
///          ref-count churn and allows the handle to be cached globally.
/// @return Runtime string handle that points at an empty immutable string.
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
        if (!empty)
        {
            rt_trap("rt_empty_string: alloc");
            return NULL;
        }
        empty->data = payload;
        empty->heap = hdr;
        empty->literal_len = 0;
        empty->literal_refs = 0;
    }
    return empty;
}

/// @brief Allocate a runtime string from a byte span.
/// @details Copies @p len bytes from @p bytes into a freshly allocated string
///          and ensures the payload is null-terminated.  A null input pointer is
///          treated as an empty span.
/// @param bytes Pointer to the source data.
/// @param len Number of bytes to copy.
/// @return Newly allocated runtime string containing the copied bytes.
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

/// @brief Increment the ownership count for a runtime string handle.
/// @details Literal strings track a small reference counter inside the handle
///          while heap-backed strings delegate to @ref rt_heap_retain.  Immortal
///          strings skip reference updates entirely.
/// @param s Runtime string handle.
/// @return The same handle for chaining.
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

/// @brief Release a reference to a runtime string handle.
/// @details Mirrors @ref rt_string_ref by decrementing literal reference counts
///          or calling @ref rt_heap_release for heap-backed strings.  When the
///          final reference disappears the wrapper structure is freed.
/// @param s Runtime string handle to release.
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

/// @brief Convenience wrapper that releases a possibly-null string handle.
/// @details Present to match historical runtime entry points.  Delegates to
///          @ref rt_string_unref.
/// @param s Runtime string handle.
void rt_str_release_maybe(rt_string s)
{
    rt_string_unref(s);
}

/// @brief Convenience wrapper that retains a possibly-null string handle.
/// @details Provides parity with `rt_str_release_maybe` and ignores null
///          handles while preserving the return value from @ref rt_string_ref.
/// @param s Runtime string handle.
void rt_str_retain_maybe(rt_string s)
{
    (void)rt_string_ref(s);
}

/// @brief Obtain the shared empty string handle.
/// @details Calls @ref rt_empty_string to lazily construct and cache the
///          immortal empty string instance.
/// @return Runtime string handle representing "".
rt_string rt_str_empty(void)
{
    return rt_empty_string();
}

/// @brief Return the BASIC-visible length of a string.
/// @details Delegates to the byte-count helper and exposes the value as a signed
///          64-bit integer to match the runtime ABI.
/// @param s Runtime string handle.
/// @return Length in characters (bytes).
int64_t rt_len(rt_string s)
{
    size_t len = rt_string_len_bytes(s);
    if (len > (size_t)INT64_MAX)
        return INT64_MAX;
    return (int64_t)len;
}

/// @brief Concatenate two runtime strings, consuming the inputs.
/// @details Computes the combined length, allocates a new string, copies the
///          payloads, and releases the input handles when non-null.  Traps on
///          length overflow to maintain deterministic runtime behaviour.
/// @param a First operand; released after concatenation when non-null.
/// @param b Second operand; released after concatenation when non-null.
/// @return Newly allocated string containing `a + b`.
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

/// @brief Extract a substring using zero-based start and length.
/// @details Normalises negative parameters to zero, clamps the slice to the
///          available length, and returns shared handles for trivial cases (such
///          as the full string or the empty string).  The caller owns the
///          returned handle.
/// @param s Source string handle.
/// @param start Zero-based starting index (negative values treated as zero).
/// @param len Requested length (negative values treated as zero).
/// @return Newly allocated substring or shared handles for empty/full slices.
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
    uint64_t requested = (uint64_t)len;
    size_t copy_len = requested > SIZE_MAX ? avail : (size_t)requested;
    if (copy_len > avail)
        copy_len = avail;
    if (copy_len == 0)
        return rt_empty_string();
    if (start_idx == 0 && copy_len == slen)
        return rt_string_ref(s);
    return rt_string_from_bytes(s->data + start_idx, copy_len);
}

/// @brief Implement BASIC's `LEFT$` intrinsic.
/// @details Validates the arguments, returning a shared empty string when
///          `n == 0`, the original string when `n` exceeds the length, and
///          otherwise delegates to @ref rt_substr.
/// @param s Source string handle.
/// @param n Number of characters to take from the left.
/// @return Resulting string.
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
    uint64_t requested = (uint64_t)n;
    if (requested > SIZE_MAX)
        return rt_string_ref(s);
    size_t take = (size_t)requested;
    if (take >= slen)
        return rt_string_ref(s);
    return rt_substr(s, 0, n);
}

/// @brief Implement BASIC's `RIGHT$` intrinsic.
/// @details Mirrors @ref rt_left but slices from the end of the string.  Rejects
///          negative lengths with a descriptive trap message.
/// @param s Source string handle.
/// @param n Number of characters to take from the right.
/// @return Resulting string.
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
    uint64_t requested = (uint64_t)n;
    if (requested > SIZE_MAX)
        return rt_string_ref(s);
    size_t take = (size_t)requested;
    if (take >= len)
        return rt_string_ref(s);
    size_t start = len - take;
    return rt_substr(s, (int64_t)start, n);
}

/// @brief Implement BASIC's two-argument `MID$` overload.
/// @details Interprets @p start as one-based, returns the original string when
///          @p start == 1, and otherwise slices from the specified position to
///          the end.  Negative or zero starts trigger traps with detailed
///          messages.
/// @param s Source string handle.
/// @param start One-based starting position.
/// @return Resulting substring.
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

/// @brief Implement BASIC's three-argument `MID$` overload.
/// @details Applies the same one-based semantics as @ref rt_mid2 while
///          respecting the requested length.  Negative lengths trigger traps and
///          slices that extend beyond the source string are clamped.
/// @param s Source string handle.
/// @param start One-based starting position.
/// @param len Requested substring length.
/// @return Resulting substring.
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
    size_t avail = slen - start_idx;
    uint64_t requested = (uint64_t)len;
    if (requested > SIZE_MAX)
    {
        if (start_idx == 0)
            return rt_string_ref(s);
        len = (int64_t)avail;
    }
    else
    {
        size_t req_len = (size_t)requested;
        if (start_idx == 0 && req_len >= slen)
            return rt_string_ref(s);
        if (req_len >= avail)
            len = (int64_t)avail;
        else
            len = (int64_t)req_len;
    }
    return rt_substr(s, (int64_t)start_idx, len);
}

/// @brief Search for a substring using zero-based indexing.
/// @details Implements the shared search logic for the INSTR family.  Handles
///          null operands, clamps the starting position, and returns the
///          one-based index mandated by BASIC (or zero when not found).
/// @param hay Haystack string to scan.
/// @param start Zero-based starting offset.
/// @param needle Needle string to locate.
/// @return One-based index of the first match, or zero when absent.
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

/// @brief Implement BASIC's two-argument `INSTR` intrinsic.
/// @details Delegates to @ref rt_find after handling the empty-needle case,
///          which returns 1 per the language rules.
/// @param hay Haystack string.
/// @param needle Needle string.
/// @return One-based index of the first match, or zero when absent.
int64_t rt_instr2(rt_string hay, rt_string needle)
{
    if (!hay || !needle)
        return 0;
    size_t needle_len = rt_string_len_bytes(needle);
    if (needle_len == 0)
        return 1;
    return rt_find(hay, 0, needle);
}

/// @brief Implement BASIC's three-argument `INSTR` intrinsic.
/// @details Accepts a one-based starting position, normalises it to zero-based
///          for the internal search, and honours the empty-needle rule by
///          returning @p start when the needle is empty.
/// @param start One-based starting position supplied by the caller.
/// @param hay Haystack string.
/// @param needle Needle string.
/// @return One-based index of the first match at or after @p start, or zero when absent.
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
/// @details Walks the leading whitespace and delegates to @ref rt_substr to
///          materialise the trimmed view.
/// @param s Source string.
/// @return Trimmed string handle.
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
/// @details Scans from the end of the string and returns a substring covering
///          the retained prefix.
/// @param s Source string.
/// @return Trimmed string handle.
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
/// @details Calculates the slice indices in-place and delegates to
///          @ref rt_substr to allocate the final result.
/// @param s Source string.
/// @return Trimmed string handle.
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

/// @brief Convert ASCII letters in a string to upper case.
/// @details Allocates a new string of the same length and maps lowercase ASCII
///          characters to their uppercase equivalents.
/// @param s Source string.
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

/// @brief Convert ASCII letters in a string to lower case.
/// @details Symmetric counterpart to @ref rt_ucase that maps uppercase ASCII
///          characters to lowercase.
/// @param s Source string.
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

/// @brief Compare two runtime strings for equality.
/// @details Performs pointer short-circuiting, length comparison, and a byte
///          wise comparison to determine equality.
/// @param a First operand.
/// @param b Second operand.
/// @return 1 when equal, otherwise 0.
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
