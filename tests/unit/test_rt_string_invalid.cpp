#include "rt.hpp"
#include <cassert>

int main()
{
    rt_string abcde = rt_const_cstr("ABCDE");
    rt_string clamped = rt_mid2(abcde, -1);
    assert(clamped == abcde);
    return 0;
}
