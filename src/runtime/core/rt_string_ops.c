//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string_ops.c
// Purpose: Core string operations for the Viper runtime. Contains memory
//   management (alloc, refcount, retain/release), basic operations (concat,
//   substr, left/right/mid), searching (index_of, instr), trimming, case
//   conversion (ucase/lcase), and comparison operators. Extended operations
//   live in rt_string_advanced.c and rt_string_specialized.c.
//
// Key invariants:
//   - Runtime strings are reference-counted; literal and embedded (SSO) strings
//     may have immortal refcounts (SIZE_MAX-1) and are never freed.
//   - All intrinsics trap on NULL or invalid arguments rather than returning
//     error codes, matching VM behaviour precisely.
//   - Slice operations (Left/Mid/Right) produce new heap-backed strings; they
//     never alias into the source string's storage.
//   - Case conversion is byte-level ASCII; multi-byte UTF-8 sequences are passed
//     through unchanged.
//   - String lengths are reported in bytes, not Unicode code points.
//   - rt_str_flip() is the sole codepoint-aware operation: it walks the string
//     using utf8_char_len() (1-4 byte sequences) and reverses whole codepoints.
//   - See rt_string.h "Encoding & indexing" for the full byte-indexing contract.
//
// Ownership/Lifetime:
//   - Functions that return rt_string transfer a new reference to the caller;
//     the caller must call rt_string_unref when finished.
//   - Functions that accept rt_string borrow the reference; they do not retain
//     or release the input.
//
// Links: src/runtime/core/rt_string_internal.h (shared helpers),
//        src/runtime/core/rt_string_advanced.c (extended operations),
//        src/runtime/core/rt_string_specialized.c (case conversion + distance),
//        src/runtime/core/rt_string.h (rt_string type and ref-counting API)
//
//===----------------------------------------------------------------------===//

