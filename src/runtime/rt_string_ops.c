//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const size_t kImmortalRefcnt = SIZE_MAX - 1;

/// @brief Retrieve the heap header associated with a runtime string.
/// @details Returns `NULL` for literal strings and embedded (SSO) strings that
///          are not backed by the shared heap. Asserts that heap-backed strings
///          carry the expected kind. Callers use this to peek at reference counts
///          or capacities without duplicating validation logic.
/// @param s Runtime string handle.
/// @return Heap header describing the allocation, or `NULL` for literals/embedded.
static rt_heap_hdr_t *rt_string_header(rt_string s)
{
    if (!s || !s->heap || s->heap == RT_SSO_SENTINEL)
        return NULL;
    assert(s->heap->kind == RT_HEAP_STRING);
    return s->heap;
}

/// @brief Report the byte length of a runtime string payload.
/// @details Handles literal strings, embedded (SSO) strings, and heap-backed
///          strings. Literal and embedded strings store the length in literal_len,
///          while heap-backed strings derive the length from the heap header.
///          Null handles yield zero, allowing callers to treat them as empty.
/// @param s Runtime string handle.
/// @return Number of bytes in the string, excluding the terminator.
static size_t rt_string_len_bytes(rt_string s)
{
    if (!s)
        return 0;
    // Literal strings (heap == NULL) and embedded strings (heap == RT_SSO_SENTINEL)
    // both store length in literal_len
    if (!s->heap || s->heap == RT_SSO_SENTINEL)
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
    if (!hdr)
        return 0;
    return __atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED) >= kImmortalRefcnt;
}

/// @brief Check if a string uses embedded (SSO) storage.
/// @param s Runtime string handle.
/// @return Non-zero if string uses embedded storage.
static int rt_string_is_embedded(rt_string s)
{
    return s && s->heap == RT_SSO_SENTINEL;
}

/// @brief Check if a string can be extended in-place for concatenation.
/// @details Returns non-zero if:
///          - String is heap-backed (not literal or SSO)
///          - Reference count is exactly 1 (sole owner)
///          - String is not immortal
///          - Capacity is sufficient for the required length
/// @param s Runtime string handle.
/// @param required_len Total length required (including null terminator capacity).
/// @return Non-zero if in-place append is possible.
static int rt_string_can_append_inplace(rt_string s, size_t required_len)
{
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr)
        return 0; // Not heap-backed
    if (rt_string_is_immortal_hdr(hdr))
        return 0; // Immortal strings cannot be modified
    size_t refcnt = __atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED);
    if (refcnt != 1)
        return 0; // Not sole owner
    // Capacity stores the total allocation size (includes space for null)
    if (hdr->cap < required_len)
        return 0; // Not enough capacity
    return 1;
}

/// @brief Allocate a runtime string with embedded data storage.
/// @details For small strings, this allocates the handle and string data in a
///          single allocation, with the data following immediately after the
///          rt_string_impl struct. This eliminates one heap allocation compared
///          to the traditional two-allocation approach.
/// @param len Number of bytes in the string (must be <= RT_SSO_MAX_LEN).
/// @return Newly allocated embedded string, or NULL on failure.
static rt_string rt_string_alloc_embedded(size_t len)
{
    assert(len <= RT_SSO_MAX_LEN);
    // Allocate struct + string data + null terminator in one block
    size_t total = sizeof(struct rt_string_impl) + len + 1;
    rt_string s = (rt_string)rt_alloc((int64_t)total);
    if (!s)
    {
        rt_trap("rt_string_alloc_embedded: alloc");
        return NULL;
    }
    s->magic = RT_STRING_MAGIC;
    // Data is embedded immediately after the struct
    s->data = (char *)(s + 1);
    s->heap = RT_SSO_SENTINEL;
    s->literal_len = len;
    s->literal_refs = 1; // Reference count for embedded strings
    s->data[len] = '\0';
    return s;
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
    s->magic = RT_STRING_MAGIC;
    s->data = payload;
    s->heap = hdr;
    s->literal_len = 0;
    s->literal_refs = 0;
    return s;
}

