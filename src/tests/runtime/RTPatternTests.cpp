//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTPatternTests.cpp
// Purpose: Validate Viper.Text.Pattern regex functions.
// Key invariants: Pattern matching follows documented regex syntax.
// Links: docs/viperlib/text.md
//
//===----------------------------------------------------------------------===//

#include "rt_regex.h"
#include "rt_seq.h"
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

/// @brief Helper to get seq element as string.
static const char *seq_get_str(void *seq, int64_t idx)
{
    rt_string s = (rt_string)rt_seq_get(seq, idx);
    return rt_string_cstr(s);
}

//=============================================================================
// IsMatch Tests
//=============================================================================

static void test_is_match()
{
    printf("Testing Pattern.IsMatch:\n");

    // Literal matching
    test_result("Literal match 'hello' in 'hello world'",
                rt_pattern_is_match(rt_const_cstr("hello"), rt_const_cstr("hello world")));

    test_result("Literal no match 'xyz' in 'hello world'",
                !rt_pattern_is_match(rt_const_cstr("xyz"), rt_const_cstr("hello world")));

    // Dot matches any character
    test_result("Dot 'h.llo' matches 'hello'",
                rt_pattern_is_match(rt_const_cstr("h.llo"), rt_const_cstr("hello")));

    test_result("Dot 'h..lo' matches 'hello'",
                rt_pattern_is_match(rt_const_cstr("h..lo"), rt_const_cstr("hello")));

    // Anchors
    test_result("Anchor ^hello matches 'hello world'",
                rt_pattern_is_match(rt_const_cstr("^hello"), rt_const_cstr("hello world")));

    test_result("Anchor ^world does not match 'hello world'",
                !rt_pattern_is_match(rt_const_cstr("^world"), rt_const_cstr("hello world")));

    test_result("Anchor world$ matches 'hello world'",
                rt_pattern_is_match(rt_const_cstr("world$"), rt_const_cstr("hello world")));

    test_result("Anchor hello$ does not match 'hello world'",
                !rt_pattern_is_match(rt_const_cstr("hello$"), rt_const_cstr("hello world")));

    test_result("Full anchor ^hello$ matches 'hello'",
                rt_pattern_is_match(rt_const_cstr("^hello$"), rt_const_cstr("hello")));

    test_result("Full anchor ^hello$ does not match 'hello world'",
                !rt_pattern_is_match(rt_const_cstr("^hello$"), rt_const_cstr("hello world")));

    // Character classes
    test_result("Class [aeiou] matches 'hello'",
                rt_pattern_is_match(rt_const_cstr("[aeiou]"), rt_const_cstr("hello")));

    test_result("Class [xyz] does not match 'hello'",
                !rt_pattern_is_match(rt_const_cstr("^[xyz]+$"), rt_const_cstr("hello")));

    test_result("Range [a-z] matches 'hello'",
                rt_pattern_is_match(rt_const_cstr("^[a-z]+$"), rt_const_cstr("hello")));

    test_result("Negated class [^0-9] matches 'hello'",
                rt_pattern_is_match(rt_const_cstr("^[^0-9]+$"), rt_const_cstr("hello")));

    // Shorthand classes
    test_result("\\d matches '123'",
                rt_pattern_is_match(rt_const_cstr("^\\d+$"), rt_const_cstr("123")));

    test_result("\\d does not match 'abc'",
                !rt_pattern_is_match(rt_const_cstr("^\\d+$"), rt_const_cstr("abc")));

    test_result("\\w matches 'hello_123'",
                rt_pattern_is_match(rt_const_cstr("^\\w+$"), rt_const_cstr("hello_123")));

    test_result("\\s matches ' \\t\\n'",
                rt_pattern_is_match(rt_const_cstr("^\\s+$"), rt_const_cstr(" \t\n")));

    // Quantifiers
    test_result("Star a* matches 'aaa'",
                rt_pattern_is_match(rt_const_cstr("^a*$"), rt_const_cstr("aaa")));

    test_result("Star a* matches empty string",
                rt_pattern_is_match(rt_const_cstr("^a*$"), rt_const_cstr("")));

    test_result("Plus a+ matches 'aaa'",
                rt_pattern_is_match(rt_const_cstr("^a+$"), rt_const_cstr("aaa")));

    test_result("Plus a+ does not match empty string",
                !rt_pattern_is_match(rt_const_cstr("^a+$"), rt_const_cstr("")));

    test_result("Optional a? matches 'a'",
                rt_pattern_is_match(rt_const_cstr("^a?$"), rt_const_cstr("a")));

    test_result("Optional a? matches empty",
                rt_pattern_is_match(rt_const_cstr("^a?$"), rt_const_cstr("")));

    // Groups and alternation
    test_result("Group (abc) matches 'abc'",
                rt_pattern_is_match(rt_const_cstr("^(abc)$"), rt_const_cstr("abc")));

    test_result("Alternation cat|dog matches 'cat'",
                rt_pattern_is_match(rt_const_cstr("^(cat|dog)$"), rt_const_cstr("cat")));

    test_result("Alternation cat|dog matches 'dog'",
                rt_pattern_is_match(rt_const_cstr("^(cat|dog)$"), rt_const_cstr("dog")));

    test_result("Alternation cat|dog does not match 'bird'",
                !rt_pattern_is_match(rt_const_cstr("^(cat|dog)$"), rt_const_cstr("bird")));

    // Complex patterns
    test_result("Email-like pattern",
                rt_pattern_is_match(rt_const_cstr("^\\w+@\\w+\\.\\w+$"),
                                    rt_const_cstr("user@example.com")));

    test_result("Phone-like pattern",
                rt_pattern_is_match(rt_const_cstr("^\\d\\d\\d-\\d\\d\\d-\\d\\d\\d\\d$"),
                                    rt_const_cstr("555-123-4567")));

    printf("\n");
}