#include "rt_int_format.h"
#include "rt_internal.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_string_internal.h"

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
static rt_heap_hdr_t *rt_string_header(rt_string s) {
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
size_t rt_string_len_bytes(rt_string s) {
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
static int rt_string_is_immortal_hdr(const rt_heap_hdr_t *hdr) {
    if (!hdr)
        return 0;
    return __atomic_load_n(&hdr->refcnt, __ATOMIC_RELAXED) >= kImmortalRefcnt;
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
static int rt_string_can_append_inplace(rt_string s, size_t required_len) {
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
static rt_string rt_string_alloc_embedded(size_t len) {
    assert(len <= RT_SSO_MAX_LEN);
    // Allocate struct + string data + null terminator in one block
    size_t total = sizeof(struct rt_string_impl) + len + 1;
    rt_string s = (rt_string)rt_alloc((int64_t)total);
    if (!s) {
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
static rt_string rt_string_wrap(char *payload) {
    if (!payload)
        return NULL;
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    assert(hdr);
    assert(hdr->kind == RT_HEAP_STRING);
    rt_string s = (rt_string)rt_alloc(sizeof(*s));
    if (!s) {
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
rt_string rt_string_alloc(size_t len, size_t cap) {
    if (len >= SIZE_MAX) {
        rt_trap("rt_string_alloc: length overflow");
        return NULL;
    }
    // Use embedded allocation for small strings
    if (len <= RT_SSO_MAX_LEN && cap <= RT_SSO_MAX_LEN + 1) {
        return rt_string_alloc_embedded(len);
    }
    size_t required = len + 1;
    if (cap < required)
        cap = required;
    char *payload = (char *)rt_heap_alloc(RT_HEAP_STRING, RT_ELEM_NONE, 1, len, cap);
    if (!payload) {
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
rt_string rt_empty_string(void) {
    static rt_string empty = NULL;
#if RT_COMPILER_MSVC
    rt_string cached =
        (rt_string)rt_atomic_load_ptr((void *const volatile *)&empty, __ATOMIC_ACQUIRE);
#else
    rt_string cached = __atomic_load_n(&empty, __ATOMIC_ACQUIRE);
#endif
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
    if (!candidate) {
        rt_trap("rt_empty_string: alloc");
        return NULL;
    }
    candidate->magic = RT_STRING_MAGIC;
    candidate->data = payload;
    candidate->heap = hdr;
    candidate->literal_len = 0;
    candidate->literal_refs = 0;

    rt_string expected = NULL;
#if RT_COMPILER_MSVC
    if (!rt_atomic_compare_exchange_ptr((void *volatile *)&empty,
                                        (void **)&expected,
                                        candidate,
                                        __ATOMIC_RELEASE,
                                        __ATOMIC_ACQUIRE))
#else
    if (!__atomic_compare_exchange_n(
            &empty, &expected, candidate, /*weak=*/0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
#endif
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
rt_string rt_string_from_bytes(const char *bytes, size_t len) {
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
rt_string rt_str_from_lit(const char *bytes, size_t len) {
    return rt_string_from_bytes(bytes, len);
}

int8_t rt_string_is_handle(void *p) {
    if (!p)
        return 0;
    const rt_string s = (const rt_string)p;
    return s->magic == RT_STRING_MAGIC ? 1 : 0;
}

/// @brief Increment the ownership count for a runtime string handle.
/// @details Literal and embedded (SSO) strings track a reference counter inside
///          the handle (literal_refs), while heap-backed strings delegate to
///          @ref rt_heap_retain. Immortal strings skip reference updates entirely.
///          Uses atomic operations for thread-safe reference counting (RACE-003 fix).
/// @param s Runtime string handle.
/// @return The same handle for chaining.
rt_string rt_string_ref(rt_string s) {
    if (!s)
        return NULL;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr) {
        // Atomic increment for thread-safe reference counting
        // Skip immortal literals (SIZE_MAX indicates immortal)
#if RT_COMPILER_MSVC
        size_t old = rt_atomic_load_size(&s->literal_refs, __ATOMIC_RELAXED);
        if (old < SIZE_MAX)
            rt_atomic_fetch_add_size(&s->literal_refs, 1, __ATOMIC_RELAXED);
#else
        size_t old = __atomic_load_n(&s->literal_refs, __ATOMIC_RELAXED);
        if (old < SIZE_MAX)
            __atomic_fetch_add(&s->literal_refs, 1, __ATOMIC_RELAXED);
#endif
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
///          Uses atomic operations for thread-safe reference counting (RACE-003 fix).
/// @param s Runtime string handle to release.
void rt_string_unref(rt_string s) {
    if (!s)
        return;
    rt_heap_hdr_t *hdr = rt_string_header(s);
    if (!hdr) {
        // Atomic decrement for thread-safe reference counting
        // Skip immortal literals (SIZE_MAX indicates immortal) and already-zero refs
#if RT_COMPILER_MSVC
        size_t old = rt_atomic_load_size(&s->literal_refs, __ATOMIC_RELAXED);
        if (old == 0 || old >= SIZE_MAX)
            return;
        // Use fetch_sub which returns old value; if old was 1, we decremented to 0
        size_t prev = rt_atomic_fetch_sub_size(&s->literal_refs, 1, __ATOMIC_RELEASE);
        if (prev == 1) {
            // We held the last reference - ensure all writes are visible before free
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            free(s);
        }
#else
        size_t old = __atomic_load_n(&s->literal_refs, __ATOMIC_RELAXED);
        if (old == 0 || old >= SIZE_MAX)
            return;
        // Use fetch_sub which returns old value; if old was 1, we decremented to 0
        size_t prev = __atomic_fetch_sub(&s->literal_refs, 1, __ATOMIC_RELEASE);
        if (prev == 1) {
            // We held the last reference - ensure all writes are visible before free
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            free(s);
        }
#endif
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
void rt_str_release_maybe(rt_string s) {
    rt_string_unref(s);
}

/// @brief Convenience wrapper that retains a possibly-null string handle.
/// @details Provides parity with `rt_str_release_maybe` and ignores null
///          handles while preserving the return value from @ref rt_string_ref.
/// @param s Runtime string handle.
void rt_str_retain_maybe(rt_string s) {
    (void)rt_string_ref(s);
}

/// @brief Obtain the shared empty string handle.
/// @details Calls @ref rt_empty_string to lazily construct and cache the
///          immortal empty string instance.
/// @return Runtime string handle representing "".
rt_string rt_str_empty(void) {
    return rt_empty_string();
}

/// @brief Return the BASIC-visible length of a string.
/// @details Delegates to the byte-count helper and exposes the value as a signed
///          64-bit integer to match the runtime ABI.
/// @param s Runtime string handle.
/// @return Length in characters (bytes).
int64_t rt_str_len(rt_string s) {
    size_t len = rt_string_len_bytes(s);
    if (len > (size_t)INT64_MAX)
        return INT64_MAX;
    return (int64_t)len;
}

/// @brief Return 1 when the runtime string is empty; 0 otherwise.
/// @details Null handles are treated as empty to match rt_str_len semantics.
/// @param s Runtime string handle.
/// @return 1 if empty; 0 otherwise.
int64_t rt_str_is_empty(rt_string s) {
    return rt_str_len(s) == 0 ? 1 : 0;
}

/// @brief Identity constructor from an existing runtime string handle.
/// @details Used as a thin shim for Viper.Strings.FromStr; returns the input
///          handle unchanged. Callers manage ownership according to IL/VM rules.
/// @param s Runtime string handle.
/// @return The same handle.
rt_string rt_str_clone(rt_string s) {
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
rt_string rt_str_concat(rt_string a, rt_string b) {
    size_t len_a = rt_string_len_bytes(a);
    size_t len_b = rt_string_len_bytes(b);
    if (len_a > SIZE_MAX - len_b) {
        rt_trap("rt_str_concat: length overflow");
        return NULL;
    }
    size_t total = len_a + len_b;
    if (total == SIZE_MAX) {
        rt_trap("rt_str_concat: length overflow");
        return NULL;
    }

    // Optimization: append in-place when `a` is uniquely owned with enough capacity
    if (rt_string_can_append_inplace(a, total + 1)) {
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
rt_string rt_str_substr(rt_string s, int64_t start, int64_t len) {
    if (!s)
        rt_trap("rt_str_substr: null");
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
///          otherwise delegates to @ref rt_str_substr.
/// @param s Source string handle.
/// @param n Number of characters to take from the left.
/// @return Resulting string.
rt_string rt_str_left(rt_string s, int64_t n) {
    if (!s)
        rt_trap("LEFT$: null string");
    if (n < 0) {
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
    return rt_str_substr(s, 0, n);
}

/// @brief Implement BASIC's `RIGHT$` intrinsic.
/// @details Mirrors @ref rt_str_left but slices from the end of the string.  Rejects
///          negative lengths with a descriptive trap message.
/// @param s Source string handle.
/// @param n Number of characters to take from the right.
/// @return Resulting string.
rt_string rt_str_right(rt_string s, int64_t n) {
    if (!s)
        rt_trap("RIGHT$: null string");
    if (n < 0) {
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
    return rt_str_substr(s, (int64_t)start, n);
}

// Forward declare UTF-8 helpers used by Mid functions
// utf8_char_to_byte_offset declared in rt_string_internal.h, defined in rt_string_advanced.c

/// @brief Implement BASIC's two-argument `MID$` overload.
/// @details Interprets @p start as one-based codepoint position.  Returns the
///          original string when @p start == 1, and otherwise slices from the
///          specified codepoint position to the end.  Negative or zero starts
///          trigger traps with detailed messages.
/// @param s Source string handle.
/// @param start One-based codepoint position.
/// @return Resulting substring.
rt_string rt_str_mid(rt_string s, int64_t start) {
    if (!s)
        rt_trap("MID$: null string");
    if (start < 1) {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 1 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t byte_len = rt_string_len_bytes(s);
    if (start == 1)
        return rt_string_ref(s);
    const char *data = s->data;
    size_t byte_off = utf8_char_to_byte_offset(data, byte_len, start);
    if (byte_off >= byte_len)
        return rt_empty_string();
    size_t n = byte_len - byte_off;
    return rt_str_substr(s, (int64_t)byte_off, (int64_t)n);
}

/// @brief Implement BASIC's three-argument `MID$` overload.
/// @details Applies the same one-based semantics as @ref rt_str_mid while
///          respecting the requested length.  Negative lengths trigger traps and
///          slices that extend beyond the source string are clamped.
/// @param s Source string handle.
/// @param start One-based starting position.
/// @param len Requested substring length.
/// @return Resulting substring.
rt_string rt_str_mid_len(rt_string s, int64_t start, int64_t len) {
    if (!s)
        rt_trap("MID$: null string");
    if (start < 1) {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(start, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: start must be >= 1 (got %s)", numbuf);
        rt_trap(buf);
    }
    if (len < 0) {
        char buf[64];
        char numbuf[32];
        rt_i64_to_cstr(len, numbuf, sizeof(numbuf));
        snprintf(buf, sizeof(buf), "MID$: len must be >= 0 (got %s)", numbuf);
        rt_trap(buf);
    }
    size_t byte_len = rt_string_len_bytes(s);
    if (len == 0)
        return rt_empty_string();
    const char *data = s->data;
    size_t byte_start = utf8_char_to_byte_offset(data, byte_len, start);
    if (byte_start >= byte_len)
        return rt_empty_string();
    // Find the byte offset of (start + len) to compute the byte length
    size_t byte_end = utf8_char_to_byte_offset(data, byte_len, start + len);
    size_t byte_count = byte_end - byte_start;
    if (byte_start == 0 && byte_end >= byte_len)
        return rt_string_ref(s);
    return rt_str_substr(s, (int64_t)byte_start, (int64_t)byte_count);
}

/// @brief Search for a substring using zero-based indexing.
/// @details Implements the shared search logic for the INSTR family.  Handles
///          null operands, clamps the starting position, and returns the
///          one-based index mandated by BASIC (or zero when not found).
/// @param hay Haystack string to scan.
/// @param start Zero-based starting offset.
/// @param needle Needle string to locate.
/// @return One-based index of the first match, or zero when absent.
static int64_t rt_find(rt_string hay, int64_t start, rt_string needle) {
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
int64_t rt_str_index_of(rt_string hay, rt_string needle) {
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
int64_t rt_str_instr3(int64_t start, rt_string hay, rt_string needle) {
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

int64_t rt_str_index_of_from(rt_string hay, int64_t start, rt_string needle) {
    return rt_str_instr3(start, hay, needle);
}

static int is_trim_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/// @brief Trim leading spaces and tabs from a string.
/// @details Walks the leading whitespace and delegates to @ref rt_str_substr to
///          materialise the trimmed view.
/// @param s Source string.
/// @return Trimmed string handle.
rt_string rt_str_ltrim(rt_string s) {
    if (!s)
        rt_trap("rt_str_ltrim: null");
    size_t slen = rt_string_len_bytes(s);
    size_t i = 0;
    while (i < slen && is_trim_ws(s->data[i]))
        ++i;
    return rt_str_substr(s, (int64_t)i, (int64_t)(slen - i));
}

/// @brief Trim trailing spaces and tabs from a string.
/// @details Scans from the end of the string and returns a substring covering
///          the retained prefix.
/// @param s Source string.
/// @return Trimmed string handle.
rt_string rt_str_rtrim(rt_string s) {
    if (!s)
        rt_trap("rt_str_rtrim: null");
    size_t end = rt_string_len_bytes(s);
    while (end > 0 && is_trim_ws(s->data[end - 1]))
        --end;
    return rt_str_substr(s, 0, (int64_t)end);
}

/// @brief Trim both leading and trailing spaces and tabs from a string.
/// @details Calculates the slice indices in-place and delegates to
///          @ref rt_str_substr to allocate the final result.
/// @param s Source string.
/// @return Trimmed string handle.
rt_string rt_str_trim(rt_string s) {
    if (!s)
        rt_trap("rt_str_trim: null");
    size_t slen = rt_string_len_bytes(s);
    size_t start = 0;
    size_t end = slen;
    while (start < end && is_trim_ws(s->data[start]))
        ++start;
    while (end > start && is_trim_ws(s->data[end - 1]))
        --end;
    return rt_str_substr(s, (int64_t)start, (int64_t)(end - start));
}

/// @brief Convert a single byte to uppercase (ASCII + Latin-1 Supplement).
/// @details Handles ASCII a-z and Latin-1 lowercase letters (à-ö, ø-ÿ).
///          Non-letter bytes and other Unicode characters pass through unchanged.
/// @param c Input byte.
/// @return Uppercase equivalent or original byte.
static unsigned char to_upper_latin1(unsigned char c) {
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
rt_string rt_str_ucase(rt_string s) {
    if (!s)
        rt_trap("rt_str_ucase: null");
    size_t len = rt_string_len_bytes(s);
    rt_string r = rt_string_alloc(len, len + 1);
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s->data[i];
        // Skip UTF-8 continuation bytes (10xxxxxx) and multi-byte lead bytes
        if ((c & 0x80) == 0) {
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
static unsigned char to_lower_latin1(unsigned char c) {
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
rt_string rt_str_lcase(rt_string s) {
    if (!s)
        rt_trap("rt_str_lcase: null");
    size_t len = rt_string_len_bytes(s);
    rt_string r = rt_string_alloc(len, len + 1);
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s->data[i];
        // Skip UTF-8 multi-byte sequences
        if ((c & 0x80) == 0) {
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
int8_t rt_str_eq(rt_string a, rt_string b) {
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

int64_t rt_str_lt(rt_string a, rt_string b) {
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

int64_t rt_str_le(rt_string a, rt_string b) {
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

int64_t rt_str_gt(rt_string a, rt_string b) {
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

int64_t rt_str_ge(rt_string a, rt_string b) {
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
