// File: tests/unit/test_rt_int_to_str_big.cpp
// Purpose: Ensure rt_int_to_str handles integers with more than 31 digits.
// Key invariants: Dynamic buffer allocation is used when snprintf reports
//                 output longer than the initial stack buffer.
// Ownership: Uses runtime library.
// Links: docs/class-catalog.md
#include "rt_internal.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    long long v = va_arg(ap, long long);
    va_end(ap);
    char buf[128];
    int n = std::sprintf(buf, "%040lld", v); // 40 digits
    if (str && size > 0)
    {
        size_t copy = (n < (int)size) ? (size_t)n : size - 1;
        std::memcpy(str, buf, copy);
        str[copy] = '\0';
    }
    return n;
}

int main()
{
    rt_string s = rt_int_to_str(1234567890LL);
    std::string expected(40, '0');
    expected.replace(expected.size() - 10, 10, "1234567890");
    assert(s && std::string(s->data, (size_t)s->size) == expected);
    return 0;
}
