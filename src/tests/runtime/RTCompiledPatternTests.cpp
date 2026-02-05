//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Viper.Text.CompiledPattern
//
//===----------------------------------------------------------------------===//

#include "rt_compiled_pattern.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static void test_result(bool cond, const char *name)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
}

//=============================================================================
// Basic Matching Tests
//=============================================================================

static void test_basic_match()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("hello"));

    test_result(rt_compiled_pattern_is_match(pattern, rt_const_cstr("hello world")),
                "basic_match: should match");

    test_result(!rt_compiled_pattern_is_match(pattern, rt_const_cstr("goodbye world")),
                "basic_match: should not match");

    test_result(rt_compiled_pattern_is_match(pattern, rt_const_cstr("say hello")),
                "basic_match: should match anywhere");
}

static void test_regex_match()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    test_result(rt_compiled_pattern_is_match(pattern, rt_const_cstr("abc123def")),
                "regex_match: should match digits");

    test_result(!rt_compiled_pattern_is_match(pattern, rt_const_cstr("abc")),
                "regex_match: should not match without digits");
}

static void test_get_pattern()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("test\\d+"));
    rt_string pat = rt_compiled_pattern_get_pattern(pattern);
    test_result(strcmp(rt_string_cstr(pat), "test\\d+") == 0,
                "get_pattern: should return original pattern");
}

//=============================================================================
// Find Tests
//=============================================================================

static void test_find()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    rt_string result = rt_compiled_pattern_find(pattern, rt_const_cstr("abc123def456"));
    test_result(strcmp(rt_string_cstr(result), "123") == 0,
                "find: should find first match");

    result = rt_compiled_pattern_find(pattern, rt_const_cstr("no digits here"));
    test_result(strlen(rt_string_cstr(result)) == 0,
                "find: should return empty on no match");
}

static void test_find_from()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    rt_string result = rt_compiled_pattern_find_from(pattern, rt_const_cstr("abc123def456"), 6);
    test_result(strcmp(rt_string_cstr(result), "456") == 0,
                "find_from: should find from position");
}

static void test_find_pos()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("world"));

    int64_t pos = rt_compiled_pattern_find_pos(pattern, rt_const_cstr("hello world"));
    test_result(pos == 6, "find_pos: should return correct position");

    pos = rt_compiled_pattern_find_pos(pattern, rt_const_cstr("hello"));
    test_result(pos == -1, "find_pos: should return -1 on no match");
}

static void test_find_all()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    void *results = rt_compiled_pattern_find_all(pattern, rt_const_cstr("a1b22c333d"));
    test_result(rt_seq_len(results) == 3, "find_all: should find all matches");

    rt_string first = (rt_string)rt_seq_get(results, 0);
    rt_string second = (rt_string)rt_seq_get(results, 1);
    rt_string third = (rt_string)rt_seq_get(results, 2);

    test_result(strcmp(rt_string_cstr(first), "1") == 0, "find_all: first match");
    test_result(strcmp(rt_string_cstr(second), "22") == 0, "find_all: second match");
    test_result(strcmp(rt_string_cstr(third), "333") == 0, "find_all: third match");
}

//=============================================================================
// Capture Group Tests
//=============================================================================

static void test_captures_basic()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("(\\d+)-(\\d+)"));

    void *groups = rt_compiled_pattern_captures(pattern, rt_const_cstr("test 123-456 end"));
    test_result(rt_seq_len(groups) == 3, "captures: should have 3 groups (full + 2 captures)");

    rt_string full = (rt_string)rt_seq_get(groups, 0);
    rt_string g1 = (rt_string)rt_seq_get(groups, 1);
    rt_string g2 = (rt_string)rt_seq_get(groups, 2);

    test_result(strcmp(rt_string_cstr(full), "123-456") == 0, "captures: full match");
    test_result(strcmp(rt_string_cstr(g1), "123") == 0, "captures: group 1");
    test_result(strcmp(rt_string_cstr(g2), "456") == 0, "captures: group 2");
}

static void test_captures_no_match()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("(\\d+)"));

    void *groups = rt_compiled_pattern_captures(pattern, rt_const_cstr("no digits"));
    test_result(rt_seq_len(groups) == 0, "captures: should be empty on no match");
}

static void test_captures_nested()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("((\\w+)@(\\w+))"));

    void *groups = rt_compiled_pattern_captures(pattern, rt_const_cstr("email: user@host.com"));
    // Groups: 0=full, 1=outer group, 2=user, 3=host
    test_result(rt_seq_len(groups) >= 3, "captures_nested: should have multiple groups");
}

//=============================================================================
// Replace Tests
//=============================================================================

static void test_replace()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    rt_string result = rt_compiled_pattern_replace(pattern,
        rt_const_cstr("a1b2c3"),
        rt_const_cstr("X"));
    test_result(strcmp(rt_string_cstr(result), "aXbXcX") == 0,
                "replace: should replace all matches");
}

static void test_replace_first()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    rt_string result = rt_compiled_pattern_replace_first(pattern,
        rt_const_cstr("a1b2c3"),
        rt_const_cstr("X"));
    test_result(strcmp(rt_string_cstr(result), "aXb2c3") == 0,
                "replace_first: should replace only first match");
}

static void test_replace_no_match()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\d+"));

    rt_string result = rt_compiled_pattern_replace(pattern,
        rt_const_cstr("no digits"),
        rt_const_cstr("X"));
    test_result(strcmp(rt_string_cstr(result), "no digits") == 0,
                "replace_no_match: should return original on no match");
}

//=============================================================================
// Split Tests
//=============================================================================