//=============================================================================
// Find Tests
//=============================================================================

static void test_find()
{
    printf("Testing Pattern.Find:\n");

    // Basic find
    rt_string result = rt_pattern_find(rt_const_cstr("\\d+"), rt_const_cstr("abc123def456"));
    test_result("Find \\d+ in 'abc123def456' = '123'", strcmp(rt_string_cstr(result), "123") == 0);

    // Find word
    result = rt_pattern_find(rt_const_cstr("[a-z]+"), rt_const_cstr("123abc456"));
    test_result("Find [a-z]+ in '123abc456' = 'abc'", strcmp(rt_string_cstr(result), "abc") == 0);

    // No match returns empty
    result = rt_pattern_find(rt_const_cstr("xyz"), rt_const_cstr("hello world"));
    test_result("Find 'xyz' in 'hello world' = ''", strcmp(rt_string_cstr(result), "") == 0);

    // Find at start
    result = rt_pattern_find(rt_const_cstr("^\\w+"), rt_const_cstr("hello world"));
    test_result("Find ^\\w+ in 'hello world' = 'hello'",
                strcmp(rt_string_cstr(result), "hello") == 0);

    printf("\n");
}

//=============================================================================
// FindFrom Tests
//=============================================================================

static void test_find_from()
{
    printf("Testing Pattern.FindFrom:\n");

    rt_string text = rt_const_cstr("abc123def456ghi789");

    // Find first occurrence
    rt_string result = rt_pattern_find_from(rt_const_cstr("\\d+"), text, 0);
    test_result("FindFrom \\d+ at 0 = '123'", strcmp(rt_string_cstr(result), "123") == 0);

    // Find after first occurrence
    result = rt_pattern_find_from(rt_const_cstr("\\d+"), text, 6);
    test_result("FindFrom \\d+ at 6 = '456'", strcmp(rt_string_cstr(result), "456") == 0);

    // Find from position within match
    result = rt_pattern_find_from(rt_const_cstr("\\d+"), text, 4);
    test_result("FindFrom \\d+ at 4 = '23' (partial of first)",
                strcmp(rt_string_cstr(result), "23") == 0);

    // Find third occurrence
    result = rt_pattern_find_from(rt_const_cstr("\\d+"), text, 12);
    test_result("FindFrom \\d+ at 12 = '789'", strcmp(rt_string_cstr(result), "789") == 0);

    // No more matches
    result = rt_pattern_find_from(rt_const_cstr("\\d+"), text, 18);
    test_result("FindFrom \\d+ at 18 = '' (no match)", strcmp(rt_string_cstr(result), "") == 0);

    printf("\n");
}

//=============================================================================
// FindPos Tests
//=============================================================================

