//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTScannerTests.cpp
// Purpose: Validate StringScanner utility.
//
//===----------------------------------------------------------------------===//

#include "rt_scanner.h"
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
// Scanner Tests
//=============================================================================

static void test_scanner_creation()
{
    printf("Testing Scanner Creation:\n");

    // Test 1: Create scanner
    {
        rt_string src = rt_const_cstr("hello world");
        void *s = rt_scanner_new(src);
        test_result("Scanner created", s != NULL);
        test_result("Pos starts at 0", rt_scanner_pos(s) == 0);
        test_result("Len is correct", rt_scanner_len(s) == 11);
        test_result("Not at end", rt_scanner_is_end(s) == 0);
    }

    printf("\n");
}

static void test_scanner_peeking()
{
    printf("Testing Scanner Peeking:\n");

    rt_string src = rt_const_cstr("abc123");
    void *s = rt_scanner_new(src);

    // Test 1: Peek current
    {
        test_result("Peek returns 'a'", rt_scanner_peek(s) == 'a');
        test_result("Pos unchanged", rt_scanner_pos(s) == 0);
    }

    // Test 2: PeekAt
    {
        test_result("PeekAt(0) is 'a'", rt_scanner_peek_at(s, 0) == 'a');
        test_result("PeekAt(1) is 'b'", rt_scanner_peek_at(s, 1) == 'b');
        test_result("PeekAt(5) is '3'", rt_scanner_peek_at(s, 5) == '3');
        test_result("PeekAt(6) out of bounds", rt_scanner_peek_at(s, 6) == -1);
    }

    // Test 3: PeekStr
    {
        rt_string peeked = rt_scanner_peek_str(s, 3);
        test_result("PeekStr(3) is 'abc'", strcmp(rt_string_cstr(peeked), "abc") == 0);
        test_result("Pos still 0", rt_scanner_pos(s) == 0);
    }

    printf("\n");
}

static void test_scanner_reading()
{
    printf("Testing Scanner Reading:\n");

    // Test 1: Read single characters
    {
        rt_string src = rt_const_cstr("abc");
        void *s = rt_scanner_new(src);

        test_result("Read 'a'", rt_scanner_read(s) == 'a');
        test_result("Pos is 1", rt_scanner_pos(s) == 1);
        test_result("Read 'b'", rt_scanner_read(s) == 'b');
        test_result("Read 'c'", rt_scanner_read(s) == 'c');
        test_result("At end", rt_scanner_is_end(s) == 1);
        test_result("Read at end returns -1", rt_scanner_read(s) == -1);
    }

    // Test 2: ReadStr
    {
        rt_string src = rt_const_cstr("hello world");
        void *s = rt_scanner_new(src);

        rt_string result = rt_scanner_read_str(s, 5);
        test_result("ReadStr(5) is 'hello'", strcmp(rt_string_cstr(result), "hello") == 0);
        test_result("Pos is 5", rt_scanner_pos(s) == 5);
    }

    // Test 3: ReadUntil
    {
        rt_string src = rt_const_cstr("key=value");
        void *s = rt_scanner_new(src);

        rt_string key = rt_scanner_read_until(s, '=');
        test_result("ReadUntil '=' gives 'key'", strcmp(rt_string_cstr(key), "key") == 0);
        test_result("Pos at '='", rt_scanner_peek(s) == '=');
    }

    // Test 4: ReadUntilAny
    {
        rt_string src = rt_const_cstr("hello, world!");
        void *s = rt_scanner_new(src);

        rt_string word = rt_scanner_read_until_any(s, rt_const_cstr(",!"));
        test_result("ReadUntilAny gives 'hello'", strcmp(rt_string_cstr(word), "hello") == 0);
    }

    printf("\n");
}

static void test_scanner_matching()
{
    printf("Testing Scanner Matching:\n");

    // Test 1: Match char
    {
        rt_string src = rt_const_cstr("abc");
        void *s = rt_scanner_new(src);

        test_result("Match 'a' true", rt_scanner_match(s, 'a') == 1);
        test_result("Match 'b' false", rt_scanner_match(s, 'b') == 0);
    }

    // Test 2: MatchStr
    {
        rt_string src = rt_const_cstr("hello world");
        void *s = rt_scanner_new(src);

        test_result("MatchStr 'hello' true", rt_scanner_match_str(s, rt_const_cstr("hello")) == 1);
        test_result("MatchStr 'world' false", rt_scanner_match_str(s, rt_const_cstr("world")) == 0);
    }

    // Test 3: Accept
    {
        rt_string src = rt_const_cstr("abc");
        void *s = rt_scanner_new(src);

        test_result("Accept 'a' succeeds", rt_scanner_accept(s, 'a') == 1);
        test_result("Pos advanced", rt_scanner_pos(s) == 1);
        test_result("Accept 'a' now fails", rt_scanner_accept(s, 'a') == 0);
    }

    // Test 4: AcceptStr
    {
        rt_string src = rt_const_cstr("helloworld");
        void *s = rt_scanner_new(src);

        test_result("AcceptStr 'hello'", rt_scanner_accept_str(s, rt_const_cstr("hello")) == 1);
        test_result("Pos is 5", rt_scanner_pos(s) == 5);
    }

    // Test 5: AcceptAny
    {
        rt_string src = rt_const_cstr("abc");
        void *s = rt_scanner_new(src);

        test_result("AcceptAny 'xyz' fails", rt_scanner_accept_any(s, rt_const_cstr("xyz")) == 0);
        test_result("AcceptAny 'cba' succeeds", rt_scanner_accept_any(s, rt_const_cstr("cba")) == 1);
    }

    printf("\n");
}

