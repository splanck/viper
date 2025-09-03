// File: tests/unit/test_rt_conv.cpp
// Purpose: Verify numeric to string runtime conversions.
// Key invariants: Returned strings match decimal formatting used by PRINT.
// Ownership: Uses runtime library.
// Links: docs/class-catalog.md
#include "rt.hpp"
#include <cassert>
#include <string>

int main()
{
    rt_str si = rt_int_to_str(-42);
    assert(si && std::string(si->data, (size_t)si->size) == "-42");
    rt_str sf = rt_f64_to_str(3.5);
    std::string s(sf->data, (size_t)sf->size);
    assert(s.find("3.5") != std::string::npos);
    return 0;
}
