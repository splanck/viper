//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_conv.cpp
// Purpose: Verify numeric to string runtime conversions.
// Key invariants: Returned strings match decimal formatting used by PRINT.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include <cassert>
#include <string>

int main()
{
    rt_string si = rt_int_to_str(-42);
    assert(si && std::string(rt_string_cstr(si), (size_t)rt_len(si)) == "-42");
    rt_string sf = rt_f64_to_str(3.5);
    std::string s(rt_string_cstr(sf), (size_t)rt_len(sf));
    assert(s.find("3.5") != std::string::npos);
    return 0;
}
