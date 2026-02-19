//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTStringInternTests.cpp
// Purpose: Correctness tests for the string interning table (P2-3.8).
//
// Key properties verified:
//   - Two strings with equal content → same canonical pointer after interning
//   - Two strings with different content → different pointers
//   - Interning the same string twice returns the same pointer (idempotent)
//   - The returned pointer is a retained reference (safe to unref)
//   - Pointer equality correctly identifies equal strings (rt_string_interned_eq)
//   - Table grows correctly under high load (many unique strings)
//   - rt_string_intern_drain() resets state; intern works correctly after drain
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_string.h"
#include "rt_string_intern.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Helpers
// ============================================================================

static rt_string make_str(const char *cstr)
{
    return rt_string_from_bytes(cstr, strlen(cstr));
}

// ============================================================================
// Same content → same pointer
// ============================================================================

static void test_same_content_same_pointer(void)
{
    rt_string_intern_drain(); // clean slate

    rt_string s1 = make_str("hello");
    rt_string s2 = make_str("hello"); // distinct object, same bytes
    assert(s1 != s2 && "pre-condition: distinct allocations");

    rt_string i1 = rt_string_intern(s1);
    rt_string i2 = rt_string_intern(s2);

    assert(i1 == i2 && "equal content must yield the same canonical pointer");
    assert(rt_string_interned_eq(i1, i2));

    rt_string_unref(i1);
    rt_string_unref(i2);
    rt_string_unref(s1);
    rt_string_unref(s2);

    rt_string_intern_drain();
    printf("test_same_content_same_pointer: PASSED\n");
}

// ============================================================================
// Different content → different pointers
// ============================================================================

static void test_different_content_different_pointer(void)
{
    rt_string_intern_drain();

    rt_string s1 = make_str("foo");
    rt_string s2 = make_str("bar");

    rt_string i1 = rt_string_intern(s1);
    rt_string i2 = rt_string_intern(s2);

    assert(i1 != i2 && "different content must yield distinct canonical pointers");
    assert(!rt_string_interned_eq(i1, i2));

    rt_string_unref(i1);
    rt_string_unref(i2);
    rt_string_unref(s1);
    rt_string_unref(s2);

    rt_string_intern_drain();
    printf("test_different_content_different_pointer: PASSED\n");
}

// ============================================================================
// Interning the same string twice is idempotent
// ============================================================================

static void test_intern_idempotent(void)
{
    rt_string_intern_drain();

    rt_string s = make_str("viper");

    rt_string i1 = rt_string_intern(s);
    rt_string i2 = rt_string_intern(s);

    assert(i1 == i2 && "interning the same object twice must return the same pointer");

    rt_string_unref(i1);
    rt_string_unref(i2);
    rt_string_unref(s);

    rt_string_intern_drain();
    printf("test_intern_idempotent: PASSED\n");
}

// ============================================================================
// Returned pointer is a valid retained reference
// ============================================================================

static void test_returned_pointer_is_retained(void)
{
    rt_string_intern_drain();

    rt_string s = make_str("retained");
    rt_string interned = rt_string_intern(s);
    rt_string_unref(s); // drop original; interned must still be valid

    // Interned string must still be readable.
    assert(rt_str_len(interned) == (int64_t)strlen("retained"));
    rt_string_unref(interned);

    rt_string_intern_drain();
    printf("test_returned_pointer_is_retained: PASSED\n");
}

// ============================================================================
// Empty string interns correctly
// ============================================================================

static void test_intern_empty_string(void)
{
    rt_string_intern_drain();

    rt_string s1 = make_str("");
    rt_string s2 = make_str("");

    rt_string i1 = rt_string_intern(s1);
    rt_string i2 = rt_string_intern(s2);

    assert(i1 == i2 && "two empty strings must intern to the same pointer");

    rt_string_unref(i1);
    rt_string_unref(i2);
    rt_string_unref(s1);
    rt_string_unref(s2);

    rt_string_intern_drain();
    printf("test_intern_empty_string: PASSED\n");
}

// ============================================================================
// Table growth: intern many unique strings to force rehashing
// ============================================================================

static void test_intern_many_strings(void)
{
    rt_string_intern_drain();

    const int N = 512; // more than INTERN_INIT_CAP (256) to force growth
    char buf[32];
    rt_string interned[N];

    for (int i = 0; i < N; i++)
    {
        snprintf(buf, sizeof(buf), "string_%d", i);
        rt_string s = make_str(buf);
        interned[i] = rt_string_intern(s);
        rt_string_unref(s);
    }

    // Re-intern each string and verify it matches the canonical copy.
    for (int i = 0; i < N; i++)
    {
        snprintf(buf, sizeof(buf), "string_%d", i);
        rt_string s = make_str(buf);
        rt_string again = rt_string_intern(s);
        assert(again == interned[i] && "re-interning must return the same canonical pointer");
        rt_string_unref(again);
        rt_string_unref(s);
    }

    for (int i = 0; i < N; i++)
        rt_string_unref(interned[i]);

    rt_string_intern_drain();
    printf("test_intern_many_strings: PASSED\n");
}

// ============================================================================
// Drain and re-intern
// ============================================================================

static void test_drain_and_reintern(void)
{
    rt_string s = make_str("after_drain");

    rt_string i1 = rt_string_intern(s);
    rt_string_unref(i1);

    rt_string_intern_drain(); // clear table

    rt_string i2 = rt_string_intern(s); // re-intern into fresh table
    assert(i2 != nullptr);
    assert(rt_str_len(i2) == (int64_t)strlen("after_drain"));

    rt_string_unref(i2);
    rt_string_unref(s);

    rt_string_intern_drain();
    printf("test_drain_and_reintern: PASSED\n");
}

// ============================================================================
// Entry point
// ============================================================================

int main(void)
{
    printf("=== rt_string_intern Tests ===\n\n");

    test_same_content_same_pointer();
    test_different_content_different_pointer();
    test_intern_idempotent();
    test_returned_pointer_is_retained();
    test_intern_empty_string();
    test_intern_many_strings();
    test_drain_and_reintern();

    printf("\nAll rt_string_intern tests passed!\n");
    return 0;
}