/// @brief Allocate a mutable runtime string with the requested length/capacity.
/// @details Uses embedded allocation for small strings (len <= RT_SSO_MAX_LEN),
///          otherwise uses the shared heap allocator. Ensures the capacity
///          accounts for a trailing null terminator, and traps on overflow or
///          allocation failure. The payload is zero-terminated before being
///          wrapped.
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
    // Use embedded allocation for small strings
    if (len <= RT_SSO_MAX_LEN && cap <= RT_SSO_MAX_LEN + 1)
    {
        return rt_string_alloc_embedded(len);
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
    rt_string cached = __atomic_load_n(&empty, __ATOMIC_ACQUIRE);
    if (cached)
        return cached;

    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, 0, 1);
    if (!payload)
        rt_trap("rt_empty_string: alloc");
    payload[0] = '\0';

    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    __atomic_store_n(&hdr->refcnt, kImmortalRefcnt, __ATOMIC_RELAXED);

    rt_string candidate = (rt_string)rt_alloc(sizeof(*candidate));
    if (!candidate)
    {
        rt_trap("rt_empty_string: alloc");
        return NULL;
    }
    candidate->magic = RT_STRING_MAGIC;
    candidate->data = payload;
    candidate->heap = hdr;
    candidate->literal_len = 0;
    candidate->literal_refs = 0;

    rt_string expected = NULL;
    if (!__atomic_compare_exchange_n(
            &empty, &expected, candidate, /*weak=*/0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
    {
        // Lost the race; discard our candidate and use the published singleton.
        free(candidate);
        free((void *)hdr);
        return expected;
    }
    return candidate;
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

/// @brief Create a runtime string from a string literal.
/// @details Wrapper for rt_str_from_bytes for x86_64 codegen.
/// @param bytes Pointer to the literal data.
/// @param len Number of bytes in the literal.
/// @return Runtime string containing the literal data.
rt_string rt_str_from_lit(const char *bytes, size_t len)
{
    return rt_string_from_bytes(bytes, len);
}

int rt_string_is_handle(void *p)
{
    if (!p)
        return 0;
    const rt_string s = (const rt_string)p;
    return s->magic == RT_STRING_MAGIC ? 1 : 0;
}

/// @brief Increment the ownership count for a runtime string handle.
/// @details Literal and embedded (SSO) strings track a reference counter inside
///          the handle (literal_refs), while heap-backed strings delegate to
///          @ref rt_heap_retain. Immortal strings skip reference updates entirely.
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
/// @details Mirrors @ref rt_string_ref by decrementing literal/embedded reference
///          counts or calling @ref rt_heap_release for heap-backed strings. When
///          the final reference disappears the wrapper structure is freed. For
///          embedded (SSO) strings, this frees the combined handle+data allocation.
/// @param s Runtime string handle to release.
void rt_string_unref(rt_string s)
{
    if (!s)
        return;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr)
    {
        // Skip decrement for immortal literals (literal_refs == SIZE_MAX)
        if (s->literal_refs > 0 && s->literal_refs < SIZE_MAX && --s->literal_refs == 0)
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

/// @brief Return 1 when the runtime string is empty; 0 otherwise.
/// @details Null handles are treated as empty to match rt_len semantics.
/// @param s Runtime string handle.
/// @return 1 if empty; 0 otherwise.
int64_t rt_str_is_empty(rt_string s)
{
    return rt_len(s) == 0 ? 1 : 0;
}

/// @brief Identity constructor from an existing runtime string handle.
/// @details Used as a thin shim for Viper.Strings.FromStr; returns the input
///          handle unchanged. Callers manage ownership according to IL/VM rules.
/// @param s Runtime string handle.
/// @return The same handle.
rt_string rt_from_str(rt_string s)
{
    return s;
}

/// @brief Concatenate two runtime strings, consuming the inputs.
/// @details Computes the combined length, allocates a new string, copies the
///          payloads, and releases the input handles when non-null.  Traps on
///          length overflow to maintain deterministic runtime behaviour.
///
///          Optimization: When the left operand is uniquely owned (refcount == 1),
///          heap-backed, and has sufficient capacity, the concatenation is performed
///          in-place by appending to the existing buffer. This avoids allocation
///          in common patterns like repeated string concatenation in loops.
///
/// @param a First operand; released after concatenation when non-null.
/// @param b Second operand; released after concatenation when non-null.
/// @return Newly allocated string containing `a + b`, or `a` reused in-place.
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

    // Optimization: append in-place when `a` is uniquely owned with enough capacity
    if (rt_string_can_append_inplace(a, total + 1))
    {
        // Append `b` directly into `a`'s buffer
        if (b && b->data && len_b > 0)
            memcpy(a->data + len_a, b->data, len_b);
        a->data[total] = '\0';
        // Update the length in the heap header
        rt_heap_set_len(a->data, total);
        // Release `b` (consumed by concat)
        if (b)
            rt_string_unref(b);
        // Return `a` reused (not released, ownership transferred)
        return a;
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

    // Optimized substring search using memchr for first-character scan.
    // memchr is typically SIMD-optimized and much faster than byte-by-byte scanning.
    // This reduces the naive O(n*m) to O(n) for the first-character search,
    // only falling back to memcmp when we find a potential match.
    const char first = needle->data[0];
    const char *pos = hay->data + start_idx;
    const char *end = hay->data + hay_len - needle_len + 1;

    while (pos < end) {
        pos = memchr(pos, first, (size_t)(end - pos));
        if (!pos)
            return 0;
        if (memcmp(pos, needle->data, needle_len) == 0)
            return (int64_t)(pos - hay->data + 1);
        ++pos;
    }
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

int64_t rt_str_index_of_from(rt_string hay, int64_t start, rt_string needle)
{
    return rt_instr3(start, hay, needle);
}

static int is_trim_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
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
    while (i < slen && is_trim_ws(s->data[i]))
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
    while (end > 0 && is_trim_ws(s->data[end - 1]))
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
    while (start < end && is_trim_ws(s->data[start]))
        ++start;
    while (end > start && is_trim_ws(s->data[end - 1]))
        --end;
    return rt_substr(s, (int64_t)start, (int64_t)(end - start));
}

/// @brief Convert a single byte to uppercase (ASCII + Latin-1 Supplement).
/// @details Handles ASCII a-z and Latin-1 lowercase letters (à-ö, ø-ÿ).
///          Non-letter bytes and other Unicode characters pass through unchanged.
/// @param c Input byte.
/// @return Uppercase equivalent or original byte.
static unsigned char to_upper_latin1(unsigned char c)
{
    // ASCII lowercase a-z
    if (c >= 'a' && c <= 'z')
        return (unsigned char)(c - 'a' + 'A');
    // Latin-1 Supplement lowercase: à-ö (0xE0-0xF6) -> À-Ö (0xC0-0xD6)
    if (c >= 0xE0 && c <= 0xF6 && c != 0xF7) // 0xF7 is ÷ (division sign)
        return (unsigned char)(c - 0x20);
    // Latin-1 Supplement lowercase: ø-þ (0xF8-0xFE) -> Ø-Þ (0xD8-0xDE)
    if (c >= 0xF8 && c <= 0xFE)
        return (unsigned char)(c - 0x20);
    // ÿ (0xFF) -> Ÿ (0x178) - but 0x178 is outside Latin-1, leave as-is
    return c;
}

/// @brief Convert letters in a string to upper case (ASCII + Latin-1).
/// @details Allocates a new string and maps lowercase letters to uppercase.
///          Handles ASCII (a-z) and Latin-1 Supplement characters (à-ÿ).
///          UTF-8 multi-byte characters (Cyrillic, Greek, CJK, etc.) are
///          passed through unchanged - full Unicode case mapping requires ICU.
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
        // Skip UTF-8 continuation bytes (10xxxxxx) and multi-byte lead bytes
        if ((c & 0x80) == 0)
        {
            // ASCII byte - apply case conversion
            c = to_upper_latin1(c);
        }
        // Multi-byte UTF-8 sequences pass through unchanged
        r->data[i] = (char)c;
    }
    r->data[len] = '\0';
    return r;
}

/// @brief Convert a single byte to lowercase (ASCII + Latin-1 Supplement).
/// @details Handles ASCII A-Z and Latin-1 uppercase letters (À-Ö, Ø-Þ).
///          Non-letter bytes and other Unicode characters pass through unchanged.
/// @param c Input byte.
/// @return Lowercase equivalent or original byte.
static unsigned char to_lower_latin1(unsigned char c)
{
    // ASCII uppercase A-Z
    if (c >= 'A' && c <= 'Z')
        return (unsigned char)(c - 'A' + 'a');
    // Latin-1 Supplement uppercase: À-Ö (0xC0-0xD6) -> à-ö (0xE0-0xF6)
    if (c >= 0xC0 && c <= 0xD6 && c != 0xD7) // 0xD7 is × (multiplication sign)
        return (unsigned char)(c + 0x20);
    // Latin-1 Supplement uppercase: Ø-Þ (0xD8-0xDE) -> ø-þ (0xF8-0xFE)
    if (c >= 0xD8 && c <= 0xDE)
        return (unsigned char)(c + 0x20);
    return c;
}

/// @brief Convert letters in a string to lower case (ASCII + Latin-1).
/// @details Allocates a new string and maps uppercase letters to lowercase.
///          Handles ASCII (A-Z) and Latin-1 Supplement characters (À-Þ).
///          UTF-8 multi-byte characters (Cyrillic, Greek, CJK, etc.) are
///          passed through unchanged - full Unicode case mapping requires ICU.
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
        // Skip UTF-8 multi-byte sequences
        if ((c & 0x80) == 0)
        {
            // ASCII byte - apply case conversion
            c = to_lower_latin1(c);
        }
        // Multi-byte UTF-8 sequences pass through unchanged
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

int64_t rt_str_lt(rt_string a, rt_string b)
{
    if (!a || !b)
        return 0;
    if (a == b)
        return 0;
    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;
    int cmp = memcmp(a->data, b->data, minlen);
    if (cmp != 0)
        return cmp < 0;
    return alen < blen;
}

int64_t rt_str_le(rt_string a, rt_string b)
{
    if (!a || !b)
        return a == b;
    if (a == b)
        return 1;
    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;
    int cmp = memcmp(a->data, b->data, minlen);
    if (cmp != 0)
        return cmp < 0;
    return alen <= blen;
}

int64_t rt_str_gt(rt_string a, rt_string b)
{
    if (!a || !b)
        return 0;
    if (a == b)
        return 0;
    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;
    int cmp = memcmp(a->data, b->data, minlen);
    if (cmp != 0)
        return cmp > 0;
    return alen > blen;
}

int64_t rt_str_ge(rt_string a, rt_string b)
{
    if (!a || !b)
        return a == b;
    if (a == b)
        return 1;
    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;
    int cmp = memcmp(a->data, b->data, minlen);
    if (cmp != 0)
        return cmp > 0;
    return alen >= blen;
}

//===----------------------------------------------------------------------===//
// Extended String Functions (Viper.String expansion)
//===----------------------------------------------------------------------===//

/// @brief Replace all occurrences of needle with replacement.
/// @param haystack Source string.
/// @param needle String to find.
/// @param replacement String to substitute.
/// @return Newly allocated string with replacements.
rt_string rt_str_replace(rt_string haystack, rt_string needle, rt_string replacement)
{
    if (!haystack)
        return rt_empty_string();
    if (!needle || !replacement)
        return rt_string_ref(haystack);

    size_t hay_len = rt_string_len_bytes(haystack);
    size_t needle_len = rt_string_len_bytes(needle);
    size_t repl_len = rt_string_len_bytes(replacement);

    // Empty needle: return original string
    if (needle_len == 0)
        return rt_string_ref(haystack);

    // Single-pass algorithm using string builder.
    // This eliminates the double-scan (count + build) that was O(2*n*m).
    // Instead we scan once, building the result as we go.
    rt_string_builder sb;
    rt_sb_init(&sb);

    const char *p = haystack->data;
    const char *end = p + hay_len;
    const char *prev = p;
    const char first = needle->data[0];
    int found_any = 0;

    // Use memchr for fast first-character scanning (SIMD-optimized)
    while (p <= end - needle_len)
    {
        // Fast scan for first character of needle
        const char *match = memchr(p, first, (size_t)(end - needle_len - p + 1));
        if (!match)
            break;

        p = match;
        if (memcmp(p, needle->data, needle_len) == 0)
        {
            found_any = 1;
            // Append chunk before match
            size_t chunk = (size_t)(p - prev);
            if (chunk > 0)
            {
                if (rt_sb_append_bytes(&sb, prev, chunk) != RT_SB_OK)
                {
                    rt_sb_free(&sb);
                    rt_trap("rt_str_replace: allocation failed");
                    return NULL;
                }
            }
            // Append replacement
            if (repl_len > 0)
            {
                if (rt_sb_append_bytes(&sb, replacement->data, repl_len) != RT_SB_OK)
                {
                    rt_sb_free(&sb);
                    rt_trap("rt_str_replace: allocation failed");
                    return NULL;
                }
            }
            p += needle_len;
            prev = p;
        }
        else
        {
            p++;
        }
    }

    // No matches found - return original string (avoid allocation)
    if (!found_any)
    {
        rt_sb_free(&sb);
        return rt_string_ref(haystack);
    }

    // Append remainder after last match
    size_t remainder = (size_t)(end - prev);
    if (remainder > 0)
    {
        if (rt_sb_append_bytes(&sb, prev, remainder) != RT_SB_OK)
        {
            rt_sb_free(&sb);
            rt_trap("rt_str_replace: allocation failed");
            return NULL;
        }
    }

    // Create result string from builder
    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);

    return result ? result : rt_empty_string();
}

