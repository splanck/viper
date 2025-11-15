// File: tests/runtime/RTArgsTests.cpp
// Purpose: Verify runtime argument store helpers (rt_args_*).
// Invariants: Store retains pushed strings; getters return retained copies;
//             clear releases stored references; cmdline joins with spaces.

#include "viper/runtime/rt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, std::strlen(s));
}

int main()
{
    // Start clean
    rt_args_clear();
    assert(rt_args_count() == 0);

    // Pushing NULL treated as empty
    rt_args_push(NULL);
    assert(rt_args_count() == 1);
    rt_string s0 = rt_args_get(0);
    assert(rt_len(s0) == 0);
    rt_string_unref(s0);

    // Push two arguments and read them back
    rt_string a = make_str("foo");
    rt_string b = make_str("bar baz");
    rt_args_push(a);
    rt_args_push(b);
    // Caller still owns a/b and should release
    rt_string_unref(a);
    rt_string_unref(b);

    assert(rt_args_count() == 3);
    rt_string s1 = rt_args_get(1);
    rt_string s2 = rt_args_get(2);
    assert(std::strcmp(rt_string_cstr(s1), "foo") == 0);
    assert(std::strcmp(rt_string_cstr(s2), "bar baz") == 0);

    // cmdline joins with spaces and returns new string
    rt_string tail = rt_cmdline();
    assert(std::strcmp(rt_string_cstr(tail),
                       ""
                       " foo bar baz") == 0);

    // Release all retained strings from getters
    rt_string_unref(s1);
    rt_string_unref(s2);
    rt_string_unref(tail);

    // Clearing leaves store empty
    rt_args_clear();
    assert(rt_args_count() == 0);

    return 0;
}
