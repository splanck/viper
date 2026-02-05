//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_html.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_escape()
{
    rt_string input = rt_string_from_bytes("<b>Hello & \"World\"</b>", 22);
    rt_string result = rt_html_escape(input);
    const char *s = rt_string_cstr(result);
    assert(strcmp(s, "&lt;b&gt;Hello &amp; &quot;World&quot;&lt;/b&gt;") == 0);
}

static void test_escape_single_quotes()
{
    rt_string input = rt_string_from_bytes("it's", 4);
    rt_string result = rt_html_escape(input);
    assert(strcmp(rt_string_cstr(result), "it&#39;s") == 0);
}

static void test_unescape()
{
    rt_string input = rt_string_from_bytes("&lt;b&gt;Hello&lt;/b&gt;", 24);
    rt_string result = rt_html_unescape(input);
    assert(strcmp(rt_string_cstr(result), "<b>Hello</b>") == 0);
}

static void test_unescape_numeric()
{
    rt_string input = rt_string_from_bytes("&#65;&#x42;", 11);
    rt_string result = rt_html_unescape(input);
    assert(strcmp(rt_string_cstr(result), "AB") == 0);
}

static void test_unescape_nbsp()
{
    rt_string input = rt_string_from_bytes("a&nbsp;b", 8);
    rt_string result = rt_html_unescape(input);
    assert(strcmp(rt_string_cstr(result), "a b") == 0);
}

static void test_strip_tags()
{
    rt_string input = rt_string_from_bytes("<p>Hello <b>World</b></p>", 25);
    rt_string result = rt_html_strip_tags(input);
    assert(strcmp(rt_string_cstr(result), "Hello World") == 0);
}

static void test_to_text()
{
    rt_string input = rt_string_from_bytes("<p>Hello &amp; World</p>", 24);
    rt_string result = rt_html_to_text(input);
    assert(strcmp(rt_string_cstr(result), "Hello & World") == 0);
}

static void test_extract_links()
{
    rt_string input = rt_string_from_bytes(
        "<a href=\"https://example.com\">Example</a> text <a href='https://test.org'>Test</a>",
        82);
    void *links = rt_html_extract_links(input);
    assert(rt_seq_len(links) == 2);

    rt_string link0 = (rt_string)rt_seq_get(links, 0);
    assert(strcmp(rt_string_cstr(link0), "https://example.com") == 0);

    rt_string link1 = (rt_string)rt_seq_get(links, 1);
    assert(strcmp(rt_string_cstr(link1), "https://test.org") == 0);
}

static void test_extract_text()
{
    rt_string input = rt_string_from_bytes(
        "<h1>Title</h1><p>Para 1</p><p>Para 2</p>", 41);
    rt_string tag = rt_string_from_bytes("p", 1);
    void *texts = rt_html_extract_text(input, tag);
    assert(rt_seq_len(texts) == 2);

    rt_string t0 = (rt_string)rt_seq_get(texts, 0);
    assert(strcmp(rt_string_cstr(t0), "Para 1") == 0);

    rt_string t1 = (rt_string)rt_seq_get(texts, 1);
    assert(strcmp(rt_string_cstr(t1), "Para 2") == 0);
}

static void test_parse_basic()
{
    rt_string input = rt_string_from_bytes("<div><p>Hello</p></div>", 23);
    void *root = rt_html_parse(input);
    assert(root != NULL);

    // Root should have children
    rt_string children_key = rt_const_cstr("children");
    void *children = rt_map_get(root, children_key);
    assert(children != NULL);
    assert(rt_seq_len(children) >= 1);
}

static void test_null_safety()
{
    rt_string empty_result = rt_html_escape(NULL);
    assert(strcmp(rt_string_cstr(empty_result), "") == 0);

    empty_result = rt_html_unescape(NULL);
    assert(strcmp(rt_string_cstr(empty_result), "") == 0);

    empty_result = rt_html_strip_tags(NULL);
    assert(strcmp(rt_string_cstr(empty_result), "") == 0);

    void *links = rt_html_extract_links(NULL);
    assert(rt_seq_len(links) == 0);

    void *texts = rt_html_extract_text(NULL, NULL);
    assert(rt_seq_len(texts) == 0);

    void *root = rt_html_parse(NULL);
    assert(root != NULL); // returns empty root node
}

static void test_roundtrip_escape_unescape()
{
    rt_string original = rt_string_from_bytes("Hello <World> & \"Friends\"", 25);
    rt_string escaped = rt_html_escape(original);
    rt_string unescaped = rt_html_unescape(escaped);
    assert(strcmp(rt_string_cstr(unescaped), "Hello <World> & \"Friends\"") == 0);
}

int main()
{
    test_escape();
    test_escape_single_quotes();
    test_unescape();
    test_unescape_numeric();
    test_unescape_nbsp();
    test_strip_tags();
    test_to_text();
    test_extract_links();
    test_extract_text();
    test_parse_basic();
    test_null_safety();
    test_roundtrip_escape_unescape();
    return 0;
}
