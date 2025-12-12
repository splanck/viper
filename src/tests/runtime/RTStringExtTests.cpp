//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTStringExtTests.cpp
// Purpose: Comprehensive tests for Viper.String extended functions.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

// Helper to create a runtime string from C string literal
static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

// Helper to compare runtime string with C string
static bool str_eq(rt_string s, const char *expected)
{
    if (!s && !expected)
        return true;
    if (!s || !expected)
        return false;
    const char *cstr = rt_string_cstr(s);
    return strcmp(cstr, expected) == 0;
}

//===----------------------------------------------------------------------===//
// Replace tests
//===----------------------------------------------------------------------===//

static void test_replace_basic()
{
    rt_string hay = make_str("hello world");
    rt_string needle = make_str("world");
    rt_string repl = make_str("universe");

    rt_string result = rt_str_replace(hay, needle, repl);
    assert(str_eq(result, "hello universe"));
}

static void test_replace_multiple()
{
    rt_string hay = make_str("foo bar foo baz foo");
    rt_string needle = make_str("foo");
    rt_string repl = make_str("qux");

    rt_string result = rt_str_replace(hay, needle, repl);
    assert(str_eq(result, "qux bar qux baz qux"));
}

static void test_replace_to_empty()
{
    rt_string hay = make_str("hello world");
    rt_string needle = make_str(" ");
    rt_string repl = make_str("");

    rt_string result = rt_str_replace(hay, needle, repl);
    assert(str_eq(result, "helloworld"));
}

static void test_replace_not_found()
{
    rt_string hay = make_str("hello world");
    rt_string needle = make_str("xyz");
    rt_string repl = make_str("abc");

    rt_string result = rt_str_replace(hay, needle, repl);
    assert(str_eq(result, "hello world"));
}

static void test_replace_empty_needle()
{
    rt_string hay = make_str("hello");
    rt_string needle = make_str("");
    rt_string repl = make_str("x");

    rt_string result = rt_str_replace(hay, needle, repl);
    assert(str_eq(result, "hello")); // Empty needle returns original
}

//===----------------------------------------------------------------------===//
// StartsWith/EndsWith/Has tests
//===----------------------------------------------------------------------===//

static void test_starts_with()
{
    rt_string str = make_str("hello world");

    assert(rt_str_starts_with(str, make_str("hello")) == 1);
    assert(rt_str_starts_with(str, make_str("world")) == 0);
    assert(rt_str_starts_with(str, make_str("")) == 1);
    assert(rt_str_starts_with(str, make_str("hello world!")) == 0);
    assert(rt_str_starts_with(str, str) == 1);
}

static void test_ends_with()
{
    rt_string str = make_str("hello world");

    assert(rt_str_ends_with(str, make_str("world")) == 1);
    assert(rt_str_ends_with(str, make_str("hello")) == 0);
    assert(rt_str_ends_with(str, make_str("")) == 1);
    assert(rt_str_ends_with(str, make_str("!hello world")) == 0);
    assert(rt_str_ends_with(str, str) == 1);
}

static void test_has()
{
    rt_string str = make_str("hello world");

    assert(rt_str_has(str, make_str("hello")) == 1);
    assert(rt_str_has(str, make_str("world")) == 1);
    assert(rt_str_has(str, make_str("lo wo")) == 1);
    assert(rt_str_has(str, make_str("xyz")) == 0);
    assert(rt_str_has(str, make_str("")) == 1);
}

//===----------------------------------------------------------------------===//
// Count tests
//===----------------------------------------------------------------------===//

static void test_count()
{
    rt_string str = make_str("abracadabra");

    assert(rt_str_count(str, make_str("a")) == 5);
    assert(rt_str_count(str, make_str("abra")) == 2);
    assert(rt_str_count(str, make_str("xyz")) == 0);
    assert(rt_str_count(str, make_str("")) == 0);
}

static void test_count_nonoverlapping()
{
    rt_string str = make_str("aaaa");

    // Non-overlapping: "aaaa" contains 2 "aa" (positions 0 and 2)
    assert(rt_str_count(str, make_str("aa")) == 2);
}

