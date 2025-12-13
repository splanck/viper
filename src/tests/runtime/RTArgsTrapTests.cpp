//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArgsTrapTests.cpp
// Purpose: Ensure rt_args_get signals out-of-range via rt_trap (not abort).
// Key invariants: Out-of-range access traps with a stable message.
// Ownership/Lifetime: Overrides vm_trap to observe trap payloads.
// Links: docs/codemap/runtime-library-c.md
//
//===----------------------------------------------------------------------===//

#include "rt_args.h"
#include "rt.hpp"

#include <cassert>
#include <string>

namespace
{
int g_trap_count = 0;
std::string g_last_trap;

extern "C" void vm_trap(const char *msg)
{
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}
} // namespace

int main()
{
    rt_args_clear();

    g_trap_count = 0;
    g_last_trap.clear();

    rt_string out = rt_args_get(0);
    assert(out == NULL);
    assert(g_trap_count == 1);
    assert(g_last_trap == "rt_args_get: index out of range");

    return 0;
}

