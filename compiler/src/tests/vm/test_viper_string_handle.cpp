// SPDX-License-Identifier: GPL-3.0-only
// File: tests/vm/test_viper_string_handle.cpp
// Purpose: Validate ViperStringHandle RAII retains/releases correctly.

#include <atomic>
#include <cstdio>

extern "C"
{
#include "viper/runtime/rt.h"
}

// Capture real runtime functions before interposing with macros.
static void (*real_rt_str_retain_maybe)(rt_string) = &rt_str_retain_maybe;
static void (*real_rt_str_release_maybe)(rt_string) = &rt_str_release_maybe;

// Global counters for retain/release calls in this TU.
static std::atomic<int> g_retain_calls{0};
static std::atomic<int> g_release_calls{0};

static void reset_counts()
{
    g_retain_calls.store(0, std::memory_order_relaxed);
    g_release_calls.store(0, std::memory_order_relaxed);
}

// Forward-declare shims that we will map the names to for this test TU.
extern "C" void test_shim_rt_str_retain_maybe(rt_string s);
extern "C" void test_shim_rt_str_release_maybe(rt_string s);

// Interpose the runtime retain/release with shims that count calls
// within this translation unit, then delegate to the real runtime.
#define rt_str_retain_maybe test_shim_rt_str_retain_maybe
#define rt_str_release_maybe test_shim_rt_str_release_maybe

#include "vm/ViperStringHandle.hpp"

extern "C" void test_shim_rt_str_retain_maybe(rt_string s)
{
    g_retain_calls.fetch_add(1, std::memory_order_relaxed);
    real_rt_str_retain_maybe(s);
}

extern "C" void test_shim_rt_str_release_maybe(rt_string s)
{
    g_release_calls.fetch_add(1, std::memory_order_relaxed);
    real_rt_str_release_maybe(s);
}

// Helper: make a fresh owned string (not immortal) for testing.
static rt_string make_owned(const char *s)
{
    // Use explicit length to avoid treating as immortal literal.
    size_t len = 0;
    while (s[len] != '\0')
        ++len;
    return rt_string_from_bytes(s, len);
}

static int expect_eq(const char *what, long long a, long long b)
{
    if (a != b)
    {
        std::fprintf(stderr, "EXPECT_EQ failed for %s: %lld != %lld\n", what, a, b);
        return 1;
    }
    return 0;
}

int main()
{
    int failures = 0;

    // construct_and_destroy_balances_release
    reset_counts();
    {
        rt_string s = make_owned("hello");
        {
            il::vm::ViperStringHandle h(s);
            failures += expect_eq("retain after construct", g_retain_calls.load(), 0);
            failures += expect_eq("release after construct", g_release_calls.load(), 0);
        }
        failures += expect_eq("retain after destroy", g_retain_calls.load(), 0);
        failures += expect_eq("release after destroy", g_release_calls.load(), 1);
    }

    // copy_construction_increments_and_destroys_release_twice
    reset_counts();
    {
        rt_string s = make_owned("world");
        {
            il::vm::ViperStringHandle h1(s);
            {
                il::vm::ViperStringHandle h2(h1);
                failures += expect_eq("retain after copy", g_retain_calls.load(), 1);
                failures += expect_eq("release after copy", g_release_calls.load(), 0);
            }
            failures += expect_eq("release after inner dtor", g_release_calls.load(), 1);
        }
        failures += expect_eq("retain total after both dtors", g_retain_calls.load(), 1);
        failures += expect_eq("release total after both dtors", g_release_calls.load(), 2);
    }

    // copy_assignment_releases_old_and_retain_new
    reset_counts();
    {
        rt_string s1 = make_owned("a");
        rt_string s2 = make_owned("b");
        {
            il::vm::ViperStringHandle h1(s1);
            il::vm::ViperStringHandle h2(s2);
            h2 = h1;
            failures += expect_eq("retain after copy assign", g_retain_calls.load(), 1);
            failures += expect_eq("release after copy assign", g_release_calls.load(), 1);
        }
        failures += expect_eq("retain total after copy assign dtors", g_retain_calls.load(), 1);
        failures += expect_eq("release total after copy assign dtors", g_release_calls.load(), 3);
    }

    // move_construction_transfers_ownership
    reset_counts();
    {
        rt_string s = make_owned("m");
        {
            il::vm::ViperStringHandle h1(s);
            {
                il::vm::ViperStringHandle h2(std::move(h1));
                failures += expect_eq("retain after move", g_retain_calls.load(), 0);
                failures += expect_eq("release after move", g_release_calls.load(), 0);
            }
            failures += expect_eq("release after moved-to dtor", g_release_calls.load(), 1);
        }
        failures += expect_eq("release total after move dtor", g_release_calls.load(), 1);
    }

    // move_assignment_releases_old_and_transfers
    reset_counts();
    {
        rt_string s1 = make_owned("x");
        rt_string s2 = make_owned("y");
        {
            il::vm::ViperStringHandle h1(s1);
            il::vm::ViperStringHandle h2(s2);
            h2 = std::move(h1);
            failures += expect_eq("retain after move assign", g_retain_calls.load(), 0);
            failures += expect_eq("release after move assign", g_release_calls.load(), 1);
        }
        failures += expect_eq("release total after move assign dtors", g_release_calls.load(), 2);
    }

    return failures ? 1 : 0;
}