static void test_scanner_skipping()
{
    printf("Testing Scanner Skipping:\n");

    // Test 1: Skip
    {
        rt_string src = rt_const_cstr("hello");
        void *s = rt_scanner_new(src);

        rt_scanner_skip(s, 3);
        test_result("Skip(3) advances pos", rt_scanner_pos(s) == 3);
        test_result("Peek is 'l'", rt_scanner_peek(s) == 'l');
    }

    // Test 2: SkipWhitespace
    {
        rt_string src = rt_const_cstr("   \t\nhello");
        void *s = rt_scanner_new(src);

        int64_t skipped = rt_scanner_skip_whitespace(s);
        test_result("Skipped 5 whitespace", skipped == 5);
        test_result("Peek is 'h'", rt_scanner_peek(s) == 'h');
    }

    printf("\n");
}

static void test_scanner_tokens()
{
    printf("Testing Scanner Token Helpers:\n");

    // Test 1: ReadIdent
    {
        rt_string src = rt_const_cstr("myVar_123 = 42");
        void *s = rt_scanner_new(src);

        rt_string ident = rt_scanner_read_ident(s);
        test_result("ReadIdent gives 'myVar_123'", strcmp(rt_string_cstr(ident), "myVar_123") == 0);
    }

    // Test 2: ReadInt
    {
        rt_string src = rt_const_cstr("-42abc");
        void *s = rt_scanner_new(src);

        rt_string num = rt_scanner_read_int(s);
        test_result("ReadInt gives '-42'", strcmp(rt_string_cstr(num), "-42") == 0);
        test_result("Stopped at 'a'", rt_scanner_peek(s) == 'a');
    }

    // Test 3: ReadNumber (float)
    {
        rt_string src = rt_const_cstr("3.14159end");
        void *s = rt_scanner_new(src);

        rt_string num = rt_scanner_read_number(s);
        test_result("ReadNumber gives '3.14159'", strcmp(rt_string_cstr(num), "3.14159") == 0);
    }

    // Test 4: ReadNumber with exponent
    {
        rt_string src = rt_const_cstr("1.5e-10");
        void *s = rt_scanner_new(src);

        rt_string num = rt_scanner_read_number(s);
        test_result("ReadNumber with exp", strcmp(rt_string_cstr(num), "1.5e-10") == 0);
    }

    // Test 5: ReadQuoted
    {
        rt_string src = rt_const_cstr("\"hello\\nworld\"");
        void *s = rt_scanner_new(src);

        rt_string quoted = rt_scanner_read_quoted(s, '"');
        test_result("ReadQuoted extracts content", strcmp(rt_string_cstr(quoted), "hello\nworld") == 0);
    }

    // Test 6: ReadLine
    {
        rt_string src = rt_const_cstr("line1\nline2\nline3");
        void *s = rt_scanner_new(src);

        rt_string line1 = rt_scanner_read_line(s);
        test_result("First line is 'line1'", strcmp(rt_string_cstr(line1), "line1") == 0);

        rt_string line2 = rt_scanner_read_line(s);
        test_result("Second line is 'line2'", strcmp(rt_string_cstr(line2), "line2") == 0);
    }

    printf("\n");
}

static void test_scanner_predicates()
{
    printf("Testing Scanner Predicates:\n");

    test_result("'5' is digit", rt_scanner_is_digit('5') == 1);
    test_result("'a' is not digit", rt_scanner_is_digit('a') == 0);
    test_result("'A' is alpha", rt_scanner_is_alpha('A') == 1);
    test_result("'5' is not alpha", rt_scanner_is_alpha('5') == 0);
    test_result("'z' is alnum", rt_scanner_is_alnum('z') == 1);
    test_result("'9' is alnum", rt_scanner_is_alnum('9') == 1);
    test_result("' ' is space", rt_scanner_is_space(' ') == 1);
    test_result("'\\n' is space", rt_scanner_is_space('\n') == 1);
    test_result("'a' is not space", rt_scanner_is_space('a') == 0);

    printf("\n");
}

static void test_scanner_position()
{
    printf("Testing Scanner Position Control:\n");

    rt_string src = rt_const_cstr("hello world");
    void *s = rt_scanner_new(src);

    // Test 1: SetPos
    {
        rt_scanner_set_pos(s, 6);
        test_result("SetPos(6) works", rt_scanner_pos(s) == 6);
        test_result("Peek is 'w'", rt_scanner_peek(s) == 'w');
    }

    // Test 2: Reset
    {
        rt_scanner_reset(s);
        test_result("Reset to 0", rt_scanner_pos(s) == 0);
        test_result("Peek is 'h'", rt_scanner_peek(s) == 'h');
    }

    // Test 3: Remaining
    {
        rt_scanner_set_pos(s, 6);
        test_result("Remaining is 5", rt_scanner_remaining(s) == 5);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Scanner Tests ===\n\n");

    test_scanner_creation();
    test_scanner_peeking();
    test_scanner_reading();
    test_scanner_matching();
    test_scanner_skipping();
    test_scanner_tokens();
    test_scanner_predicates();
    test_scanner_position();

    printf("All Scanner tests passed!\n");
    return 0;
}