//===----------------------------------------------------------------------===//
// PadLeft/PadRight tests
//===----------------------------------------------------------------------===//

static void test_pad_left()
{
    rt_string str = make_str("42");

    rt_string result = rt_str_pad_left(str, 5, make_str("0"));
    assert(str_eq(result, "00042"));

    // No padding if already at width
    rt_string no_pad = rt_str_pad_left(str, 2, make_str("0"));
    assert(str_eq(no_pad, "42"));

    // No padding if wider than target
    rt_string wider = rt_str_pad_left(str, 1, make_str("0"));
    assert(str_eq(wider, "42"));
}

static void test_pad_right()
{
    rt_string str = make_str("hi");

    rt_string result = rt_str_pad_right(str, 5, make_str("."));
    assert(str_eq(result, "hi..."));

    // No padding if already at width
    rt_string no_pad = rt_str_pad_right(str, 2, make_str("."));
    assert(str_eq(no_pad, "hi"));
}

static void test_pad_empty_pad_char()
{
    rt_string str = make_str("test");
    rt_string empty_pad = make_str("");

    // Empty pad string returns original
    rt_string result = rt_str_pad_left(str, 10, empty_pad);
    assert(str_eq(result, "test"));
}

//===----------------------------------------------------------------------===//
// Split/Join tests
//===----------------------------------------------------------------------===//

static void test_split_basic()
{
    rt_string str = make_str("a,b,c");
    rt_string delim = make_str(",");

    void *seq = rt_str_split(str, delim);
    assert(rt_seq_len(seq) == 3);
    assert(str_eq((rt_string)rt_seq_get(seq, 0), "a"));
    assert(str_eq((rt_string)rt_seq_get(seq, 1), "b"));
    assert(str_eq((rt_string)rt_seq_get(seq, 2), "c"));
}

static void test_split_multichar_delim()
{
    rt_string str = make_str("a::b::c");
    rt_string delim = make_str("::");

    void *seq = rt_str_split(str, delim);
    assert(rt_seq_len(seq) == 3);
    assert(str_eq((rt_string)rt_seq_get(seq, 0), "a"));
    assert(str_eq((rt_string)rt_seq_get(seq, 1), "b"));
    assert(str_eq((rt_string)rt_seq_get(seq, 2), "c"));
}

static void test_split_no_delim()
{
    rt_string str = make_str("hello");
    rt_string delim = make_str(",");

    void *seq = rt_str_split(str, delim);
    assert(rt_seq_len(seq) == 1);
    assert(str_eq((rt_string)rt_seq_get(seq, 0), "hello"));
}

static void test_split_empty_parts()
{
    rt_string str = make_str(",a,,b,");
    rt_string delim = make_str(",");

    void *seq = rt_str_split(str, delim);
    assert(rt_seq_len(seq) == 5);
    assert(str_eq((rt_string)rt_seq_get(seq, 0), ""));
    assert(str_eq((rt_string)rt_seq_get(seq, 1), "a"));
    assert(str_eq((rt_string)rt_seq_get(seq, 2), ""));
    assert(str_eq((rt_string)rt_seq_get(seq, 3), "b"));
    assert(str_eq((rt_string)rt_seq_get(seq, 4), ""));
}

static void test_join_basic()
{
    void *seq = rt_seq_new();
    rt_seq_push(seq, (void *)make_str("a"));
    rt_seq_push(seq, (void *)make_str("b"));
    rt_seq_push(seq, (void *)make_str("c"));

    rt_string sep = make_str(",");
    rt_string result = rt_strings_join(sep, seq);
    assert(str_eq(result, "a,b,c"));
}

static void test_join_empty_sep()
{
    void *seq = rt_seq_new();
    rt_seq_push(seq, (void *)make_str("a"));
    rt_seq_push(seq, (void *)make_str("b"));
    rt_seq_push(seq, (void *)make_str("c"));

    rt_string sep = make_str("");
    rt_string result = rt_strings_join(sep, seq);
    assert(str_eq(result, "abc"));
}