/// @brief Check if string starts with prefix.
/// @param str Source string.
/// @param prefix Prefix to check.
/// @return 1 if str starts with prefix, 0 otherwise.
int64_t rt_str_starts_with(rt_string str, rt_string prefix)
{
    if (!str || !prefix)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t prefix_len = rt_string_len_bytes(prefix);

    if (prefix_len > str_len)
        return 0;
    if (prefix_len == 0)
        return 1;

    return memcmp(str->data, prefix->data, prefix_len) == 0;
}

/// @brief Check if string ends with suffix.
/// @param str Source string.
/// @param suffix Suffix to check.
/// @return 1 if str ends with suffix, 0 otherwise.
int64_t rt_str_ends_with(rt_string str, rt_string suffix)
{
    if (!str || !suffix)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t suffix_len = rt_string_len_bytes(suffix);

    if (suffix_len > str_len)
        return 0;
    if (suffix_len == 0)
        return 1;

    return memcmp(str->data + str_len - suffix_len, suffix->data, suffix_len) == 0;
}

/// @brief Check if string contains needle.
/// @param str Source string.
/// @param needle Substring to find.
/// @return 1 if str contains needle, 0 otherwise.
int64_t rt_str_has(rt_string str, rt_string needle)
{
    if (!str || !needle)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t needle_len = rt_string_len_bytes(needle);

    if (needle_len == 0)
        return 1;
    if (needle_len > str_len)
        return 0;

    // Simple substring search
    for (size_t i = 0; i + needle_len <= str_len; i++)
    {
        if (memcmp(str->data + i, needle->data, needle_len) == 0)
            return 1;
    }
    return 0;
}

