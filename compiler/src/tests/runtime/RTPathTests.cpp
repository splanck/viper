//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTPathTests.cpp
// Purpose: Validate runtime path manipulation functions in rt_path.c.
// Key invariants: Path operations handle both Unix and Windows separators,
//                 normalize removes redundant components, and absolute detection
//                 considers platform conventions.
// Ownership/Lifetime: Uses runtime library; tests return newly allocated strings.
// Links: docs/viperlib.md

#include "rt.hpp"
#include "rt_path.h"

#include <cassert>
#include <cstdio>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Test rt_path_join.
static void test_join()
{
    printf("Testing rt_path_join:\n");

    // Basic join
    rt_string a = rt_const_cstr("/foo");
    rt_string b = rt_const_cstr("bar");
    rt_string r = rt_path_join(a, b);
    test_result("basic join", rt_str_eq(r, rt_const_cstr("/foo/bar")));
    rt_string_unref(r);

    // Empty first component
    a = rt_const_cstr("");
    b = rt_const_cstr("bar");
    r = rt_path_join(a, b);
    test_result("empty first", rt_str_eq(r, rt_const_cstr("bar")));
    rt_string_unref(r);

    // Empty second component
    a = rt_const_cstr("/foo");
    b = rt_const_cstr("");
    r = rt_path_join(a, b);
    test_result("empty second", rt_str_eq(r, rt_const_cstr("/foo")));
    rt_string_unref(r);

    // Second component is absolute (Unix)
    a = rt_const_cstr("/foo");
    b = rt_const_cstr("/bar");
    r = rt_path_join(a, b);
    test_result("second absolute", rt_str_eq(r, rt_const_cstr("/bar")));
    rt_string_unref(r);

    // Trailing separator in first
    a = rt_const_cstr("/foo/");
    b = rt_const_cstr("bar");
    r = rt_path_join(a, b);
    test_result("trailing sep", rt_str_eq(r, rt_const_cstr("/foo/bar")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_dir.
static void test_dir()
{
    printf("Testing rt_path_dir:\n");

    rt_string p = rt_const_cstr("/foo/bar/baz.txt");
    rt_string r = rt_path_dir(p);
    test_result("nested path", rt_str_eq(r, rt_const_cstr("/foo/bar")));
    rt_string_unref(r);

    p = rt_const_cstr("baz.txt");
    r = rt_path_dir(p);
    test_result("no directory", rt_str_eq(r, rt_const_cstr(".")));
    rt_string_unref(r);

    p = rt_const_cstr("/baz.txt");
    r = rt_path_dir(p);
    test_result("root file", rt_str_eq(r, rt_const_cstr("/")));
    rt_string_unref(r);

    p = rt_const_cstr("");
    r = rt_path_dir(p);
    test_result("empty path", rt_str_eq(r, rt_const_cstr("")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_name.
static void test_name()
{
    printf("Testing rt_path_name:\n");

    rt_string p = rt_const_cstr("/foo/bar/baz.txt");
    rt_string r = rt_path_name(p);
    test_result("full path", rt_str_eq(r, rt_const_cstr("baz.txt")));
    rt_string_unref(r);

    p = rt_const_cstr("baz.txt");
    r = rt_path_name(p);
    test_result("filename only", rt_str_eq(r, rt_const_cstr("baz.txt")));
    rt_string_unref(r);

    p = rt_const_cstr("/foo/bar/");
    r = rt_path_name(p);
    test_result("trailing slash", rt_str_eq(r, rt_const_cstr("bar")));
    rt_string_unref(r);

    p = rt_const_cstr("");
    r = rt_path_name(p);
    test_result("empty path", rt_str_eq(r, rt_const_cstr("")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_stem.
static void test_stem()
{
    printf("Testing rt_path_stem:\n");

    rt_string p = rt_const_cstr("/foo/bar/baz.txt");
    rt_string r = rt_path_stem(p);
    test_result("full path", rt_str_eq(r, rt_const_cstr("baz")));
    rt_string_unref(r);

    p = rt_const_cstr("file.tar.gz");
    r = rt_path_stem(p);
    test_result("multiple dots", rt_str_eq(r, rt_const_cstr("file.tar")));
    rt_string_unref(r);

    p = rt_const_cstr(".hidden");
    r = rt_path_stem(p);
    test_result("hidden file", rt_str_eq(r, rt_const_cstr(".hidden")));
    rt_string_unref(r);

    p = rt_const_cstr("noext");
    r = rt_path_stem(p);
    test_result("no extension", rt_str_eq(r, rt_const_cstr("noext")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_ext.
static void test_ext()
{
    printf("Testing rt_path_ext:\n");

    rt_string p = rt_const_cstr("/foo/bar/baz.txt");
    rt_string r = rt_path_ext(p);
    test_result("full path", rt_str_eq(r, rt_const_cstr(".txt")));
    rt_string_unref(r);

    p = rt_const_cstr("file.tar.gz");
    r = rt_path_ext(p);
    test_result("multiple dots", rt_str_eq(r, rt_const_cstr(".gz")));
    rt_string_unref(r);

    p = rt_const_cstr(".hidden");
    r = rt_path_ext(p);
    test_result("hidden file", rt_str_eq(r, rt_const_cstr("")));
    rt_string_unref(r);

    p = rt_const_cstr("noext");
    r = rt_path_ext(p);
    test_result("no extension", rt_str_eq(r, rt_const_cstr("")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_with_ext.
static void test_with_ext()
{
    printf("Testing rt_path_with_ext:\n");

    rt_string p = rt_const_cstr("/foo/bar.txt");
    rt_string e = rt_const_cstr(".md");
    rt_string r = rt_path_with_ext(p, e);
    test_result("replace ext with dot", rt_str_eq(r, rt_const_cstr("/foo/bar.md")));
    rt_string_unref(r);

    e = rt_const_cstr("md");
    r = rt_path_with_ext(p, e);
    test_result("replace ext without dot", rt_str_eq(r, rt_const_cstr("/foo/bar.md")));
    rt_string_unref(r);

    p = rt_const_cstr("/foo/bar");
    e = rt_const_cstr(".txt");
    r = rt_path_with_ext(p, e);
    test_result("add ext to no ext", rt_str_eq(r, rt_const_cstr("/foo/bar.txt")));
    rt_string_unref(r);

    p = rt_const_cstr("/foo/bar.txt");
    e = rt_const_cstr("");
    r = rt_path_with_ext(p, e);
    test_result("remove ext", rt_str_eq(r, rt_const_cstr("/foo/bar")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_is_abs.
static void test_is_abs()
{
    printf("Testing rt_path_is_abs:\n");

    rt_string p = rt_const_cstr("/foo/bar");
    test_result("unix absolute", rt_path_is_abs(p) == 1);

    p = rt_const_cstr("foo/bar");
    test_result("relative", rt_path_is_abs(p) == 0);

    p = rt_const_cstr("");
    test_result("empty", rt_path_is_abs(p) == 0);

    printf("\n");
}

/// @brief Test rt_path_norm.
static void test_norm()
{
    printf("Testing rt_path_norm:\n");

    rt_string p = rt_const_cstr("/foo//bar");
    rt_string r = rt_path_norm(p);
    test_result("double slash", rt_str_eq(r, rt_const_cstr("/foo/bar")));
    rt_string_unref(r);

    p = rt_const_cstr("/foo/./bar");
    r = rt_path_norm(p);
    test_result("dot component", rt_str_eq(r, rt_const_cstr("/foo/bar")));
    rt_string_unref(r);

    p = rt_const_cstr("/foo/bar/../baz");
    r = rt_path_norm(p);
    test_result("dotdot component", rt_str_eq(r, rt_const_cstr("/foo/baz")));
    rt_string_unref(r);

    p = rt_const_cstr("foo/../bar");
    r = rt_path_norm(p);
    test_result("relative dotdot", rt_str_eq(r, rt_const_cstr("bar")));
    rt_string_unref(r);

    p = rt_const_cstr("../foo");
    r = rt_path_norm(p);
    test_result("leading dotdot", rt_str_eq(r, rt_const_cstr("../foo")));
    rt_string_unref(r);

    p = rt_const_cstr("");
    r = rt_path_norm(p);
    test_result("empty path", rt_str_eq(r, rt_const_cstr(".")));
    rt_string_unref(r);

    p = rt_const_cstr("/");
    r = rt_path_norm(p);
    test_result("root only", rt_str_eq(r, rt_const_cstr("/")));
    rt_string_unref(r);

    printf("\n");
}

/// @brief Test rt_path_sep.
static void test_sep()
{
    printf("Testing rt_path_sep:\n");

    rt_string r = rt_path_sep();
#ifdef _WIN32
    test_result("platform sep", rt_str_eq(r, rt_const_cstr("\\")));
#else
    test_result("platform sep", rt_str_eq(r, rt_const_cstr("/")));
#endif
    rt_string_unref(r);

    printf("\n");
}

/// @brief Entry point for path tests.
int main()
{
    printf("=== RT Path Tests ===\n\n");

    test_join();
    test_dir();
    test_name();
    test_stem();
    test_ext();
    test_with_ext();
    test_is_abs();
    test_norm();
    test_sep();

    printf("All path tests passed!\n");
    return 0;
}
