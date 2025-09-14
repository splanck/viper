// File: tests/runtime/RTAbsI64OverflowTests.cpp
// Purpose: Verify rt_abs_i64 traps on overflow input.
// Key invariants: Overflowing inputs trigger runtime trap.
// Ownership: Uses runtime library; stubs vm_trap to capture message.
// Links: docs/runtime-abi.md
#include "rt_math.h"
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