/// @brief Count non-overlapping occurrences of needle in str.
/// @param str Source string.
/// @param needle Substring to count.
/// @return Number of non-overlapping occurrences.
int64_t rt_str_count(rt_string str, rt_string needle)
{
    if (!str || !needle)
        return 0;

    size_t str_len = rt_string_len_bytes(str);
    size_t needle_len = rt_string_len_bytes(needle);

    if (needle_len == 0)
        return 0;
    if (needle_len > str_len)
        return 0;

    int64_t count = 0;
    const char *p = str->data;
    const char *end = p + str_len;

    while (p <= end - needle_len)
    {
        if (memcmp(p, needle->data, needle_len) == 0)
        {
            count++;
            p += needle_len; // Non-overlapping
        }
        else
        {
            p++;
        }
    }

    return count;
}

/// @brief Pad string on the left to reach specified width.
/// @param str Source string.
/// @param width Target width.
/// @param pad_str Padding character (first char used).
/// @return Newly allocated padded string.
rt_string rt_str_pad_left(rt_string str, int64_t width, rt_string pad_str)
{
    if (!str)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);

    if (width <= (int64_t)str_len || !pad_str || rt_string_len_bytes(pad_str) == 0)
        return rt_string_ref(str);

    char pad_char = pad_str->data[0];
    size_t target = (size_t)width;
    size_t pad_count = target - str_len;

    rt_string result = rt_string_alloc(target, target + 1);
    if (!result)
        return NULL;

    memset(result->data, pad_char, pad_count);
    memcpy(result->data + pad_count, str->data, str_len);
    result->data[target] = '\0';

    return result;
}