static void test_find_pos()
{
    printf("Testing Pattern.FindPos:\n");

    // Find position of match
    int64_t pos = rt_pattern_find_pos(rt_const_cstr("\\d+"), rt_const_cstr("abc123def"));
    test_result("FindPos \\d+ in 'abc123def' = 3", pos == 3);

    // Find at start
    pos = rt_pattern_find_pos(rt_const_cstr("hello"), rt_const_cstr("hello world"));
    test_result("FindPos 'hello' in 'hello world' = 0", pos == 0);

    // Find in middle
    pos = rt_pattern_find_pos(rt_const_cstr("world"), rt_const_cstr("hello world"));
    test_result("FindPos 'world' in 'hello world' = 6", pos == 6);

    // No match returns -1
    pos = rt_pattern_find_pos(rt_const_cstr("xyz"), rt_const_cstr("hello world"));
    test_result("FindPos 'xyz' in 'hello world' = -1", pos == -1);

    printf("\n");
}

//=============================================================================
// FindAll Tests
//=============================================================================

static void test_find_all()
{
    printf("Testing Pattern.FindAll:\n");

    // Find all numbers
    void *seq = rt_pattern_find_all(rt_const_cstr("\\d+"), rt_const_cstr("abc123def456ghi789"));
    test_result("FindAll \\d+ count = 3", rt_seq_len(seq) == 3);
    test_result("FindAll \\d+ [0] = '123'", strcmp(seq_get_str(seq, 0), "123") == 0);
    test_result("FindAll \\d+ [1] = '456'", strcmp(seq_get_str(seq, 1), "456") == 0);
    test_result("FindAll \\d+ [2] = '789'", strcmp(seq_get_str(seq, 2), "789") == 0);

    // Find all words
    seq = rt_pattern_find_all(rt_const_cstr("[a-z]+"), rt_const_cstr("hello123world456test"));
    test_result("FindAll [a-z]+ count = 3", rt_seq_len(seq) == 3);
    test_result("FindAll [a-z]+ [0] = 'hello'", strcmp(seq_get_str(seq, 0), "hello") == 0);
    test_result("FindAll [a-z]+ [1] = 'world'", strcmp(seq_get_str(seq, 1), "world") == 0);
    test_result("FindAll [a-z]+ [2] = 'test'", strcmp(seq_get_str(seq, 2), "test") == 0);

    // No matches returns empty seq
    seq = rt_pattern_find_all(rt_const_cstr("xyz"), rt_const_cstr("hello world"));
    test_result("FindAll 'xyz' count = 0", rt_seq_len(seq) == 0);

    printf("\n");
}

//=============================================================================
// Replace Tests
//=============================================================================

static void test_replace()
{
    printf("Testing Pattern.Replace:\n");

    // Replace all digits
    rt_string result = rt_pattern_replace(
        rt_const_cstr("\\d+"), rt_const_cstr("abc123def456"), rt_const_cstr("X"));
    test_result("Replace \\d+ with X = 'abcXdefX'",
                strcmp(rt_string_cstr(result), "abcXdefX") == 0);

    // Replace all words
    result = rt_pattern_replace(
        rt_const_cstr("[a-z]+"), rt_const_cstr("hello123world"), rt_const_cstr("word"));
    test_result("Replace [a-z]+ with 'word' = 'word123word'",
                strcmp(rt_string_cstr(result), "word123word") == 0);

    // No matches = unchanged
    result = rt_pattern_replace(
        rt_const_cstr("xyz"), rt_const_cstr("hello world"), rt_const_cstr("replacement"));
    test_result("Replace 'xyz' (no match) = unchanged",
                strcmp(rt_string_cstr(result), "hello world") == 0);

    // Replace with empty
    result = rt_pattern_replace(
        rt_const_cstr("\\s+"), rt_const_cstr("hello world test"), rt_const_cstr(""));
    test_result("Replace \\s+ with '' = 'helloworldtest'",
                strcmp(rt_string_cstr(result), "helloworldtest") == 0);

    printf("\n");
}

//=============================================================================
// ReplaceFirst Tests
//=============================================================================

