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

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void test_heading() {
    rt_string md = make_str("# Hello World");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<h1>") != NULL);
    assert(strstr(rt_string_cstr(html), "Hello World") != NULL);
    assert(strstr(rt_string_cstr(html), "</h1>") != NULL);
}

static void test_heading_levels() {
    rt_string md = make_str("## Second\n### Third");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<h2>Second</h2>") != NULL);
    assert(strstr(rt_string_cstr(html), "<h3>Third</h3>") != NULL);
}

static void test_invalid_heading_markers_are_paragraphs() {
    rt_string md = make_str("#NoSpace\n####### Too deep");
    rt_string html = rt_markdown_to_html(md);
    const char *out = rt_string_cstr(html);
    assert(strstr(out, "<h1>") == NULL);
    assert(strstr(out, "<h6>") == NULL);
    assert(strstr(out, "<p>#NoSpace</p>") != NULL);
    assert(strstr(out, "<p>####### Too deep</p>") != NULL);

    rt_string text = rt_markdown_to_text(md);
    assert(strcmp(rt_string_cstr(text), "#NoSpace\n####### Too deep") == 0);

    void *headings = rt_markdown_extract_headings(md);
    assert(rt_seq_len(headings) == 0);
}

static void test_bold() {
    rt_string md = make_str("This is **bold** text");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<strong>bold</strong>") != NULL);
}

static void test_italic() {
    rt_string md = make_str("This is *italic* text");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<em>italic</em>") != NULL);
}

static void test_inline_code() {
    rt_string md = make_str("Use `printf` here");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<code>printf</code>") != NULL);
}

static void test_link() {
    rt_string md = make_str("Visit [Google](https://google.com) now");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<a href=\"https://google.com\">Google</a>") != NULL);
}

static void test_link_url_attribute_escaping() {
    rt_string md = make_str("Visit [x](https://example.test/\" onclick=\"evil) now");
    rt_string html = rt_markdown_to_html(md);
    const char *out = rt_string_cstr(html);
    assert(strstr(out, "href=\"https://example.test/&quot; onclick=&quot;evil\"") != NULL);
    assert(strstr(out, "onclick=\"evil") == NULL);
}

static void test_link_url_leading_space_scheme_blocked() {
    rt_string md = make_str("[x](  javascript:alert(1))");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<a href=\"#\">x</a>") != NULL);
}

static void test_list() {
    rt_string md = make_str("- Item 1\n- Item 2\n- Item 3");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<ul>") != NULL);
    assert(strstr(rt_string_cstr(html), "<li>Item 1</li>") != NULL);
    assert(strstr(rt_string_cstr(html), "<li>Item 2</li>") != NULL);
    assert(strstr(rt_string_cstr(html), "</ul>") != NULL);
}

static void test_paragraph() {
    rt_string md = make_str("Hello world");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<p>Hello world</p>") != NULL);
}

static void test_unmatched_inline_markers_are_literal() {
    rt_string md = make_str("This is **not closed and `code");
    rt_string html = rt_markdown_to_html(md);
    const char *out = rt_string_cstr(html);
    assert(strstr(out, "<strong>") == NULL);
    assert(strstr(out, "<code>") == NULL);
    assert(strstr(out, "**not closed") != NULL);
    assert(strstr(out, "`code") != NULL);
}

static void test_code_block() {
    rt_string md = make_str("```\nint x = 5;\nreturn x;\n```");
    rt_string html = rt_markdown_to_html(md);
    assert(strstr(rt_string_cstr(html), "<pre><code>") != NULL);
    assert(strstr(rt_string_cstr(html), "int x = 5;") != NULL);
    assert(strstr(rt_string_cstr(html), "</code></pre>") != NULL);
}

static void test_to_text() {
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

static void test_to_text_does_not_add_final_newline() {
    rt_string md = make_str("plain");
    rt_string text = rt_markdown_to_text(md);
    assert(rt_str_len(text) == 5);
    assert(strcmp(rt_string_cstr(text), "plain") == 0);

    md = make_str("# Title\nNext");
    text = rt_markdown_to_text(md);
    assert(strcmp(rt_string_cstr(text), "Title\nNext") == 0);
}

static void test_to_text_preserves_malformed_link_and_strips_underscore() {
    rt_string md = make_str("This is _italic_ and [broken");
    rt_string text = rt_markdown_to_text(md);
    assert(strcmp(rt_string_cstr(text), "This is italic and [broken") == 0);
}

static void test_extract_links() {
    rt_string md = make_str("See [A](http://a.com) and [B](http://b.com)");
    void *links = rt_markdown_extract_links(md);
    assert(rt_seq_len(links) == 2);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(links, 0)), "http://a.com") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(links, 1)), "http://b.com") == 0);
}

