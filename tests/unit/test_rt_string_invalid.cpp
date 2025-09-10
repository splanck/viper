#include "rt.hpp"

int main()
{
    rt_string bad = rt_const_cstr("12x");
    rt_to_int(bad);
    return 0;
}