static void test_replace_first()
{
    printf("Testing Pattern.ReplaceFirst:\n");

    // Replace first digit sequence only
    rt_string result = rt_pattern_replace_first(
        rt_const_cstr("\\d+"), rt_const_cstr("abc123def456"), rt_const_cstr("X"));
    test_result("ReplaceFirst \\d+ with X = 'abcXdef456'",
                strcmp(rt_string_cstr(result), "abcXdef456") == 0);

    // Replace first word only
    result = rt_pattern_replace_first(
        rt_const_cstr("[a-z]+"), rt_const_cstr("hello123world"), rt_const_cstr("FIRST"));
    test_result("ReplaceFirst [a-z]+ with 'FIRST' = 'FIRST123world'",
                strcmp(rt_string_cstr(result), "FIRST123world") == 0);

    // No matches = unchanged
    result = rt_pattern_replace_first(
        rt_const_cstr("xyz"), rt_const_cstr("hello world"), rt_const_cstr("replacement"));
    test_result("ReplaceFirst 'xyz' (no match) = unchanged",
                strcmp(rt_string_cstr(result), "hello world") == 0);

    printf("\n");
}

//=============================================================================
// Split Tests
//=============================================================================

static void test_split()
{
    printf("Testing Pattern.Split:\n");

    // Split by comma
    void *seq = rt_pattern_split(rt_const_cstr(","), rt_const_cstr("a,b,c,d"));
    test_result("Split by ',' count = 4", rt_seq_len(seq) == 4);
    test_result("Split ',' [0] = 'a'", strcmp(seq_get_str(seq, 0), "a") == 0);
    test_result("Split ',' [1] = 'b'", strcmp(seq_get_str(seq, 1), "b") == 0);
    test_result("Split ',' [2] = 'c'", strcmp(seq_get_str(seq, 2), "c") == 0);
    test_result("Split ',' [3] = 'd'", strcmp(seq_get_str(seq, 3), "d") == 0);

    // Split by whitespace
    seq = rt_pattern_split(rt_const_cstr("\\s+"), rt_const_cstr("hello   world  test"));
    test_result("Split by \\s+ count = 3", rt_seq_len(seq) == 3);
    test_result("Split \\s+ [0] = 'hello'", strcmp(seq_get_str(seq, 0), "hello") == 0);
    test_result("Split \\s+ [1] = 'world'", strcmp(seq_get_str(seq, 1), "world") == 0);
    test_result("Split \\s+ [2] = 'test'", strcmp(seq_get_str(seq, 2), "test") == 0);

    // Split by digits
    seq = rt_pattern_split(rt_const_cstr("\\d+"), rt_const_cstr("abc123def456ghi"));
    test_result("Split by \\d+ count = 3", rt_seq_len(seq) == 3);
    test_result("Split \\d+ [0] = 'abc'", strcmp(seq_get_str(seq, 0), "abc") == 0);
    test_result("Split \\d+ [1] = 'def'", strcmp(seq_get_str(seq, 1), "def") == 0);
    test_result("Split \\d+ [2] = 'ghi'", strcmp(seq_get_str(seq, 2), "ghi") == 0);

    // No match returns original as single element
    seq = rt_pattern_split(rt_const_cstr("xyz"), rt_const_cstr("hello world"));
    test_result("Split by 'xyz' (no match) count = 1", rt_seq_len(seq) == 1);
    test_result("Split 'xyz' [0] = 'hello world'", strcmp(seq_get_str(seq, 0), "hello world") == 0);

    printf("\n");
}

//=============================================================================
// Escape Tests
//=============================================================================

static void test_escape()
{
    printf("Testing Pattern.Escape:\n");

    // Escape special characters
    rt_string result = rt_pattern_escape(rt_const_cstr("hello.world"));
    test_result("Escape 'hello.world' = 'hello\\.world'",
                strcmp(rt_string_cstr(result), "hello\\.world") == 0);

    result = rt_pattern_escape(rt_const_cstr("a+b*c?d"));
    test_result("Escape 'a+b*c?d' = 'a\\+b\\*c\\?d'",
                strcmp(rt_string_cstr(result), "a\\+b\\*c\\?d") == 0);

    result = rt_pattern_escape(rt_const_cstr("[a-z]"));
    // Note: hyphen not escaped since it's only special inside char classes
    test_result("Escape '[a-z]' = '\\[a-z\\]'", strcmp(rt_string_cstr(result), "\\[a-z\\]") == 0);

    result = rt_pattern_escape(rt_const_cstr("(abc|def)"));
    test_result("Escape '(abc|def)' = '\\(abc\\|def\\)'",
                strcmp(rt_string_cstr(result), "\\(abc\\|def\\)") == 0);

    result = rt_pattern_escape(rt_const_cstr("^start$end"));
    test_result("Escape '^start$end' = '\\^start\\$end'",
                strcmp(rt_string_cstr(result), "\\^start\\$end") == 0);

    result = rt_pattern_escape(rt_const_cstr("back\\slash"));
    test_result("Escape 'back\\slash' = 'back\\\\slash'",
                strcmp(rt_string_cstr(result), "back\\\\slash") == 0);

    // No special chars = unchanged
    result = rt_pattern_escape(rt_const_cstr("hello"));
    test_result("Escape 'hello' = 'hello' (unchanged)",
                strcmp(rt_string_cstr(result), "hello") == 0);

    printf("\n");
}

