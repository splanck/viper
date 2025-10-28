// File: tests/runtime/RTStringSizeClampTests.cpp
// Purpose: Ensure substring helpers clamp lengths that exceed size_t.
// Key invariants: Requests beyond SIZE_MAX return the full available tail.
// Ownership: Uses runtime library.
// Links: docs/runtime-vm.md#runtime-abi
#include "rt.hpp"

#include <assert.h>
#include <stdint.h>

#if SIZE_MAX < INT64_MAX

int main()
{
    rt_string sample = rt_const_cstr("ABCDE");
    int64_t huge = (int64_t)SIZE_MAX + 42;

    rt_string full = rt_substr(sample, 0, huge);
    assert(rt_str_eq(full, sample));

    rt_string tail = rt_substr(sample, 2, huge);
    assert(rt_str_eq(tail, rt_const_cstr("CDE")));

    rt_string left = rt_left(sample, huge);
    assert(rt_str_eq(left, sample));

    rt_string right = rt_right(sample, huge);
    assert(rt_str_eq(right, sample));

    rt_string mid = rt_mid3(sample, 2, huge);
    assert(rt_str_eq(mid, rt_const_cstr("BCDE")));

    return 0;
}

#else

int main()
{
    return 0;
}

#endif

