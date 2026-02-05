//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTextWrapTests.cpp
// Purpose: Validate TextWrapper utility.
//
//===----------------------------------------------------------------------===//

#include "rt_textwrap.h"
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
// TextWrapper Tests
//=============================================================================

static void test_wrap()
{
    printf("Testing TextWrapper Wrap:\n");

    // Test 1: Short text (no wrapping needed)
    {
        rt_string text = rt_const_cstr("Hello");
        rt_string result = rt_textwrap_wrap(text, 20);
        test_result("Short text unchanged", strcmp(rt_string_cstr(result), "Hello") == 0);
    }

    // Test 2: Wrap at word boundary
    {
        rt_string text = rt_const_cstr("Hello world test");
        rt_string result = rt_textwrap_wrap(text, 12);
        test_result("Wrapped at word", strstr(rt_string_cstr(result), "\n") != NULL);
    }

    // Test 3: Preserve existing newlines
    {
        rt_string text = rt_const_cstr("Line1\nLine2");
        rt_string result = rt_textwrap_wrap(text, 80);
        test_result("Preserves newlines", strcmp(rt_string_cstr(result), "Line1\nLine2") == 0);
    }

    printf("\n");
}

static void test_indent()
{
    printf("Testing TextWrapper Indent:\n");

    // Test 1: Indent single line
    {
        rt_string text = rt_const_cstr("Hello");
        rt_string result = rt_textwrap_indent(text, rt_const_cstr("  "));
        test_result("Indent single line", strcmp(rt_string_cstr(result), "  Hello") == 0);
    }

    // Test 2: Indent multiple lines
    {
        rt_string text = rt_const_cstr("Line1\nLine2");
        rt_string result = rt_textwrap_indent(text, rt_const_cstr("> "));
        test_result("Indent multiple lines", strcmp(rt_string_cstr(result), "> Line1\n> Line2") == 0);
    }

    printf("\n");
}

static void test_dedent()
{
    printf("Testing TextWrapper Dedent:\n");

    // Test 1: Remove common indent
    {
        rt_string text = rt_const_cstr("    Line1\n    Line2");
        rt_string result = rt_textwrap_dedent(text);
        test_result("Removes common indent", strcmp(rt_string_cstr(result), "Line1\nLine2") == 0);
    }

    // Test 2: Mixed indent (uses minimum)
    {
        rt_string text = rt_const_cstr("  Line1\n    Line2");
        rt_string result = rt_textwrap_dedent(text);
        test_result("Uses minimum indent", strncmp(rt_string_cstr(result), "Line1\n", 6) == 0);
    }

    printf("\n");
}

static void test_truncate()
{
    printf("Testing TextWrapper Truncate:\n");

    // Test 1: Truncate with ellipsis
    {
        rt_string text = rt_const_cstr("Hello World");
        rt_string result = rt_textwrap_truncate(text, 8);
        test_result("Truncate with ellipsis", strcmp(rt_string_cstr(result), "Hello...") == 0);
    }

    // Test 2: No truncation needed
    {
        rt_string text = rt_const_cstr("Hello");
        rt_string result = rt_textwrap_truncate(text, 10);
        test_result("No truncation if short", strcmp(rt_string_cstr(result), "Hello") == 0);
    }

    // Test 3: Custom suffix
    {
        rt_string text = rt_const_cstr("Hello World");
        rt_string result = rt_textwrap_truncate_with(text, 9, rt_const_cstr(">>"));
        test_result("Custom suffix", strcmp(rt_string_cstr(result), "Hello W>>") == 0);
    }

    printf("\n");
}

static void test_shorten()
{
    printf("Testing TextWrapper Shorten:\n");

    // Test 1: Shorten in middle
    {
        rt_string text = rt_const_cstr("Hello World Test");
        rt_string result = rt_textwrap_shorten(text, 11);
        // Should be something like "Hell...Test"
        test_result("Shorten has ellipsis", strstr(rt_string_cstr(result), "...") != NULL);
        test_result("Shorten starts with H", rt_string_cstr(result)[0] == 'H');
    }

    printf("\n");
}

static void test_alignment()
{
    printf("Testing TextWrapper Alignment:\n");

    // Test 1: Left align
    {
        rt_string text = rt_const_cstr("Hi");
        rt_string result = rt_textwrap_left(text, 5);
        test_result("Left align", strcmp(rt_string_cstr(result), "Hi   ") == 0);
    }

    // Test 2: Right align
    {
        rt_string text = rt_const_cstr("Hi");
        rt_string result = rt_textwrap_right(text, 5);
        test_result("Right align", strcmp(rt_string_cstr(result), "   Hi") == 0);
    }

    // Test 3: Center align
    {
        rt_string text = rt_const_cstr("Hi");
        rt_string result = rt_textwrap_center(text, 6);
        test_result("Center align", strcmp(rt_string_cstr(result), "  Hi  ") == 0);
    }

    // Test 4: Odd width center
    {
        rt_string text = rt_const_cstr("Hi");
        rt_string result = rt_textwrap_center(text, 5);
        test_result("Center odd width", strcmp(rt_string_cstr(result), " Hi  ") == 0);
    }

    printf("\n");
}

static void test_utility()
{
    printf("Testing TextWrapper Utility:\n");

    // Test 1: Line count
    {
        rt_string text = rt_const_cstr("Line1\nLine2\nLine3");
        test_result("Line count", rt_textwrap_line_count(text) == 3);
    }

    // Test 2: Single line count
    {
        rt_string text = rt_const_cstr("No newlines");
        test_result("Single line count", rt_textwrap_line_count(text) == 1);
    }

    // Test 3: Max line length
    {
        rt_string text = rt_const_cstr("Hi\nHello\nHi");
        test_result("Max line length", rt_textwrap_max_line_len(text) == 5);
    }

    printf("\n");
}

static void test_hang()
{
    printf("Testing TextWrapper Hang:\n");

    // Test: Hanging indent
    {
        rt_string text = rt_const_cstr("First\nSecond\nThird");
        rt_string result = rt_textwrap_hang(text, rt_const_cstr("    "));
        // First line should not have indent
        test_result("Hang first line no indent", strncmp(rt_string_cstr(result), "First", 5) == 0);
        // Subsequent lines should have indent
        test_result("Hang has indented lines", strstr(rt_string_cstr(result), "    Second") != NULL);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT TextWrapper Tests ===\n\n");

    test_wrap();
    test_indent();
    test_dedent();
    test_truncate();
    test_shorten();
    test_alignment();
    test_utility();
    test_hang();

    printf("All TextWrapper tests passed!\n");
    return 0;
}
