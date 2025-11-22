//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTAbsI64OverflowTests.cpp
// Purpose: Verify rt_abs_i64 traps on overflow input. 
// Key invariants: Overflowing inputs trigger runtime trap.
// Ownership/Lifetime: Uses runtime library; stubs vm_trap to capture message.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"
#include <cassert>
#include <climits>
#include <cstring>

static const char *g_msg = nullptr;

extern "C" void vm_trap(const char *msg)
{
    g_msg = msg;
}

int main()
{
    (void)rt_abs_i64(LLONG_MIN);
    assert(g_msg && std::strcmp(g_msg, "rt_abs_i64: overflow") == 0);
    return 0;
}