static void test_extract_headings() {
    rt_string md = make_str("# First\nText\n## Second\nMore text\n### Third");
    void *headings = rt_markdown_extract_headings(md);
    assert(rt_seq_len(headings) == 3);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(headings, 0)), "First") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(headings, 1)), "Second") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(headings, 2)), "Third") == 0);
}

static void test_null_safety() {
    rt_string html = rt_markdown_to_html(NULL);
    assert(strlen(rt_string_cstr(html)) == 0);

    rt_string text = rt_markdown_to_text(NULL);
    assert(strlen(rt_string_cstr(text)) == 0);

    void *links = rt_markdown_extract_links(NULL);
    assert(rt_seq_len(links) == 0);

    void *headings = rt_markdown_extract_headings(NULL);
    assert(rt_seq_len(headings) == 0);
}

/// @brief Main.
static void test_hr_and_list_transitions() {
    // VDOC-049: spaced rules render as <hr>, not list items, and an open list
    // closes before a heading.
    rt_string hr1 = rt_markdown_to_html(make_str("* * *"));
    assert(strstr(rt_string_cstr(hr1), "<hr>") != NULL);
    assert(strstr(rt_string_cstr(hr1), "<li>") == NULL);
    rt_string hr2 = rt_markdown_to_html(make_str("- - -"));
    assert(strstr(rt_string_cstr(hr2), "<hr>") != NULL);

    rt_string mixed = rt_markdown_to_html(make_str("- item\n# Heading"));
    const char *out = rt_string_cstr(mixed);
    const char *ul_close = strstr(out, "</ul>");
    const char *h1 = strstr(out, "<h1>");
    assert(ul_close != NULL && h1 != NULL && ul_close < h1);
}

static void test_crlf_matches_lf() {
    // VDOC-050: CRLF input renders identically to LF input; no CR bytes leak
    // into content.
    rt_string crlf = rt_markdown_to_html(make_str("# Heading\r\ntext\r\n"));
    rt_string lf = rt_markdown_to_html(make_str("# Heading\ntext\n"));
    assert(strcmp(rt_string_cstr(crlf), rt_string_cstr(lf)) == 0);
    assert(strchr(rt_string_cstr(crlf), '\r') == NULL);

    void *heads = rt_markdown_extract_headings(make_str("# One\r\n## Two\r\n"));
    rt_string first = (rt_string)rt_seq_get(heads, 0);
    assert(strcmp(rt_string_cstr(first), "One") == 0);
}

static void test_to_text_preserves_intraword_and_unmatched_markers() {
    // VDOC-051: literal underscores in identifiers survive ToText; only
    // matched emphasis pairs are stripped, and unmatched markers stay.
    rt_string text = rt_markdown_to_text(make_str("use snake_case and file_name_here"));
    assert(strcmp(rt_string_cstr(text), "use snake_case and file_name_here") == 0);

    text = rt_markdown_to_text(make_str("**bold** and `code` and *em*"));
    assert(strcmp(rt_string_cstr(text), "bold and code and em") == 0);

    text = rt_markdown_to_text(make_str("2 * 3 equals 6"));
    assert(strstr(rt_string_cstr(text), "2 * 3") != NULL);

    text = rt_markdown_to_text(make_str("lone ` backtick and trailing _"));
    assert(strcmp(rt_string_cstr(text), "lone ` backtick and trailing _") == 0);
}

int main() {
    test_hr_and_list_transitions();
    test_crlf_matches_lf();
    test_to_text_preserves_intraword_and_unmatched_markers();
    test_heading();
    test_heading_levels();
    test_invalid_heading_markers_are_paragraphs();
    test_bold();
    test_italic();
    test_inline_code();
    test_link();
    test_link_url_attribute_escaping();
    test_link_url_leading_space_scheme_blocked();
    test_list();
    test_paragraph();
    test_unmatched_inline_markers_are_literal();
    test_code_block();
    test_to_text();
    test_to_text_does_not_add_final_newline();
    test_to_text_preserves_malformed_link_and_strips_underscore();
    test_extract_links();
    test_extract_headings();
    test_null_safety();
    return 0;
}
