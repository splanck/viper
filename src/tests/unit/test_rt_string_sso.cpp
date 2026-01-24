//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_string_sso.cpp
// Purpose: Test embedded (SSO) string allocation.
// Key invariants: Small strings use embedded storage, larger strings use heap.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/vm-performance.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}

int main()
{
    printf("Test 1: Create small string via rt_string_from_bytes\n");
    rt_string small = rt_string_from_bytes("hello", 5);
    assert(small != nullptr);
    printf("  small created, magic=%llx\n", (unsigned long long)small->magic);
    printf("  small->heap=%p, RT_SSO_SENTINEL=%p\n", (void *)small->heap, (void *)RT_SSO_SENTINEL);
    printf("  small->data=%p\n", (void *)small->data);
    printf("  small->literal_len=%zu\n", small->literal_len);

    // Check if it's embedded
    bool is_embedded = (small->heap == RT_SSO_SENTINEL);
    printf("  is_embedded=%d\n", is_embedded);

    printf("Test 2: Check length\n");
    int64_t len = rt_len(small);
    printf("  len=%lld\n", (long long)len);
    assert(len == 5);

    printf("Test 3: Check content via rt_string_cstr\n");
    const char *cstr = rt_string_cstr(small);
    printf("  cstr=%s\n", cstr);
    assert(strcmp(cstr, "hello") == 0);

    printf("Test 4: Create larger string (non-SSO)\n");
    const char *long_str = "This is a string longer than 32 characters for testing";
    rt_string large = rt_string_from_bytes(long_str, strlen(long_str));
    assert(large != nullptr);
    printf("  large->heap=%p\n", (void *)large->heap);
    bool large_embedded = (large->heap == RT_SSO_SENTINEL);
    printf("  large_embedded=%d (should be 0)\n", large_embedded);
    assert(!large_embedded); // Should use heap, not embedded

    printf("Test 5: Concat two small strings\n");
    rt_string a = rt_string_from_bytes("foo", 3);
    rt_string b = rt_string_from_bytes("bar", 3);
    rt_string ab = rt_concat(a, b);
    printf("  ab->heap=%p\n", (void *)ab->heap);
    len = rt_len(ab);
    printf("  len=%lld\n", (long long)len);
    assert(len == 6);
    cstr = rt_string_cstr(ab);
    printf("  cstr=%s\n", cstr);
    assert(strcmp(cstr, "foobar") == 0);

    printf("Test 6: Release strings\n");
    rt_string_unref(small);
    rt_string_unref(large);
    rt_string_unref(ab);

    printf("All SSO tests passed!\n");
    return 0;
}
