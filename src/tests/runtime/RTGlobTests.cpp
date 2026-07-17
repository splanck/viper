//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTGlobTests.cpp
// Purpose: Validate glob pattern matching functions.
//
//===----------------------------------------------------------------------===//

#include "rt_glob.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Pattern Matching Tests
//=============================================================================

static void test_glob_match() {
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

    // VDOC-186: a leading ** must not leak separator permission into a later
    // ?, *, or class token. `**/a?b` cannot match `a/b` because the trailing
    // `?` follows ordinary component rules and cannot match the separator.
    {
        rt_string pattern = rt_const_cstr("**/a?b");
        rt_string path = rt_const_cstr("a/b");
        test_result("** does not leak / into later ?", rt_glob_match(path, pattern) == 0);
    }
    {
        rt_string pattern = rt_const_cstr("**/a*b");
        rt_string path = rt_const_cstr("a/b");
        test_result("** does not leak / into later *", rt_glob_match(path, pattern) == 0);
    }
    {
        rt_string pattern = rt_const_cstr("**/a[x/]b");
        rt_string path = rt_const_cstr("a/b");
        test_result("** does not leak / into later class", rt_glob_match(path, pattern) == 0);
    }
    // Legitimate ** matches still work after the fix.
    {
        rt_string pattern = rt_const_cstr("**/c");
        rt_string path = rt_const_cstr("a/b/c");
        test_result("** still crosses separators", rt_glob_match(path, pattern) == 1);
    }
    {
        rt_string pattern = rt_const_cstr("**/x?z");
        rt_string path = rt_const_cstr("a/b/xyz");
        test_result("** then ? matches within the final component",
                    rt_glob_match(path, pattern) == 1);
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

    // Test 13: Character class
    {
        rt_string pattern = rt_const_cstr("file[0-9].txt");
        rt_string path = rt_const_cstr("file7.txt");
        test_result("character class matches range", rt_glob_match(path, pattern) == 1);
    }

    // Test 14: Negated character class
    {
        rt_string pattern = rt_const_cstr("file[!0-9].txt");
        rt_string path = rt_const_cstr("filea.txt");
        test_result("negated character class matches non-range", rt_glob_match(path, pattern) == 1);
    }

    // Test 15: Character class mismatch
    {
        rt_string pattern = rt_const_cstr("file[abc].txt");
        rt_string path = rt_const_cstr("filez.txt");
        test_result("character class mismatch", rt_glob_match(path, pattern) == 0);
    }

#ifdef _WIN32
    // Test 16: Windows backslashes are path separators for wildcard boundaries.
    {
        rt_string pattern = rt_const_cstr("dir/*.txt");
        rt_string path = rt_const_cstr("dir\\hello.txt");
        test_result("Windows slash pattern matches backslash path",
                    rt_glob_match(path, pattern) == 1);
    }
    {
        rt_string pattern = rt_const_cstr("*.txt");
        rt_string path = rt_const_cstr("dir\\hello.txt");
        test_result("Windows * does not cross backslash", rt_glob_match(path, pattern) == 0);
    }
#endif

    // Test 17: long ** matches are handled without a fixed recursion attempt cap.
    {
        std::string deep_path(12000, 'a');
        deep_path += "/target.txt";
        rt_string pattern = rt_const_cstr("**/target.txt");
        rt_string path = rt_string_from_bytes(deep_path.data(), deep_path.size());
        test_result("long ** match", rt_glob_match(path, pattern) == 1);
        rt_string_unref(path);
    }

    // Test 18: embedded NUL bytes are rejected instead of truncating the match input.
    {
        const char path_bytes[] = {'s', 'a', 'f', 'e', '\0', '.', 't', 'x', 't'};
        const char pattern_bytes[] = {'s', 'a', 'f', 'e', '*', '\0', '*'};
        rt_string path = rt_string_from_bytes(path_bytes, sizeof(path_bytes));
        rt_string pattern = rt_string_from_bytes(pattern_bytes, sizeof(pattern_bytes));
        test_result("embedded NUL path rejected", rt_glob_match(path, rt_const_cstr("safe*")) == 0);
        test_result("embedded NUL pattern rejected",
                    rt_glob_match(rt_const_cstr("safe.txt"), pattern) == 0);
        rt_string_unref(path);
        rt_string_unref(pattern);
    }

    printf("\n");
}

static void test_glob_null_safety() {
    printf("Testing Glob null safety:\n");

    test_result("null path returns false", rt_glob_match(nullptr, rt_const_cstr("*.txt")) == 0);
    test_result("null pattern returns false",
                rt_glob_match(rt_const_cstr("hello.txt"), nullptr) == 0);

    void *files = rt_glob_files(rt_const_cstr("."), nullptr);
    test_result("Files null pattern returns empty", rt_seq_len(files) == 0);

    files = rt_glob_files(nullptr, rt_const_cstr("*"));
    test_result("Files null dir returns empty", rt_seq_len(files) == 0);

    void *recursive = rt_glob_files_recursive(rt_const_cstr("."), nullptr);
    test_result("FilesRecursive null pattern returns empty", rt_seq_len(recursive) == 0);

    recursive = rt_glob_files_recursive(nullptr, rt_const_cstr("*"));
    test_result("FilesRecursive null base returns empty", rt_seq_len(recursive) == 0);

    void *entries = rt_glob_entries(rt_const_cstr("."), nullptr);
    test_result("Entries null pattern returns empty", rt_seq_len(entries) == 0);

    entries = rt_glob_entries(nullptr, rt_const_cstr("*"));
    test_result("Entries null dir returns empty", rt_seq_len(entries) == 0);

    const char pattern_bytes[] = {'*', '\0', '*'};
    rt_string nul_pattern = rt_string_from_bytes(pattern_bytes, sizeof(pattern_bytes));
    files = rt_glob_files(rt_const_cstr("."), nul_pattern);
    test_result("Files embedded NUL pattern returns empty", rt_seq_len(files) == 0);
    recursive = rt_glob_files_recursive(rt_const_cstr("."), nul_pattern);
    test_result("FilesRecursive embedded NUL pattern returns empty", rt_seq_len(recursive) == 0);
    entries = rt_glob_entries(rt_const_cstr("."), nul_pattern);
    test_result("Entries embedded NUL pattern returns empty", rt_seq_len(entries) == 0);
    rt_string_unref(nul_pattern);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main() {
    printf("=== RT Glob Tests ===\n\n");

    test_glob_match();
    test_glob_null_safety();

    printf("All Glob tests passed!\n");
    return 0;
}
