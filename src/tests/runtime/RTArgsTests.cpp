//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArgsTests.cpp
// Purpose: Verify runtime argument store helpers (rt_args_*).
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include "zanna/runtime/rt.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>

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
    rt_string missing_name = make_str("ZANNA_RT_ARGS_MISSING_FOR_TEST");
    assert(rt_env_has_var(missing_name) == 0);
    rt_string missing_value = rt_env_get_var(missing_name);
    assert(std::strcmp(rt_string_cstr(missing_value), "") == 0);
    rt_string_unref(missing_name);
    rt_string_unref(missing_value);

    rt_string env_name = make_str("ZANNA_RT_ARGS_UTF8_VALUE");
    rt_string env_value = make_str("caf\xc3\xa9");
    rt_env_set_var(env_name, env_value);
    assert(rt_env_has_var(env_name) == 1);
    rt_string roundtrip = rt_env_get_var(env_name);
    assert(std::strcmp(rt_string_cstr(roundtrip), "caf\xc3\xa9") == 0);
    rt_string_unref(env_name);
    rt_string_unref(env_value);
    rt_string_unref(roundtrip);

    // VDOC-211: the legacy host-init flag is now an atomic once-init, so many
    // threads reading the argument store concurrently observe a single stable
    // count with no crash, corruption, or spin under contention. (The 0->1->2
    // import is one-shot per process, so this exercises the atomic read/CAS path
    // for regression rather than reproducing the original data race.)
    {
        rt_args_clear();
        rt_args_push(make_str("alpha"));
        rt_args_push(make_str("beta"));
        const int64_t expected_count = rt_args_count();
        assert(expected_count == 2);

        std::atomic<bool> ok{true};
        std::vector<std::thread> workers;
        for (int t = 0; t < 8; ++t) {
            workers.emplace_back([&]() {
                for (int i = 0; i < 2000; ++i) {
                    if (rt_args_count() != expected_count)
                        ok.store(false);
                    rt_string a = rt_args_get(0);
                    if (a)
                        rt_string_unref(a);
                }
            });
        }
        for (auto &w : workers)
            w.join();
        assert(ok.load());
    }

    return 0;
}