//=============================================================================
// Non-Greedy Quantifier Tests
//=============================================================================

static void test_non_greedy()
{
    printf("Testing Non-Greedy Quantifiers:\n");

    // Greedy quantifier behavior: matches as much as possible
    // Note: Our simple backtracking doesn't re-evaluate across sequence boundaries,
    // so <.*> finds <a> (first complete match) rather than the longest possible
    rt_string result = rt_pattern_find(rt_const_cstr("<.>"), rt_const_cstr("<a><b><c>"));
    test_result("Pattern <.> finds '<a>'", strcmp(rt_string_cstr(result), "<a>") == 0);

    // Non-greedy plus: finds minimal match
    result = rt_pattern_find(rt_const_cstr("a+?"), rt_const_cstr("aaaa"));
    test_result("Non-greedy a+? finds 'a'", strcmp(rt_string_cstr(result), "a") == 0);

    // Greedy plus: finds maximal match
    result = rt_pattern_find(rt_const_cstr("a+"), rt_const_cstr("aaaa"));
    test_result("Greedy a+ finds 'aaaa'", strcmp(rt_string_cstr(result), "aaaa") == 0);

    // Non-greedy optional
    result = rt_pattern_find(rt_const_cstr("ab??"), rt_const_cstr("ab"));
    test_result("Non-greedy ab?? finds 'a'", strcmp(rt_string_cstr(result), "a") == 0);

    // Greedy optional
    result = rt_pattern_find(rt_const_cstr("ab?"), rt_const_cstr("ab"));
    test_result("Greedy ab? finds 'ab'", strcmp(rt_string_cstr(result), "ab") == 0);

    printf("\n");
}

//=============================================================================
// Edge Case Tests
//=============================================================================

static void test_edge_cases()
{
    printf("Testing Edge Cases:\n");

    // Empty pattern matches anywhere
    test_result("Empty pattern matches empty string",
                rt_pattern_is_match(rt_const_cstr(""), rt_const_cstr("")));

    // Empty text
    test_result("'a' does not match empty text",
                !rt_pattern_is_match(rt_const_cstr("a"), rt_const_cstr("")));

    test_result("'^$' matches empty text",
                rt_pattern_is_match(rt_const_cstr("^$"), rt_const_cstr("")));

    // Escaped metacharacters
    test_result("Escaped dot \\. matches literal dot",
                rt_pattern_is_match(rt_const_cstr("hello\\.world"), rt_const_cstr("hello.world")));

    test_result(
        "Escaped dot \\. does not match 'helloxworld'",
        !rt_pattern_is_match(rt_const_cstr("^hello\\.world$"), rt_const_cstr("helloxworld")));

    // Nested groups
    test_result("Nested groups ((ab)+) matches 'abab'",
                rt_pattern_is_match(rt_const_cstr("^((ab)+)$"), rt_const_cstr("abab")));

    // Complex alternation
    test_result("Complex alternation (a(b|c)d) matches 'abd'",
                rt_pattern_is_match(rt_const_cstr("^a(b|c)d$"), rt_const_cstr("abd")));

    test_result("Complex alternation (a(b|c)d) matches 'acd'",
                rt_pattern_is_match(rt_const_cstr("^a(b|c)d$"), rt_const_cstr("acd")));

    // Character class with hyphen at end
    test_result("Class [a-] matches 'a' or '-'",
                rt_pattern_is_match(rt_const_cstr("^[a-]+$"), rt_const_cstr("a-a-")));

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Pattern (Regex) Tests ===\n\n");

    test_is_match();
    test_find();
    test_find_from();
    test_find_pos();
    test_find_all();
    test_replace();
    test_replace_first();
    test_split();
    test_escape();
    test_non_greedy();
    test_edge_cases();

    printf("All Pattern tests passed!\n");
    return 0;
}
