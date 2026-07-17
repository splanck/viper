//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_string_invalid.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"

#include <cassert>
#include <cstring>
#include <setjmp.h>

int main() {
    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        rt_string abcde = rt_const_cstr("ABCDE");
        (void)rt_str_mid(abcde, -1);
        rt_trap_clear_recovery();
        assert(false && "rt_str_mid should trap on invalid start");
    }

    const char *message = rt_trap_get_error();
    assert(rt_trap_get_kind() == RT_TRAP_KIND_DOMAIN_ERROR);
    assert(message != nullptr);
    assert(std::strstr(message, "MID$: start must be >= 1") != nullptr);
    rt_trap_clear_recovery();
    return 0;
}
