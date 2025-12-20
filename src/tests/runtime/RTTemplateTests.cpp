//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTemplateTests.cpp
// Purpose: Validate Viper.Text.Template string templating functions.
// Key invariants: Placeholders are correctly replaced; missing keys left as-is.
// Links: docs/viperlib/text.md
//
//===----------------------------------------------------------------------===//

#include "rt_bag.h"
#include "rt_box.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_template.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Create a heap-allocated string (for use as map value).
static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

/// @brief Helper to compare strings.
static bool str_eq(rt_string a, const char *b)
{
    const char *a_str = rt_string_cstr(a);
    if (!a_str)
        a_str = "";
    return strcmp(a_str, b) == 0;
}

/// @brief Helper to check if bag contains string.
static bool bag_contains(void *bag, const char *str)
{
    return rt_bag_has(bag, rt_const_cstr(str)) != 0;
}

//=============================================================================
// Render Tests (Map-based)
//=============================================================================

static void test_render()
{
    printf("Testing Template.Render:\n");

    // Basic substitution
    void *values = rt_map_new();
    rt_map_set(values, rt_const_cstr("name"), rt_box_str(make_str("Alice")));
    rt_map_set(values, rt_const_cstr("count"), rt_box_str(make_str("5")));

    rt_string tmpl = rt_const_cstr("Hello {{name}}, you have {{count}} messages.");
    rt_string result = rt_template_render(tmpl, values);
    test_result("Basic substitution",
                str_eq(result, "Hello Alice, you have 5 messages."));

    // Whitespace in placeholder
    tmpl = rt_const_cstr("Hello {{ name }}, welcome!");
    result = rt_template_render(tmpl, values);
    test_result("Whitespace in placeholder",
                str_eq(result, "Hello Alice, welcome!"));

    // Multiple occurrences of same key
    tmpl = rt_const_cstr("{{name}} meets {{name}}");
    result = rt_template_render(tmpl, values);
    test_result("Multiple same key",
                str_eq(result, "Alice meets Alice"));

    // Missing key left as-is
    tmpl = rt_const_cstr("Hello {{unknown}}!");
    result = rt_template_render(tmpl, values);
    test_result("Missing key left as-is",
                str_eq(result, "Hello {{unknown}}!"));

    // Empty template
    tmpl = rt_const_cstr("");
    result = rt_template_render(tmpl, values);
    test_result("Empty template",
                str_eq(result, ""));

    // No placeholders
    tmpl = rt_const_cstr("No placeholders here");
    result = rt_template_render(tmpl, values);
    test_result("No placeholders",
                str_eq(result, "No placeholders here"));

    // Empty placeholder key - left as literal
    tmpl = rt_const_cstr("Hello {{}}!");
    result = rt_template_render(tmpl, values);
    test_result("Empty key left as literal",
                str_eq(result, "Hello {{}}!"));

    // Unclosed placeholder - left as-is
    tmpl = rt_const_cstr("Hello {{name");
    result = rt_template_render(tmpl, values);
    test_result("Unclosed placeholder",
                str_eq(result, "Hello {{name"));

    // Adjacent placeholders
    tmpl = rt_const_cstr("{{name}}{{count}}");
    result = rt_template_render(tmpl, values);
    test_result("Adjacent placeholders",
                str_eq(result, "Alice5"));

    // Placeholder at start
    tmpl = rt_const_cstr("{{name}} is here");
    result = rt_template_render(tmpl, values);
    test_result("Placeholder at start",
                str_eq(result, "Alice is here"));

    // Placeholder at end
    tmpl = rt_const_cstr("User: {{name}}");
    result = rt_template_render(tmpl, values);
    test_result("Placeholder at end",
                str_eq(result, "User: Alice"));

    printf("\n");
}

//=============================================================================
// RenderSeq Tests (Positional)
//=============================================================================

