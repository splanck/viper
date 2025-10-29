// File: tests/unit/test_rt_int_to_str_big.cpp
// Purpose: Ensure rt_int_to_str handles integers with more than 31 digits.
// Key invariants: Dynamic buffer allocation is used when snprintf reports
//                 output longer than the initial stack buffer.
// Ownership: Uses runtime library.
// Links: docs/codemap.md
#include "rt_internal.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" int snprintf(char *str, size_t size, const char *fmt, ...)
{
    static bool reentered = false;
    va_list ap;
    va_start(ap, fmt);
    long long v = va_arg(ap, long long);
    va_end(ap);

    if (reentered)
    {
        const std::string digits = std::to_string(v);
        std::string formatted(40, '0');
        if (v < 0)
        {
            formatted[0] = '-';
            const size_t digit_count = digits.size() - 1;
            std::memcpy(&formatted[formatted.size() - digit_count], digits.data() + 1, digit_count);
        }
        else
        {
            const size_t digit_count = digits.size();
            std::memcpy(&formatted[formatted.size() - digit_count], digits.data(), digit_count);
        }

        const int n = static_cast<int>(formatted.size());
        if (str && size > 0)
        {
            const size_t copy = (n < static_cast<int>(size)) ? static_cast<size_t>(n) : size - 1;
            std::memcpy(str, formatted.data(), copy);
            str[copy] = '\0';
        }
        return n;
    }

    reentered = true;
    char buf[128];
    int n = std::snprintf(buf, sizeof(buf), "%040lld", v); // 40 digits
    assert(n > 0 && static_cast<size_t>(n) < sizeof(buf));
    reentered = false;
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
    assert(s && std::string(s->data, rt_heap_len(s->data)) == expected);
    return 0;
}
