//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArgsTests.cpp
// Purpose: Verify runtime argument store helpers (rt_args_*).
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, std::strlen(s));
}

int main() {
    // Start clean
    rt_args_clear();
    assert(rt_args_count() == 0);

    // Pushing NULL treated as empty
    rt_args_push(NULL);
    assert(rt_args_count() == 1);
    rt_string s0 = rt_args_get(0);
    assert(rt_str_len(s0) == 0);
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

    // Environment variables: missing, set, get, and UTF-8 round-trip.
    rt_string missing_name = make_str("VIPER_RT_ARGS_MISSING_FOR_TEST");
    assert(rt_env_has_var(missing_name) == 0);
    rt_string missing_value = rt_env_get_var(missing_name);
    assert(std::strcmp(rt_string_cstr(missing_value), "") == 0);
    rt_string_unref(missing_name);
    rt_string_unref(missing_value);

    rt_string env_name = make_str("VIPER_RT_ARGS_UTF8_VALUE");
    rt_string env_value = make_str("caf\xc3\xa9");
    rt_env_set_var(env_name, env_value);
    assert(rt_env_has_var(env_name) == 1);
    rt_string roundtrip = rt_env_get_var(env_name);
    assert(std::strcmp(rt_string_cstr(roundtrip), "caf\xc3\xa9") == 0);
    rt_string_unref(env_name);
    rt_string_unref(env_value);
    rt_string_unref(roundtrip);

    return 0;
}