static void test_render_seq()
{
    printf("Testing Template.RenderSeq:\n");

    void *values = rt_seq_new();
    rt_seq_push(values, rt_box_str(make_str("Alice")));
    rt_seq_push(values, rt_box_str(make_str("Bob")));
    rt_seq_push(values, rt_box_str(make_str("Charlie")));

    // Basic positional substitution
    rt_string tmpl = rt_const_cstr("{{0}} and {{1}} meet {{2}}");
    rt_string result = rt_template_render_seq(tmpl, values);
    test_result("Positional substitution",
                str_eq(result, "Alice and Bob meet Charlie"));

    // Same index multiple times
    tmpl = rt_const_cstr("{{0}}, {{0}}, {{0}}!");
    result = rt_template_render_seq(tmpl, values);
    test_result("Same index multiple times",
                str_eq(result, "Alice, Alice, Alice!"));

    // Out of range index left as-is
    tmpl = rt_const_cstr("{{0}} and {{99}}");
    result = rt_template_render_seq(tmpl, values);
    test_result("Out of range left as-is",
                str_eq(result, "Alice and {{99}}"));

    // Non-numeric key left as-is
    tmpl = rt_const_cstr("{{abc}} and {{0}}");
    result = rt_template_render_seq(tmpl, values);
    test_result("Non-numeric key left as-is",
                str_eq(result, "{{abc}} and Alice"));

    // Negative number (not valid index)
    tmpl = rt_const_cstr("{{-1}} and {{0}}");
    result = rt_template_render_seq(tmpl, values);
    test_result("Negative number left as-is",
                str_eq(result, "{{-1}} and Alice"));

    printf("\n");
}

//=============================================================================
// RenderWith Tests (Custom Delimiters)
//=============================================================================

static void test_render_with()
{
    printf("Testing Template.RenderWith:\n");

    void *values = rt_map_new();
    rt_map_set(values, rt_const_cstr("name"), rt_box_str(make_str("Alice")));
    rt_map_set(values, rt_const_cstr("count"), rt_box_str(make_str("5")));

    // Dollar sign delimiters
    rt_string tmpl = rt_const_cstr("Hello $name$!");
    rt_string result = rt_template_render_with(tmpl, values,
                                               rt_const_cstr("$"), rt_const_cstr("$"));
    test_result("Dollar delimiters",
                str_eq(result, "Hello Alice!"));

    // Percent delimiters
    tmpl = rt_const_cstr("Hello %name%!");
    result = rt_template_render_with(tmpl, values,
                                     rt_const_cstr("%"), rt_const_cstr("%"));
    test_result("Percent delimiters",
                str_eq(result, "Hello Alice!"));

    // HTML-style delimiters
    tmpl = rt_const_cstr("<%= name %> has <%= count %> items");
    result = rt_template_render_with(tmpl, values,
                                     rt_const_cstr("<%="), rt_const_cstr("%>"));
    test_result("HTML-style delimiters",
                str_eq(result, "Alice has 5 items"));

    // Single char prefix, multi-char suffix
    tmpl = rt_const_cstr("Hello $name}}!");
    result = rt_template_render_with(tmpl, values,
                                     rt_const_cstr("$"), rt_const_cstr("}}"));
    test_result("Mixed delimiter lengths",
                str_eq(result, "Hello Alice!"));

    printf("\n");
}

//=============================================================================
// Has Tests
//=============================================================================

static void test_has()
{
    printf("Testing Template.Has:\n");

    rt_string tmpl = rt_const_cstr("Hello {{name}}, you have {{count}} messages.");

    test_result("Has 'name'",
                rt_template_has(tmpl, rt_const_cstr("name")));

    test_result("Has 'count'",
                rt_template_has(tmpl, rt_const_cstr("count")));

    test_result("Not has 'unknown'",
                !rt_template_has(tmpl, rt_const_cstr("unknown")));

    test_result("Not has empty key",
                !rt_template_has(tmpl, rt_const_cstr("")));

    // With whitespace in template
    tmpl = rt_const_cstr("Hello {{ name }}!");
    test_result("Has with whitespace",
                rt_template_has(tmpl, rt_const_cstr("name")));

    // Empty template
    tmpl = rt_const_cstr("");
    test_result("Empty template has nothing",
                !rt_template_has(tmpl, rt_const_cstr("name")));

    printf("\n");
}

//=============================================================================
// Keys Tests
//=============================================================================

