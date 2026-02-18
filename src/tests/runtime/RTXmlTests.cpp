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
// Ownership/Lifetime: XML node objects use tree-based ownership; do not call
//                     rt_obj_release_check0/rt_obj_free on them.
//
//===----------------------------------------------------------------------===//

#include "viper/runtime/rt.h"

#include "rt_string.h"
#include "rt_xml.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int ok)
{
    printf("  %-50s %s\n", label, ok ? "PASS" : "FAIL");
    assert(ok);
}

static rt_string S(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static int str_eq_c(rt_string s, const char *expected)
{
    size_t elen = strlen(expected);
    rt_string exp = rt_string_from_bytes(expected, elen);
    int result = rt_str_eq(s, exp);
    rt_string_unref(exp);
    return result;
}

static int str_contains_c(rt_string s, const char *needle)
{
    const char *cstr = rt_string_cstr(s);
    if (!cstr)
        return 0;
    return strstr(cstr, needle) != NULL;
}

static void test_parse_simple(void)
{
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

    // XML node objects (parse, element, child_at) use tree-based ownership —
    // rt_obj_release_check0 / rt_obj_free must NOT be called on them.
}

static void test_is_valid(void)
{
    printf("rt_xml_is_valid:\n");
    rt_string valid = S("<a><b/></a>");
    rt_string invalid = S("<a><b></a>");
    check("valid XML returns 1", rt_xml_is_valid(valid));
    check("invalid XML returns 0", !rt_xml_is_valid(invalid));
    rt_string_unref(valid);
    rt_string_unref(invalid);
}

static void test_create_and_format(void)
{
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

static void test_escape_unescape(void)
{
    printf("rt_xml escape / unescape:\n");
    rt_string special = S("a < b & c > d");
    rt_string escaped = rt_xml_escape(special);
    check("escaped contains &lt;", str_contains_c(escaped, "&lt;"));
    check("escaped contains &amp;", str_contains_c(escaped, "&amp;"));
    check("escaped contains &gt;", str_contains_c(escaped, "&gt;"));

    rt_string unescaped = rt_xml_unescape(escaped);
    check("unescape roundtrip", rt_str_eq(special, unescaped));

    rt_string_unref(unescaped);
    rt_string_unref(escaped);
    rt_string_unref(special);
}

static void test_children(void)
{
    printf("rt_xml children manipulation:\n");
    rt_string parent_tag = S("list");
    void *parent = rt_xml_element(parent_tag);
    rt_string_unref(parent_tag);

    for (int i = 0; i < 3; ++i)
    {
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

int main(void)
{
    printf("=== RTXmlTests ===\n");
    test_parse_simple();
    test_is_valid();
    test_create_and_format();
    test_escape_unescape();
    test_children();
    printf("All XML tests passed.\n");
    return 0;
}
