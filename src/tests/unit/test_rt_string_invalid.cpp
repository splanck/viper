#include "rt.hpp"

int main()
{
    rt_string abcde = rt_const_cstr("ABCDE");
    rt_mid2(abcde, -1);
    return 0;
}
