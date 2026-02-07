//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTValStrTests.cpp
// Purpose: Validate VAL and STR$ runtime conversions.
// Key invariants: Parsing stops at non-numeric; round-trip within tolerance.
// Ownership/Lifetime: Uses runtime library.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "viper/runtime/rt.h"
#include <cassert>
#include <cmath>
#include <string>

namespace
{
std::string toStd(rt_string s)
{
    // Use the public API which handles both SSO and heap-backed strings
    return std::string(rt_string_cstr(s), (size_t)rt_str_len(s));
}
} // namespace

int main()
{
    rt_string spaced = rt_const_cstr("  -12.5E+1x");
    assert(rt_val(spaced) == -125.0);
    assert(rt_val(rt_const_cstr("abc")) == 0.0);
    assert(rt_val(rt_const_cstr("")) == 0.0);

    bool ok = true;
    (void)rt_val_to_double("1e400", &ok);
    assert(!ok);

    ok = true;
    double parsed = rt_val_to_double(" 42 ", &ok);
    assert(ok && parsed == 42.0);

    const double vals[] = {0.0, 1.25, -2.5, 123.456, -3.5, 1.0e20};
    for (double v : vals)
    {
        rt_string t = rt_str(v);
        double r = rt_val(t);
        assert(r == v);
    }

    rt_string s42 = rt_str(42.0);
    assert(toStd(s42) == "42");
    rt_string sn = rt_str(-3.5);
    assert(toStd(sn) == "-3.5");
    return 0;
}
