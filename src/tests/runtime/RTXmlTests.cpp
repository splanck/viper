//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTXmlTests.cpp
// Purpose: Validate Viper.Data.Xml (rt_xml_*) parse/create/format/query API.
// Key invariants: Parse produces correct node tree; attributes are readable;
//                 formatted output round-trips through parse.
// Ownership/Lifetime: XML node objects are refcounted; parent links retain children.
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_object.h"
#include "rt_string.h"
#include "rt_xml.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int ok) {
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static rt_string S(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static int str_eq_c(rt_string s, const char *expected) {
    size_t elen = strlen(expected);
    rt_string exp = rt_string_from_bytes(expected, elen);
    int result = rt_str_eq(s, exp);
    rt_string_unref(exp);
    return result;
}

static int str_eq_bytes(rt_string s, const char *expected, size_t len) {
    return s && (size_t)rt_str_len(s) == len && memcmp(rt_string_cstr(s), expected, len) == 0;
}

static int str_contains_c(rt_string s, const char *needle) {
    const char *cstr = rt_string_cstr(s);
    if (!cstr)
        return 0;
    return strstr(cstr, needle) != NULL;
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_parse_simple(void) {
    printf("rt_xml_parse simple:\n");
    rt_string xml = S("<root><child name=\"hello\">world</child></root>");
    void *doc = rt_xml_parse(xml);
    rt_string_unref(xml);
    check("parse returns non-null", doc != NULL);
    check("doc type == document", rt_xml_node_type(doc) == XML_NODE_DOCUMENT);

    void *root = rt_xml_root(doc);
    check("root non-null", root != NULL);

    rt_string tag = rt_xml_tag(root);
    check("root tag == 'root'", str_eq_c(tag, "root"));
    rt_string_unref(tag);

    check("root has 1 child", rt_xml_child_count(root) == 1);

    rt_string child_tag = S("child");
    void *child = rt_xml_child(root, child_tag);
    rt_string_unref(child_tag);
    check("child element non-null", child != NULL);

    rt_string name_attr_key = S("name");
    rt_string attr_val = rt_xml_attr(child, name_attr_key);
    rt_string_unref(name_attr_key);
    check("child name attr == 'hello'", str_eq_c(attr_val, "hello"));
    rt_string_unref(attr_val);

    rt_string text_content = rt_xml_text_content(child);
    check("child text content == 'world'", str_eq_c(text_content, "world"));
    rt_string_unref(text_content);

    // Child references are borrowed from the parsed document; releasing the
    // document later releases the tree.
}

static void test_is_valid(void) {
    printf("rt_xml_is_valid:\n");
    rt_string valid = S("<a><b/></a>");
    rt_string invalid = S("<a><b></a>");
    rt_string text = S("not xml");
    rt_string multi = S("<a/><b/>");
    rt_string raw_amp = S("<a>Tom & Jerry</a>");
    rt_string dup_attr = S("<a id=\"1\" id=\"2\"/>");
    check("valid XML returns 1", rt_xml_is_valid(valid));
    check("invalid XML returns 0", !rt_xml_is_valid(invalid));
    check("top-level text is invalid XML", !rt_xml_is_valid(text));
    check("multiple roots are invalid XML", !rt_xml_is_valid(multi));
    check("raw ampersand is invalid XML", !rt_xml_is_valid(raw_amp));
    check("duplicate attributes are invalid XML", !rt_xml_is_valid(dup_attr));
    rt_string_unref(valid);
    rt_string_unref(invalid);
    rt_string_unref(text);
    rt_string_unref(multi);
    rt_string_unref(raw_amp);
    rt_string_unref(dup_attr);
}

static void test_create_and_format(void) {
    printf("rt_xml create and format:\n");
    rt_string root_tag = S("person");
    void *elem = rt_xml_element(root_tag);
    rt_string_unref(root_tag);
    check("element non-null", elem != NULL);

    rt_string name_key = S("name");
    rt_string name_val = S("Alice");
    rt_xml_set_attr(elem, name_key, name_val);
    rt_string_unref(name_key);
    rt_string_unref(name_val);

    rt_string text_content = S("Hello");
    rt_xml_set_text(elem, text_content);
    rt_string_unref(text_content);

    rt_string formatted = rt_xml_format(elem);
    check("format non-empty", rt_str_len(formatted) > 0);
    check("format contains tag", str_contains_c(formatted, "person"));
    check("format contains attr", str_contains_c(formatted, "name"));
    check("format contains text", str_contains_c(formatted, "Hello"));
    rt_string_unref(formatted);
}

static void test_escape_unescape(void) {
    printf("rt_xml escape / unescape:\n");
    rt_string special = S("a < b & c > d \"quoted\" 'apos'");
    rt_string escaped = rt_xml_escape(special);
    check("escaped contains &lt;", str_contains_c(escaped, "&lt;"));
    check("escaped contains &amp;", str_contains_c(escaped, "&amp;"));
    check("escaped contains &gt;", str_contains_c(escaped, "&gt;"));
    check("escaped contains &quot;", str_contains_c(escaped, "&quot;"));
    check("escaped contains &apos;", str_contains_c(escaped, "&apos;"));

    rt_string unescaped = rt_xml_unescape(escaped);
    check("unescape roundtrip", rt_str_eq(special, unescaped));

    const char entity_after_nul[] = {'a', '\0', '&', 'a', 'm', 'p', ';', 'b'};
    const char unescaped_bytes[] = {'a', '\0', '&', 'b'};
    rt_string with_nul = rt_string_from_bytes(entity_after_nul, sizeof(entity_after_nul));
    rt_string unescaped_nul = rt_xml_unescape(with_nul);
    check("unescape scans past embedded NUL",
          str_eq_bytes(unescaped_nul, unescaped_bytes, sizeof(unescaped_bytes)));

    rt_string_unref(unescaped_nul);
    rt_string_unref(with_nul);
    rt_string_unref(unescaped);
    rt_string_unref(escaped);
    rt_string_unref(special);
}

static void test_children(void) {
    printf("rt_xml children manipulation:\n");
    rt_string parent_tag = S("list");
    void *parent = rt_xml_element(parent_tag);
    rt_string_unref(parent_tag);

    for (int i = 0; i < 3; ++i) {
        rt_string item_tag = S("item");
        void *item = rt_xml_element(item_tag);
        rt_string_unref(item_tag);
        rt_xml_append(parent, item);
    }

    check("child count == 3", rt_xml_child_count(parent) == 3);

    void *first_child = rt_xml_child_at(parent, 0);
    check("child_at(0) non-null", first_child != NULL);

    rt_string item_tag2 = S("item");
    rt_string first_tag = rt_xml_tag(first_child);
    check("child_at(0) tag == 'item'", rt_str_eq(first_tag, item_tag2));
    rt_string_unref(first_tag);
    rt_string_unref(item_tag2);
}

static void test_root_and_path_find(void) {
    printf("rt_xml root and path find:\n");
    rt_string xml = S("<catalog><book id=\"1\"><title>Primer</title></book></catalog>");
    void *doc = rt_xml_parse(xml);
    rt_string_unref(xml);
    check("parse returns doc", doc != NULL);

    void *root = rt_xml_root(doc);
    rt_string book_tag = S("book");
    void *book = rt_xml_child(root, book_tag);
    rt_string_unref(book_tag);
    check("book child found", book != NULL);

    void *root_from_book = rt_xml_root(book);
    rt_string root_tag = rt_xml_tag(root_from_book);
    check("root works from descendant", str_eq_c(root_tag, "catalog"));
    rt_string_unref(root_tag);

    rt_string path = S("catalog/book/title");
    void *title = rt_xml_find(doc, path);
    rt_string_unref(path);
    rt_string title_text = rt_xml_text_content(title);
    check("path find returns title", str_eq_c(title_text, "Primer"));
    rt_string_unref(title_text);
}

static void test_invalid_node_creation(void) {
    printf("rt_xml invalid node creation:\n");
    rt_string bad_tag = S("1bad");
    check("invalid element name returns null", rt_xml_element(bad_tag) == NULL);
    rt_string_unref(bad_tag);

    rt_string comment = S("bad--comment");
    check("invalid comment returns null", rt_xml_comment(comment) == NULL);
    rt_string_unref(comment);

    rt_string cdata = S("bad]]>cdata");
    check("invalid cdata returns null", rt_xml_cdata(cdata) == NULL);
    rt_string_unref(cdata);
}

static void test_whitespace_doctype_and_invalid_chars(void) {
    printf("rt_xml whitespace, doctype, invalid chars:\n");

    rt_string xml = S("<root> </root>");
    void *doc = rt_xml_parse(xml);
    rt_string_unref(xml);
    check("whitespace text doc parses", doc != NULL);
    void *root = rt_xml_root(doc);
    check("whitespace text node is preserved", rt_xml_child_count(root) == 1);
    rt_string text = rt_xml_text_content(root);
    check("whitespace text content preserved", str_eq_c(text, " "));
    rt_string_unref(text);
    release_obj(doc);

    rt_string with_doctype = S("<!DOCTYPE root [<!ELEMENT root (#PCDATA)>]><root>ok</root>");
    doc = rt_xml_parse(with_doctype);
    rt_string_unref(with_doctype);
    check("doctype internal subset parses", doc != NULL);
    root = rt_xml_root(doc);
    text = rt_xml_text_content(root);
    check("doctype document text content", str_eq_c(text, "ok"));
    rt_string_unref(text);
    release_obj(doc);

    const char invalid_bytes[] = {
        '<', 'r', 'o', 'o', 't', '>', '\x01', '<', '/', 'r', 'o', 'o', 't', '>'};
    rt_string invalid = rt_string_from_bytes(invalid_bytes, sizeof(invalid_bytes));
    check("invalid XML control char rejected", !rt_xml_is_valid(invalid));
    rt_string_unref(invalid);
}

static void test_retained_attr_and_appended_child(void) {
    printf("rt_xml retained attr and appended child:\n");

    rt_string xml = S("<root id=\"42\"/>");
    void *doc = rt_xml_parse(xml);
    rt_string_unref(xml);
    check("attr retention doc parses", doc != NULL);
    void *root = rt_xml_root(doc);
    rt_string id_key = S("id");
    rt_string attr = rt_xml_attr(root, id_key);
    check("attr value before caller release", str_eq_c(attr, "42"));
    rt_string_unref(attr);
    attr = rt_xml_attr(root, id_key);
    check("attr value survives caller release", str_eq_c(attr, "42"));
    rt_string_unref(attr);
    rt_string_unref(id_key);
    release_obj(doc);

    rt_string parent_tag = S("parent");
    rt_string child_tag = S("child");
    void *parent = rt_xml_element(parent_tag);
    void *child = rt_xml_element(child_tag);
    rt_string_unref(parent_tag);
    rt_string_unref(child_tag);
    rt_xml_append(parent, child);
    release_obj(child);
    rt_string formatted = rt_xml_format(parent);
    check("append retains child for parent", str_contains_c(formatted, "<child/>"));
    rt_string_unref(formatted);
    release_obj(parent);
}

static void test_mixed_content_pretty_round_trip(void) {
    printf("rt_xml mixed content pretty formatting:\n");

    rt_string xml = S("<p>Hello <b>world</b>!</p>");
    void *doc = rt_xml_parse(xml);
    rt_string_unref(xml);
    check("mixed content parses", doc != NULL);
    void *root = rt_xml_root(doc);

    rt_string pretty = rt_xml_format_pretty(root, 2);
    check("mixed content stays inline", str_eq_c(pretty, "<p>Hello <b>world</b>!</p>"));

    void *round_trip = rt_xml_parse(pretty);
    check("pretty mixed content reparses", round_trip != NULL);
    rt_string text = rt_xml_text_content(rt_xml_root(round_trip));
    check("pretty mixed content text unchanged", str_eq_c(text, "Hello world!"));

    rt_string_unref(text);
    rt_string_unref(pretty);
    release_obj(round_trip);
    release_obj(doc);
}

int main(void) {
    printf("=== RTXmlTests ===\n");
    test_parse_simple();
    test_is_valid();
    test_create_and_format();
    test_escape_unescape();
    test_children();
    test_root_and_path_find();
    test_invalid_node_creation();
    test_whitespace_doctype_and_invalid_chars();
    test_retained_attr_and_appended_child();
    test_mixed_content_pretty_round_trip();
    printf("All XML tests passed.\n");
    return 0;
}
