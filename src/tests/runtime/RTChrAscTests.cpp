//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTChrAscTests.cpp
// Purpose: Validate CHR$ and ASC runtime helpers.
// Key invariants: CHR$ validates 0-255 range; ASC returns 0 for empty string.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string c = rt_str_chr(65);
    assert(rt_str_eq(c, rt_const_cstr("A")));

    assert(rt_str_asc(rt_const_cstr("A")) == 65);
    assert(rt_str_asc(rt_const_cstr("")) == 0);

    return 0;
}
