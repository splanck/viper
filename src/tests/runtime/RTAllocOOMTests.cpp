//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTAllocOOMTests.cpp
// Purpose: Verify that runtime allocation failures produce clean trap messages
//          rather than crashes. Tests the alloc hook mechanism and the behavior
//          of callers (string, list, etc.) when rt_alloc returns NULL.
// Key invariants: No runtime allocation failure should cause a segfault.
// Ownership/Lifetime: Uses rt_set_alloc_hook to simulate OOM without
//                     exhausting system memory.
// Links: src/runtime/core/rt_memory.c, src/runtime/core/rt_string.c
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// ── vm_trap override ────────────────────────────────────────────────────────
namespace
{
int g_trap_count = 0;
std::string g_last_trap;
bool g_fail_next_alloc = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

/// Fail the next allocation, then delegate to real allocator.
static void *fail_once_hook(int64_t bytes, void *(*next)(int64_t))
{
    if (g_fail_next_alloc)
    {
        g_fail_next_alloc = false;
        return NULL;
    }
    return next ? next(bytes) : NULL;
}

// ── Tests ───────────────────────────────────────────────────────────────────

/// rt_alloc with negative size → trap "negative allocation"
/// (goes through rt_alloc_impl since no hook is installed)
static void test_alloc_negative_traps()
{
    g_trap_count = 0;
    g_last_trap.clear();
    rt_set_alloc_hook(NULL);

    void *p = rt_alloc(-1);
    assert(p == NULL);
    assert(g_trap_count == 1);
    assert(g_last_trap == "negative allocation");
}

/// rt_alloc with oversized request → trap "allocation too large"
/// (goes through rt_alloc_impl since no hook is installed)
static void test_alloc_too_large_traps()
{
    g_trap_count = 0;
    g_last_trap.clear();
    rt_set_alloc_hook(NULL);

    // Request more than SIZE_MAX bytes (on 64-bit this is still huge,
    // but the check is (uint64_t)bytes > SIZE_MAX)
    // Since SIZE_MAX == UINT64_MAX on 64-bit, this path is only reachable
    // on 32-bit. Skip this test on 64-bit.
    if (sizeof(size_t) < 8)
    {
        void *p = rt_alloc((int64_t)((uint64_t)SIZE_MAX + 1));
        assert(p == NULL);
        assert(g_trap_count == 1);
        assert(g_last_trap == "allocation too large");
    }
}

/// String allocation with OOM hook → trap from string layer
static void test_string_alloc_oom()
{
    g_trap_count = 0;
    g_last_trap.clear();
    g_fail_next_alloc = true;
    rt_set_alloc_hook(fail_once_hook);

    // String longer than SSO threshold forces heap allocation via rt_alloc
    static const char *long_str = "this_is_a_string_that_definitely_exceeds_the_sso_limit_of_32_bytes";
    rt_string s = rt_string_from_bytes(long_str, strlen(long_str));
    assert(s == NULL);
    assert(g_trap_count == 1);
    assert(g_last_trap.find("alloc") != std::string::npos);

    rt_set_alloc_hook(NULL);
}

int main()
{
    test_alloc_negative_traps();
    printf("  PASS: rt_alloc(-1) → trap 'negative allocation'\n");

    test_alloc_too_large_traps();
    printf("  PASS: rt_alloc(too_large) → trap or skip on 64-bit\n");

    test_string_alloc_oom();
    printf("  PASS: rt_string_from_bytes OOM → clean trap\n");

    printf("All OOM tests passed.\n");
    return 0;
}