static void test_split()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr(","));

    void *parts = rt_compiled_pattern_split(pattern, rt_const_cstr("a,b,c"));
    test_result(rt_seq_len(parts) == 3, "split: should split into 3 parts");

    test_result(strcmp(rt_string_cstr((rt_string)rt_seq_get(parts, 0)), "a") == 0, "split: part 0");
    test_result(strcmp(rt_string_cstr((rt_string)rt_seq_get(parts, 1)), "b") == 0, "split: part 1");
    test_result(strcmp(rt_string_cstr((rt_string)rt_seq_get(parts, 2)), "c") == 0, "split: part 2");
}

static void test_split_regex()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("\\s+"));

    void *parts = rt_compiled_pattern_split(pattern, rt_const_cstr("one   two\tthree"));
    test_result(rt_seq_len(parts) == 3, "split_regex: should split by whitespace");
}

static void test_split_limit()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr(","));

    void *parts = rt_compiled_pattern_split_n(pattern, rt_const_cstr("a,b,c,d,e"), 3);
    test_result(rt_seq_len(parts) == 3, "split_limit: should split into 3 parts max");

    test_result(strcmp(rt_string_cstr((rt_string)rt_seq_get(parts, 0)), "a") == 0, "split_limit: part 0");
    test_result(strcmp(rt_string_cstr((rt_string)rt_seq_get(parts, 1)), "b") == 0, "split_limit: part 1");
    test_result(strcmp(rt_string_cstr((rt_string)rt_seq_get(parts, 2)), "c,d,e") == 0, "split_limit: part 2 (rest)");
}

//=============================================================================
// Edge Cases
//=============================================================================

static void test_empty_pattern()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr(""));

    // Empty pattern matches at every position
    test_result(rt_compiled_pattern_is_match(pattern, rt_const_cstr("test")),
                "empty_pattern: should match");
}

static void test_empty_text()
{
    void *pattern = rt_compiled_pattern_new(rt_const_cstr("a"));

    test_result(!rt_compiled_pattern_is_match(pattern, rt_const_cstr("")),
                "empty_text: should not match non-empty pattern");

    void *empty_pat = rt_compiled_pattern_new(rt_const_cstr(""));
    test_result(rt_compiled_pattern_is_match(empty_pat, rt_const_cstr("")),
                "empty_text: empty pattern should match");
}

static void test_anchors()
{
    void *start_pattern = rt_compiled_pattern_new(rt_const_cstr("^hello"));
    void *end_pattern = rt_compiled_pattern_new(rt_const_cstr("world$"));

    test_result(rt_compiled_pattern_is_match(start_pattern, rt_const_cstr("hello world")),
                "anchors: ^ should match at start");
    test_result(!rt_compiled_pattern_is_match(start_pattern, rt_const_cstr("say hello")),
                "anchors: ^ should not match in middle");

    test_result(rt_compiled_pattern_is_match(end_pattern, rt_const_cstr("hello world")),
                "anchors: $ should match at end");
    test_result(!rt_compiled_pattern_is_match(end_pattern, rt_const_cstr("world hello")),
                "anchors: $ should not match in middle");
}

static void test_character_classes()
{
    void *digit = rt_compiled_pattern_new(rt_const_cstr("[0-9]+"));
    void *word = rt_compiled_pattern_new(rt_const_cstr("[a-zA-Z]+"));
    void *not_digit = rt_compiled_pattern_new(rt_const_cstr("[^0-9]+"));

    test_result(rt_compiled_pattern_is_match(digit, rt_const_cstr("abc123")),
                "char_class: [0-9] should match digits");
    test_result(rt_compiled_pattern_is_match(word, rt_const_cstr("Hello123")),
                "char_class: [a-zA-Z] should match letters");
    test_result(rt_compiled_pattern_is_match(not_digit, rt_const_cstr("abc")),
                "char_class: [^0-9] should match non-digits");
}

static void test_quantifiers()
{
    void *star = rt_compiled_pattern_new(rt_const_cstr("ab*c"));
    void *plus = rt_compiled_pattern_new(rt_const_cstr("ab+c"));
    void *quest = rt_compiled_pattern_new(rt_const_cstr("ab?c"));

    // a*
    test_result(rt_compiled_pattern_is_match(star, rt_const_cstr("ac")), "quantifiers: * matches zero");
    test_result(rt_compiled_pattern_is_match(star, rt_const_cstr("abc")), "quantifiers: * matches one");
    test_result(rt_compiled_pattern_is_match(star, rt_const_cstr("abbbc")), "quantifiers: * matches many");

    // a+
    test_result(!rt_compiled_pattern_is_match(plus, rt_const_cstr("ac")), "quantifiers: + requires one");
    test_result(rt_compiled_pattern_is_match(plus, rt_const_cstr("abc")), "quantifiers: + matches one");
    test_result(rt_compiled_pattern_is_match(plus, rt_const_cstr("abbbc")), "quantifiers: + matches many");

    // a?
    test_result(rt_compiled_pattern_is_match(quest, rt_const_cstr("ac")), "quantifiers: ? matches zero");
    test_result(rt_compiled_pattern_is_match(quest, rt_const_cstr("abc")), "quantifiers: ? matches one");
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    // Basic matching
    test_basic_match();
    test_regex_match();
    test_get_pattern();

    // Find operations
    test_find();
    test_find_from();
    test_find_pos();
    test_find_all();

    // Capture groups
    test_captures_basic();
    test_captures_no_match();
    test_captures_nested();

    // Replace operations
    test_replace();
    test_replace_first();
    test_replace_no_match();

    // Split operations
    test_split();
    test_split_regex();
    test_split_limit();

    // Edge cases
    test_empty_pattern();
    test_empty_text();
    test_anchors();
    test_character_classes();
    test_quantifiers();

    printf("All CompiledPattern tests passed!\n");
    return 0;
}