static void test_keys()
{
    printf("Testing Template.Keys:\n");

    // Multiple unique keys
    rt_string tmpl = rt_const_cstr("{{name}} {{age}} {{city}}");
    void *keys = rt_template_keys(tmpl);
    test_result("Keys count = 3", rt_bag_len(keys) == 3);
    test_result("Keys contains 'name'", bag_contains(keys, "name"));
    test_result("Keys contains 'age'", bag_contains(keys, "age"));
    test_result("Keys contains 'city'", bag_contains(keys, "city"));

    // Duplicate keys
    tmpl = rt_const_cstr("{{name}} and {{name}} again");
    keys = rt_template_keys(tmpl);
    test_result("Duplicate keys count = 1", rt_bag_len(keys) == 1);
    test_result("Keys contains 'name'", bag_contains(keys, "name"));

    // No placeholders
    tmpl = rt_const_cstr("No placeholders here");
    keys = rt_template_keys(tmpl);
    test_result("No placeholders = empty bag", rt_bag_len(keys) == 0);

    // Empty placeholder ignored
    tmpl = rt_const_cstr("Hello {{}}!");
    keys = rt_template_keys(tmpl);
    test_result("Empty key not in bag", rt_bag_len(keys) == 0);

    // With whitespace
    tmpl = rt_const_cstr("{{ name }} and {{ age }}");
    keys = rt_template_keys(tmpl);
    test_result("Whitespace trimmed, count = 2", rt_bag_len(keys) == 2);
    test_result("Contains trimmed 'name'", bag_contains(keys, "name"));
    test_result("Contains trimmed 'age'", bag_contains(keys, "age"));

    printf("\n");
}

//=============================================================================
// Escape Tests
//=============================================================================

static void test_escape()
{
    printf("Testing Template.Escape:\n");

    // Escape opening braces
    rt_string text = rt_const_cstr("Use {{name}} for placeholders");
    rt_string result = rt_template_escape(text);
    test_result("Escape {{ and }}",
                str_eq(result, "Use {{{{name}}}} for placeholders"));

    // No special chars
    text = rt_const_cstr("No braces here");
    result = rt_template_escape(text);
    test_result("No braces unchanged",
                str_eq(result, "No braces here"));

    // Only opening braces
    text = rt_const_cstr("{{");
    result = rt_template_escape(text);
    test_result("Just {{ escaped",
                str_eq(result, "{{{{"));

    // Only closing braces
    text = rt_const_cstr("}}");
    result = rt_template_escape(text);
    test_result("Just }} escaped",
                str_eq(result, "}}}}"));

    // Mixed single braces (not escaped)
    text = rt_const_cstr("{ } { }");
    result = rt_template_escape(text);
    test_result("Single braces not escaped",
                str_eq(result, "{ } { }"));

    // Empty string
    text = rt_const_cstr("");
    result = rt_template_escape(text);
    test_result("Empty string",
                str_eq(result, ""));

    // Multiple pairs
    text = rt_const_cstr("{{a}} and {{b}}");
    result = rt_template_escape(text);
    test_result("Multiple pairs",
                str_eq(result, "{{{{a}}}} and {{{{b}}}}"));

    printf("\n");
}

//=============================================================================
// Edge Cases
//=============================================================================

static void test_edge_cases()
{
    printf("Testing Edge Cases:\n");

    void *values = rt_map_new();
    rt_map_set(values, rt_const_cstr("x"), rt_box_str(make_str("X")));

    // Just a placeholder
    rt_string tmpl = rt_const_cstr("{{x}}");
    rt_string result = rt_template_render(tmpl, values);
    test_result("Just a placeholder",
                str_eq(result, "X"));

    // Empty value
    rt_map_set(values, rt_const_cstr("empty"), rt_box_str(make_str("")));
    tmpl = rt_const_cstr("Hello {{empty}}!");
    result = rt_template_render(tmpl, values);
    test_result("Empty value",
                str_eq(result, "Hello !"));

    // Value with braces
    rt_map_set(values, rt_const_cstr("braces"), rt_box_str(make_str("{{content}}")));
    tmpl = rt_const_cstr("Result: {{braces}}");
    result = rt_template_render(tmpl, values);
    test_result("Value with braces",
                str_eq(result, "Result: {{content}}"));

    // Long template
    rt_string_builder sb;
    rt_sb_init(&sb);
    for (int i = 0; i < 100; i++)
    {
        rt_sb_append_cstr(&sb, "{{x}}");
    }
    rt_string long_tmpl = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    result = rt_template_render(long_tmpl, values);
    // Should be 100 X's
    const char *res_str = rt_string_cstr(result);
    bool all_x = (strlen(res_str) == 100);
    for (size_t i = 0; i < 100 && all_x; i++)
    {
        if (res_str[i] != 'X')
            all_x = false;
    }
    test_result("Long template (100 placeholders)", all_x);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Template Tests ===\n\n");

    test_render();
    test_render_seq();
    test_render_with();
    test_has();
    test_keys();
    test_escape();
    test_edge_cases();

    printf("All Template tests passed!\n");
    return 0;
}
