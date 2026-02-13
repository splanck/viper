//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTGlobTests.cpp
// Purpose: Validate glob pattern matching functions.
//
//===----------------------------------------------------------------------===//

#include "rt_glob.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Pattern Matching Tests
//=============================================================================

static void test_glob_match()
{
    printf("Testing Glob.Match:\n");

    // Test 1: Literal match
    {
        rt_string pattern = rt_const_cstr("hello.txt");
        rt_string path = rt_const_cstr("hello.txt");
        test_result("Literal match", rt_glob_match(path, pattern) == 1);
    }

    // Test 2: Literal non-match
    {
        rt_string pattern = rt_const_cstr("hello.txt");
        rt_string path = rt_const_cstr("world.txt");
        test_result("Literal non-match", rt_glob_match(path, pattern) == 0);
    }

    // Test 3: * wildcard
    {
        rt_string pattern = rt_const_cstr("*.txt");
        rt_string path = rt_const_cstr("hello.txt");
        test_result("* matches prefix", rt_glob_match(path, pattern) == 1);
    }

    // Test 4: * doesn't match /
    {
        rt_string pattern = rt_const_cstr("*.txt");
        rt_string path = rt_const_cstr("dir/hello.txt");
        test_result("* doesn't match /", rt_glob_match(path, pattern) == 0);
    }

    // Test 5: ** matches /
    {
        rt_string pattern = rt_const_cstr("**/*.txt");
        rt_string path = rt_const_cstr("dir/hello.txt");
        test_result("**/ matches directories", rt_glob_match(path, pattern) == 1);
    }

    // Test 6: ** matches multiple directories
    {
        rt_string pattern = rt_const_cstr("**/*.txt");
        rt_string path = rt_const_cstr("a/b/c/hello.txt");
        test_result("** matches deep paths", rt_glob_match(path, pattern) == 1);
    }

    // Test 7: ? wildcard
    {
        rt_string pattern = rt_const_cstr("file?.txt");
        rt_string path = rt_const_cstr("file1.txt");
        test_result("? matches single char", rt_glob_match(path, pattern) == 1);
    }

    // Test 8: ? doesn't match multiple chars
    {
        rt_string pattern = rt_const_cstr("file?.txt");
        rt_string path = rt_const_cstr("file12.txt");
        test_result("? doesn't match multiple", rt_glob_match(path, pattern) == 0);
    }

    // Test 9: ? doesn't match /
    {
        rt_string pattern = rt_const_cstr("a?b");
        rt_string path = rt_const_cstr("a/b");
        test_result("? doesn't match /", rt_glob_match(path, pattern) == 0);
    }

    // Test 10: Complex pattern
    {
        rt_string pattern = rt_const_cstr("src/**/*.c");
        rt_string path = rt_const_cstr("src/runtime/main.c");
        test_result("Complex pattern", rt_glob_match(path, pattern) == 1);
    }

    // Test 11: * at end
    {
        rt_string pattern = rt_const_cstr("test*");
        rt_string path = rt_const_cstr("testing");
        test_result("* at end matches", rt_glob_match(path, pattern) == 1);
    }

    // Test 12: Multiple *
    {
        rt_string pattern = rt_const_cstr("*test*");
        rt_string path = rt_const_cstr("my_test_file");
        test_result("Multiple * matches", rt_glob_match(path, pattern) == 1);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Glob Tests ===\n\n");

    test_glob_match();

    printf("All Glob tests passed!\n");
    return 0;
}
