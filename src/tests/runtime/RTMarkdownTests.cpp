//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_markdown.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void test_heading()
{
    rt_string md = make_str("# Hello World");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<h1>") != NULL);
    assert(strstr(rt_string_cstr(html), "Hello World") != NULL);
    assert(strstr(rt_string_cstr(html), "</h1>") != NULL);
}

static void test_heading_levels()
{
    rt_string md = make_str("## Second\n### Third");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<h2>Second</h2>") != NULL);
    assert(strstr(rt_string_cstr(html), "<h3>Third</h3>") != NULL);
}

static void test_bold()
{
    rt_string md = make_str("This is **bold** text");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<strong>bold</strong>") != NULL);
}

static void test_italic()
{
    rt_string md = make_str("This is *italic* text");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<em>italic</em>") != NULL);
}

static void test_inline_code()
{
    rt_string md = make_str("Use `printf` here");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<code>printf</code>") != NULL);
}

static void test_link()
{
    rt_string md = make_str("Visit [Google](https://google.com) now");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<a href=\"https://google.com\">Google</a>") != NULL);
}

static void test_list()
{
    rt_string md = make_str("- Item 1\n- Item 2\n- Item 3");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<ul>") != NULL);
    assert(strstr(rt_string_cstr(html), "<li>Item 1</li>") != NULL);
    assert(strstr(rt_string_cstr(html), "<li>Item 2</li>") != NULL);
    assert(strstr(rt_string_cstr(html), "</ul>") != NULL);
}

static void test_paragraph()
{
    rt_string md = make_str("Hello world");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<p>Hello world</p>") != NULL);
}

static void test_code_block()
{
    rt_string md = make_str("```\nint x = 5;\nreturn x;\n```");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<pre><code>") != NULL);
    assert(strstr(rt_string_cstr(html), "int x = 5;") != NULL);
    assert(strstr(rt_string_cstr(html), "</code></pre>") != NULL);
}

static void test_to_text()
{
    rt_string md = make_str("# Title\n**bold** and *italic*\n[link](http://x.com)");
    rt_string text = rt_markdown_to_text(md);
    const char *t = rt_string_cstr(text);
    // Should contain "Title" without "#"
    assert(strstr(t, "Title") != NULL);
    assert(strstr(t, "#") == NULL);
    // Should contain "bold" without "**"
    assert(strstr(t, "bold") != NULL);
    // Should contain "link" but not the URL
    assert(strstr(t, "link") != NULL);
}

static void test_extract_links()
{
    rt_string md = make_str("See [A](http://a.com) and [B](http://b.com)");
    void *links = rt_markdown_extract_links(md);
    assert(rt_seq_len(links) == 2);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(links, 0)), "http://a.com") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(links, 1)), "http://b.com") == 0);
}

static void test_extract_headings()
{
    rt_string md = make_str("# First\nText\n## Second\nMore text\n### Third");
    void *headings = rt_markdown_extract_headings(md);
    assert(rt_seq_len(headings) == 3);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(headings, 0)), "First") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(headings, 1)), "Second") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(headings, 2)), "Third") == 0);
}

static void test_null_safety()
{
    rt_string html = rt_markdown_to_html(NULL);
    assert(strlen(rt_string_cstr(html)) == 0);

    rt_string text = rt_markdown_to_text(NULL);
    assert(strlen(rt_string_cstr(text)) == 0);

    void *links = rt_markdown_extract_links(NULL);
    assert(rt_seq_len(links) == 0);

    void *headings = rt_markdown_extract_headings(NULL);
    assert(rt_seq_len(headings) == 0);
}

int main()
{
    test_heading();
    test_heading_levels();
    test_bold();
    test_italic();
    test_inline_code();
    test_link();
    test_list();
    test_paragraph();
    test_code_block();
    test_to_text();
    test_extract_links();
    test_extract_headings();
    test_null_safety();
    return 0;
}