/// @brief Pad string on the right to reach specified width.
/// @param str Source string.
/// @param width Target width.
/// @param pad_str Padding character (first char used).
/// @return Newly allocated padded string.
rt_string rt_str_pad_right(rt_string str, int64_t width, rt_string pad_str)
{
    if (!str)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);

    if (width <= (int64_t)str_len || !pad_str || rt_string_len_bytes(pad_str) == 0)
        return rt_string_ref(str);

    char pad_char = pad_str->data[0];
    size_t target = (size_t)width;
    size_t pad_count = target - str_len;

    rt_string result = rt_string_alloc(target, target + 1);
    if (!result)
        return NULL;

    memcpy(result->data, str->data, str_len);
    memset(result->data + str_len, pad_char, pad_count);
    result->data[target] = '\0';

    return result;
}

/// @brief Split string by delimiter into a sequence.
/// @param str Source string.
/// @param delim Delimiter string.
/// @return Seq containing string parts.
void *rt_str_split(rt_string str, rt_string delim)
{
    if (!str)
    {
        // Push empty string for null input
        void *result = rt_seq_with_capacity(1);
        rt_seq_push(result, (void *)rt_empty_string());
        return result;
    }

    size_t str_len = rt_string_len_bytes(str);
    size_t delim_len = delim ? rt_string_len_bytes(delim) : 0;

    // Empty delimiter: return single element with original string
    if (delim_len == 0)
    {
        void *result = rt_seq_with_capacity(1);
        rt_seq_push(result, (void *)rt_string_ref(str));
        return result;
    }

    // Pass 1: Count delimiters to pre-allocate result sequence
    // Uses memchr for SIMD-optimized first-character scanning
    const char *p = str->data;
    const char *end = str->data + str_len;
    const char first = delim->data[0];
    size_t count = 1; // At least one segment

    while (p <= end - delim_len)
    {
        const char *match = memchr(p, first, (size_t)(end - delim_len - p + 1));
        if (!match)
            break;

        p = match;
        if (memcmp(p, delim->data, delim_len) == 0)
        {
            count++;
            p += delim_len;
        }
        else
        {
            p++;
        }
    }

    // Pre-allocate sequence with exact capacity
    void *result = rt_seq_with_capacity((int64_t)count);

    // Pass 2: Build segments
    const char *start = str->data;
    p = str->data;

    while (p <= end - delim_len)
    {
        const char *match = memchr(p, first, (size_t)(end - delim_len - p + 1));
        if (!match)
            break;

        p = match;
        if (memcmp(p, delim->data, delim_len) == 0)
        {
            size_t chunk_len = (size_t)(p - start);
            rt_string chunk = rt_string_from_bytes(start, chunk_len);
            rt_seq_push(result, (void *)chunk);
            p += delim_len;
            start = p;
        }
        else
        {
            p++;
        }
    }

    // Add final segment
    size_t final_len = (size_t)(end - start);
    rt_string final_str = rt_string_from_bytes(start, final_len);
    rt_seq_push(result, (void *)final_str);

    return result;
}

