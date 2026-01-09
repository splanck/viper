//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTStringWrapAllocFailTests.cpp
// Purpose: Ensure string allocation helpers guard against rt_alloc failures.
// Key invariants: Wrappers must not dereference NULL handles and should trap.
// Ownership/Lifetime: Overrides vm_trap and rt_alloc via hook to simulate failure.
// Links: docs/codemap/runtime-library-c.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"

#include <cassert>
#include <string>

namespace
{
int g_trap_count = 0;
std::string g_last_trap;
bool g_fail_next_alloc = false;

extern "C" void vm_trap(const char *msg)
{
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

void *fail_rt_alloc_once(int64_t bytes, void *(*next)(int64_t))
{
    if (g_fail_next_alloc)
    {
        g_fail_next_alloc = false;
        (void)bytes;
        return NULL;
    }
    return next ? next(bytes) : NULL;
}
} // namespace

int main()
{
    g_trap_count = 0;
    g_last_trap.clear();
    g_fail_next_alloc = true;
    rt_set_alloc_hook(fail_rt_alloc_once);

    // Use a string longer than RT_SSO_MAX_LEN (32) to test heap allocation path
    static const char *long_str =
        "this_is_a_very_long_string_that_exceeds_the_sso_limit";
    rt_string result = rt_string_from_bytes(long_str, 53);
    assert(result == NULL);
    assert(g_trap_count == 1);
    assert(g_last_trap == "rt_string_wrap: alloc");

    rt_set_alloc_hook(NULL);
    return 0;
}