static void test_join_empty_seq()
{
    void *seq = rt_seq_new();

    rt_string sep = make_str(",");
    rt_string result = rt_strings_join(sep, seq);
    assert(str_eq(result, ""));
}

static void test_split_join_roundtrip()
{
    rt_string original = make_str("hello:world:test");
    rt_string delim = make_str(":");

    void *parts = rt_str_split(original, delim);
    rt_string rejoined = rt_strings_join(delim, parts);

    assert(str_eq(rejoined, "hello:world:test"));
}

//===----------------------------------------------------------------------===//
// Repeat tests
//===----------------------------------------------------------------------===//

static void test_repeat()
{
    rt_string str = make_str("ab");

    assert(str_eq(rt_str_repeat(str, 3), "ababab"));
    assert(str_eq(rt_str_repeat(str, 1), "ab"));
    assert(str_eq(rt_str_repeat(str, 0), ""));
}

static void test_repeat_negative()
{
    rt_string str = make_str("test");

    assert(str_eq(rt_str_repeat(str, -5), ""));
}

static void test_repeat_empty()
{
    rt_string str = make_str("");

    assert(str_eq(rt_str_repeat(str, 100), ""));
}

//===----------------------------------------------------------------------===//
// Flip tests
//===----------------------------------------------------------------------===//

static void test_flip()
{
    assert(str_eq(rt_str_flip(make_str("hello")), "olleh"));
    assert(str_eq(rt_str_flip(make_str("a")), "a"));
    assert(str_eq(rt_str_flip(make_str("")), ""));
    assert(str_eq(rt_str_flip(make_str("ab")), "ba"));
}

static void test_flip_palindrome()
{
    rt_string str = make_str("racecar");
    assert(str_eq(rt_str_flip(str), "racecar"));
}

//===----------------------------------------------------------------------===//
// Cmp tests
//===----------------------------------------------------------------------===//

static void test_cmp()
{
    assert(rt_str_cmp(make_str("abc"), make_str("abc")) == 0);
    assert(rt_str_cmp(make_str("abc"), make_str("abd")) == -1);
    assert(rt_str_cmp(make_str("abd"), make_str("abc")) == 1);
    assert(rt_str_cmp(make_str("ab"), make_str("abc")) == -1);
    assert(rt_str_cmp(make_str("abc"), make_str("ab")) == 1);
}

static void test_cmp_nocase()
{
    assert(rt_str_cmp_nocase(make_str("ABC"), make_str("abc")) == 0);
    assert(rt_str_cmp_nocase(make_str("abc"), make_str("ABC")) == 0);
    assert(rt_str_cmp_nocase(make_str("ABC"), make_str("abd")) == -1);
    assert(rt_str_cmp_nocase(make_str("ABD"), make_str("abc")) == 1);
}

static void test_cmp_null()
{
    assert(rt_str_cmp(NULL, NULL) == 0);
    assert(rt_str_cmp(make_str("a"), NULL) == 1);
    assert(rt_str_cmp(NULL, make_str("a")) == -1);
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

int main()
{
    // Replace tests
    test_replace_basic();
    test_replace_multiple();
    test_replace_to_empty();
    test_replace_not_found();
    test_replace_empty_needle();

    // StartsWith/EndsWith/Has tests
    test_starts_with();
    test_ends_with();
    test_has();

    // Count tests
    test_count();
    test_count_nonoverlapping();

    // PadLeft/PadRight tests
    test_pad_left();
    test_pad_right();
    test_pad_empty_pad_char();

    // Split/Join tests
    test_split_basic();
    test_split_multichar_delim();
    test_split_no_delim();
    test_split_empty_parts();
    test_join_basic();
    test_join_empty_sep();
    test_join_empty_seq();
    test_split_join_roundtrip();

    // Repeat tests
    test_repeat();
    test_repeat_negative();
    test_repeat_empty();

    // Flip tests
    test_flip();
    test_flip_palindrome();

    // Cmp tests
    test_cmp();
    test_cmp_nocase();
    test_cmp_null();

    return 0;
}