/// @brief Join sequence of strings with separator.
/// @param sep Separator string.
/// @param seq Sequence of strings to join.
/// @return Newly allocated joined string.
rt_string rt_strings_join(rt_string sep, void *seq)
{
    if (!seq)
        return rt_empty_string();

    int64_t len = rt_seq_len(seq);
    if (len == 0)
        return rt_empty_string();

    size_t sep_len = sep ? rt_string_len_bytes(sep) : 0;

    // Calculate total length
    size_t total = 0;
    for (int64_t i = 0; i < len; i++)
    {
        rt_string item = (rt_string)rt_seq_get(seq, i);
        size_t item_len = item ? rt_string_len_bytes(item) : 0;
        if (total > SIZE_MAX - item_len)
        {
            rt_trap("rt_strings_join: length overflow");
            return NULL;
        }
        total += item_len;
        if (i < len - 1 && sep_len > 0)
        {
            if (total > SIZE_MAX - sep_len)
            {
                rt_trap("rt_strings_join: length overflow");
                return NULL;
            }
            total += sep_len;
        }
    }

    rt_string result = rt_string_alloc(total, total + 1);
    if (!result)
        return NULL;

    char *dst = result->data;
    for (int64_t i = 0; i < len; i++)
    {
        rt_string item = (rt_string)rt_seq_get(seq, i);
        size_t item_len = item ? rt_string_len_bytes(item) : 0;
        if (item_len > 0)
        {
            memcpy(dst, item->data, item_len);
            dst += item_len;
        }

        if (i < len - 1 && sep_len > 0)
        {
            memcpy(dst, sep->data, sep_len);
            dst += sep_len;
        }
    }

    *dst = '\0';
    return result;
}

