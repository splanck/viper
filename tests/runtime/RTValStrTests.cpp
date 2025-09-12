// File: tests/runtime/RTValStrTests.cpp
// Purpose: Validate VAL and STR$ runtime conversions.
// Key invariants: Parsing stops at non-numeric; round-trip within tolerance.
// Ownership: Uses runtime library.
// Links: docs/class-catalog.md
#include "rt.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

int main()
{
    rt_string s = rt_const_cstr(" -12.5xyz");
    assert(std::fabs(rt_val(s) + 12.5) < 1e-12);
    assert(rt_val(rt_const_cstr("")) == 0.0);

    rt_string s42 = rt_str(42.0);
    assert(std::string(s42->data, (size_t)s42->size) == "42");
    rt_string sn = rt_str(-3.5);
    assert(std::string(sn->data, (size_t)sn->size) == "-3.5");

    double vals[] = {0.0, 1.25, -2.5, 123.456};
    for (double v : vals)
    {
        rt_string t = rt_str(v);
        double r = rt_val(t);
        assert(std::fabs(r - v) < 1e-9 * std::fmax(1.0, std::fabs(v)));
    }

    // Verify formatting of large long double values doesn't over-read buffers.
    long double big = std::numeric_limits<long double>::max();
    char tmp[64];
    int len = std::snprintf(tmp, sizeof(tmp), "%g", static_cast<double>(big));
    rt_string sbig = rt_str(static_cast<double>(big));
    assert(std::string(sbig->data, (size_t)sbig->size) == std::string(tmp, (size_t)len));
    return 0;
}