/// @brief Repeat string count times.
/// @param str Source string.
/// @param count Number of repetitions.
/// @return Newly allocated repeated string.
rt_string rt_str_repeat(rt_string str, int64_t count)
{
    if (!str || count <= 0)
        return rt_empty_string();

    size_t str_len = rt_string_len_bytes(str);
    if (str_len == 0)
        return rt_empty_string();

    // Check for overflow
    if ((size_t)count > SIZE_MAX / str_len)
    {
        rt_trap("rt_str_repeat: length overflow");
        return NULL;
    }

    size_t total = str_len * (size_t)count;
    rt_string result = rt_string_alloc(total, total + 1);
    if (!result)
        return NULL;

    char *dst = result->data;
    for (int64_t i = 0; i < count; i++)
    {
        memcpy(dst, str->data, str_len);
        dst += str_len;
    }

    *dst = '\0';
    return result;
}

/// @brief Get UTF-8 character byte length from leading byte.
/// @param c First byte of UTF-8 sequence.
/// @return Number of bytes in the character (1-4), or 1 for invalid.
static size_t utf8_char_len(unsigned char c)
{
    if ((c & 0x80) == 0)
        return 1; // ASCII: 0xxxxxxx
    if ((c & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    if ((c & 0xF0) == 0xE0)
        return 3; // 1110xxxx
    if ((c & 0xF8) == 0xF0)
        return 4; // 11110xxx
    return 1;     // Invalid, treat as single byte
}

/// @brief Reverse string characters (UTF-8 aware).
/// @details Reverses the sequence of Unicode codepoints, not bytes.
///          For ASCII-only strings, this is equivalent to byte reversal.
///          For UTF-8 strings with multi-byte characters, this preserves
///          character integrity (e.g., "Hello, 世界!" becomes "!界世 ,olleH").
/// @param str Source string.
/// @return Newly allocated reversed string.
rt_string rt_str_flip(rt_string str)
{
    if (!str)
        return rt_empty_string();

    size_t len = rt_string_len_bytes(str);
    if (len == 0)
        return rt_empty_string();

    const char *data = str->data;

    // First pass: count characters and find their start positions
    size_t char_count = 0;
    for (size_t i = 0; i < len;)
    {
        size_t clen = utf8_char_len((unsigned char)data[i]);
        if (i + clen > len)
            clen = len - i; // Handle truncated sequences
        i += clen;
        char_count++;
    }

    // Allocate positions array (offsets of each character start)
    size_t *positions = (size_t *)malloc((char_count + 1) * sizeof(size_t));
    if (!positions)
        return NULL;

    // Second pass: record character positions
    size_t idx = 0;
    for (size_t i = 0; i < len;)
    {
        positions[idx++] = i;
        size_t clen = utf8_char_len((unsigned char)data[i]);
        if (i + clen > len)
            clen = len - i;
        i += clen;
    }
    positions[char_count] = len; // End sentinel

    // Allocate result buffer
    rt_string result = rt_string_alloc(len, len + 1);
    if (!result)
    {
        free(positions);
        return NULL;
    }

    // Build reversed string by copying characters in reverse order
    size_t dest = 0;
    for (size_t i = char_count; i > 0; i--)
    {
        size_t start = positions[i - 1];
        size_t end = positions[i];
        size_t clen = end - start;
        memcpy(result->data + dest, data + start, clen);
        dest += clen;
    }
    result->data[len] = '\0';

    free(positions);
    return result;
}

/// @brief Compare two strings, returning -1, 0, or 1.
/// @param a First string.
/// @param b Second string.
/// @return -1 if a < b, 0 if a == b, 1 if a > b.
int64_t rt_str_cmp(rt_string a, rt_string b)
{
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;

    int result = memcmp(a->data, b->data, minlen);
    if (result != 0)
        return (result > 0) - (result < 0);

    if (alen < blen)
        return -1;
    if (alen > blen)
        return 1;
    return 0;
}

/// @brief Case-insensitive string comparison, returning -1, 0, or 1.
/// @param a First string.
/// @param b Second string.
/// @return -1 if a < b, 0 if a == b, 1 if a > b (case-insensitive).
int64_t rt_str_cmp_nocase(rt_string a, rt_string b)
{
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    size_t alen = rt_string_len_bytes(a);
    size_t blen = rt_string_len_bytes(b);
    size_t minlen = alen < blen ? alen : blen;

    for (size_t i = 0; i < minlen; i++)
    {
        unsigned char ca = (unsigned char)tolower((unsigned char)a->data[i]);
        unsigned char cb = (unsigned char)tolower((unsigned char)b->data[i]);
        if (ca < cb)
            return -1;
        if (ca > cb)
            return 1;
    }

    if (alen < blen)
        return -1;
    if (alen > blen)
        return 1;
    return 0;
}
