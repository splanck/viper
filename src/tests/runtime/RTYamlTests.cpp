//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "rt_box.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_yaml.h"
#include "rt_yaml_internal.h"
}

static rt_string make_str(const char *text) {
    return rt_string_from_bytes(text, (int64_t)std::strlen(text));
}

static bool str_eq(rt_string value, const char *expected) {
    return std::strcmp(rt_string_cstr(value), expected) == 0;
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void map_set_string(void *map, const char *key, const char *value) {
    rt_string k = make_str(key);
    rt_string v = make_str(value);
    rt_map_set(map, k, (void *)v);
    release_obj((void *)v);
    release_obj((void *)k);
}

static void test_parse_nested_mapping_and_sequence() {
    rt_string yaml = make_str("name: Alice\nage: 30\nitems:\n  - 1\n  - 2\n");
    void *root = rt_yaml_parse(yaml);
    assert(root != nullptr);

    assert(str_eq((rt_string)rt_map_get(root, rt_const_cstr("name")), "Alice"));
    assert(rt_unbox_i64(rt_map_get(root, rt_const_cstr("age"))) == 30);

    void *items = rt_map_get(root, rt_const_cstr("items"));
    assert(items != nullptr);
    assert(rt_seq_len(items) == 2);
    assert(rt_unbox_i64(rt_seq_get(items, 0)) == 1);
    assert(rt_unbox_i64(rt_seq_get(items, 1)) == 2);

    release_obj(root);
    release_obj((void *)yaml);
}

static void test_sequence_preserves_null_items() {
    rt_string yaml = make_str("- 1\n-\n- 3\n");
    void *seq = rt_yaml_parse(yaml);
    assert(seq != nullptr);

    assert(rt_seq_len(seq) == 3);
    assert(rt_unbox_i64(rt_seq_get(seq, 0)) == 1);
    assert(rt_seq_get(seq, 1) == nullptr);
    assert(rt_unbox_i64(rt_seq_get(seq, 2)) == 3);

    release_obj(seq);
    release_obj((void *)yaml);
}

static void test_multi_document_stream_returns_sequence() {
    rt_string yaml = make_str("---\nname: Alice\n---\nname: Bob\n");
    void *docs = rt_yaml_parse(yaml);
    assert(docs != nullptr);
    assert(str_eq(rt_yaml_type_of(docs), "sequence"));
    assert(rt_seq_len(docs) == 2);

    void *doc0 = rt_seq_get(docs, 0);
    void *doc1 = rt_seq_get(docs, 1);
    assert(str_eq((rt_string)rt_map_get(doc0, rt_const_cstr("name")), "Alice"));
    assert(str_eq((rt_string)rt_map_get(doc1, rt_const_cstr("name")), "Bob"));

    release_obj(docs);
    release_obj((void *)yaml);
}

static void test_invalid_yaml_sets_error_and_is_invalid() {
    rt_string yaml = make_str("name: \"unterminated\n");
    void *parsed = rt_yaml_parse(yaml);
    assert(parsed == nullptr);

    rt_string error = rt_yaml_error();
    assert(error != nullptr);
    assert(std::strstr(rt_string_cstr(error), "unterminated") != nullptr);
    assert(rt_yaml_is_valid(yaml) == 0);

    release_obj((void *)error);
    release_obj((void *)yaml);
}

static void test_format_round_trip() {
    rt_string yaml = make_str("title: Example\nvalues:\n  - 10\n  - 20\n");
    void *parsed = rt_yaml_parse(yaml);
    assert(parsed != nullptr);

    rt_string formatted = rt_yaml_format_indent(parsed, 4);
    assert(formatted != nullptr);
    assert(std::strstr(rt_string_cstr(formatted), "title: Example") != nullptr);

    void *round_trip = rt_yaml_parse(formatted);
    assert(round_trip != nullptr);
    void *values = rt_map_get(round_trip, rt_const_cstr("values"));
    assert(values != nullptr);
    assert(rt_seq_len(values) == 2);
    assert(rt_unbox_i64(rt_seq_get(values, 0)) == 10);
    assert(rt_unbox_i64(rt_seq_get(values, 1)) == 20);

    release_obj(round_trip);
    release_obj((void *)formatted);
    release_obj(parsed);
    release_obj((void *)yaml);
}

static void test_type_of_reports_public_contract_names() {
    void *mapping = rt_yaml_parse(make_str("enabled: true\n"));
    void *sequence = rt_yaml_parse(make_str("- a\n- b\n"));
    void *integer = rt_yaml_parse(make_str("42\n"));
    void *boolean = rt_yaml_parse(make_str("true\n"));
    void *string = rt_yaml_parse(make_str("\"hello\"\n"));

    assert(str_eq(rt_yaml_type_of(nullptr), "null"));
    assert(str_eq(rt_yaml_type_of(mapping), "mapping"));
    assert(str_eq(rt_yaml_type_of(sequence), "sequence"));
    assert(str_eq(rt_yaml_type_of(integer), "int"));
    assert(str_eq(rt_yaml_type_of(boolean), "bool"));
    assert(str_eq(rt_yaml_type_of(string), "string"));

    release_obj(mapping);
    release_obj(sequence);
    release_obj(integer);
    release_obj(boolean);
    release_obj(string);
}

static void test_valid_null_and_invalid_indentation() {
    rt_string null_yaml = make_str("null\n");
    assert(rt_yaml_is_valid(null_yaml) == 1);
    void *null_value = rt_yaml_parse(null_yaml);
    assert(null_value == nullptr);
    rt_string error = rt_yaml_error();
    assert(rt_str_len(error) == 0);
    release_obj((void *)error);
    release_obj((void *)null_yaml);

    rt_string tabbed = make_str("root:\n\tchild: value\n");
    assert(rt_yaml_is_valid(tabbed) == 0);
    release_obj((void *)tabbed);

    rt_string overindented = make_str("a: 1\n  b: 2\n");
    assert(rt_yaml_is_valid(overindented) == 0);
    release_obj((void *)overindented);
}

static void test_flow_collections_round_trip() {
    rt_string yaml = make_str("empty_seq: []\nempty_map: {}\nitems: [1, \"two\", true]\n");
    void *root = rt_yaml_parse(yaml);
    assert(root != nullptr);

    void *empty_seq = rt_map_get(root, rt_const_cstr("empty_seq"));
    void *empty_map = rt_map_get(root, rt_const_cstr("empty_map"));
    void *items = rt_map_get(root, rt_const_cstr("items"));
    assert(str_eq(rt_yaml_type_of(empty_seq), "sequence"));
    assert(str_eq(rt_yaml_type_of(empty_map), "mapping"));
    assert(rt_seq_len(empty_seq) == 0);
    assert(rt_map_len(empty_map) == 0);
    assert(rt_seq_len(items) == 3);

    rt_string formatted = rt_yaml_format(root);
    void *round_trip = rt_yaml_parse(formatted);
    assert(round_trip != nullptr);
    assert(str_eq(rt_yaml_type_of(rt_map_get(round_trip, rt_const_cstr("empty_seq"))), "sequence"));
    assert(str_eq(rt_yaml_type_of(rt_map_get(round_trip, rt_const_cstr("empty_map"))), "mapping"));

    release_obj(round_trip);
    release_obj((void *)formatted);
    release_obj(root);
    release_obj((void *)yaml);
}

static void test_quoted_scalars_comments_and_escapes() {
    rt_string scalar = make_str("\"a: b\" # comment\n");
    void *parsed_scalar = rt_yaml_parse(scalar);
    assert(parsed_scalar != nullptr);
    assert(str_eq((rt_string)parsed_scalar, "a: b"));
    release_obj(parsed_scalar);
    release_obj((void *)scalar);

    rt_string mapping = make_str("\"a: key\": \"line\\n\\u0041\" # comment\n");
    void *parsed = rt_yaml_parse(mapping);
    assert(parsed != nullptr);
    rt_string value = (rt_string)rt_map_get(parsed, rt_const_cstr("a: key"));
    assert(std::strcmp(rt_string_cstr(value), "line\nA") == 0);
    release_obj(parsed);
    release_obj((void *)mapping);

    rt_string bad_escape = make_str("\"bad\\q\"\n");
    assert(rt_yaml_is_valid(bad_escape) == 0);
    release_obj((void *)bad_escape);
}

static void test_overflow_numbers_remain_strings() {
    rt_string yaml = make_str("922337203685477580799\n");
    void *value = rt_yaml_parse(yaml);
    assert(value != nullptr);
    assert(str_eq(rt_yaml_type_of(value), "string"));
    release_obj(value);
    release_obj((void *)yaml);
}

static void test_duplicate_keys_are_invalid() {
    rt_string block = make_str("a: 1\na: 2\n");
    assert(rt_yaml_is_valid(block) == 0);
    release_obj((void *)block);

    rt_string flow = make_str("{a: 1, a: 2}\n");
    assert(rt_yaml_is_valid(flow) == 0);
    release_obj((void *)flow);
}

static void test_yaml_12_boolean_keywords_and_plain_hashes() {
    rt_string yaml = make_str("answer: yes\npower: on\ndisabled: off\ntruth: true\ninf: inf\nnan: "
                              "nan\nvalue: a#b # comment\n");
    void *root = rt_yaml_parse(yaml);
    assert(root != nullptr);

    assert(str_eq(rt_yaml_type_of(rt_map_get(root, rt_const_cstr("answer"))), "string"));
    assert(str_eq((rt_string)rt_map_get(root, rt_const_cstr("answer")), "yes"));
    assert(str_eq((rt_string)rt_map_get(root, rt_const_cstr("power")), "on"));
    assert(str_eq((rt_string)rt_map_get(root, rt_const_cstr("disabled")), "off"));
    assert(str_eq(rt_yaml_type_of(rt_map_get(root, rt_const_cstr("inf"))), "string"));
    assert(str_eq(rt_yaml_type_of(rt_map_get(root, rt_const_cstr("nan"))), "string"));
    assert(str_eq((rt_string)rt_map_get(root, rt_const_cstr("value")), "a#b"));
    assert(str_eq(rt_yaml_type_of(rt_map_get(root, rt_const_cstr("truth"))), "bool"));

    release_obj(root);
    release_obj((void *)yaml);
}

static void test_inline_document_marker_is_plain_scalar() {
    rt_string yaml = make_str("--- value\n");
    void *value = rt_yaml_parse(yaml);
    assert(value != nullptr);
    assert(str_eq(rt_yaml_type_of(value), "string"));
    assert(str_eq((rt_string)value, "--- value"));

    release_obj(value);
    release_obj((void *)yaml);
}

static void test_malformed_flow_collections_are_invalid() {
    rt_string missing = make_str("[1,,2]\n");
    assert(rt_yaml_is_valid(missing) == 0);
    release_obj((void *)missing);

    rt_string trailing = make_str("[1, 2,]\n");
    assert(rt_yaml_is_valid(trailing) == 0);
    release_obj((void *)trailing);

    rt_string empty_value = make_str("{a: }\n");
    assert(rt_yaml_is_valid(empty_value) == 0);
    release_obj((void *)empty_value);
}

static void test_flow_depth_limit_is_invalid() {
    std::string deep;
    for (int i = 0; i < 210; ++i)
        deep.push_back('[');
    deep += "0";
    for (int i = 0; i < 210; ++i)
        deep.push_back(']');
    deep.push_back('\n');

    rt_string yaml = rt_string_from_bytes(deep.data(), deep.size());
    assert(rt_yaml_is_valid(yaml) == 0);
    release_obj((void *)yaml);
}

static void test_formatter_quotes_ambiguous_and_multiline_strings() {
    void *map = rt_map_new();
    map_set_string(map, "inf", ".inf");
    map_set_string(map, "nan", ".nan");
    map_set_string(map, "end", "...");
    map_set_string(map, "multi", "line 1\nline 2\n");
    map_set_string(map, "trail", "value ");

    rt_string formatted = rt_yaml_format(map);
    assert(std::strstr(rt_string_cstr(formatted), "inf: \".inf\"") != nullptr);
    assert(std::strstr(rt_string_cstr(formatted), "nan: \".nan\"") != nullptr);
    assert(std::strstr(rt_string_cstr(formatted), "end: \"...\"") != nullptr);
    assert(std::strstr(rt_string_cstr(formatted), "multi: \"line 1\\nline 2\\n\"") != nullptr);

    void *round_trip = rt_yaml_parse(formatted);
    assert(round_trip != nullptr);
    assert(str_eq((rt_string)rt_map_get(round_trip, rt_const_cstr("inf")), ".inf"));
    assert(str_eq((rt_string)rt_map_get(round_trip, rt_const_cstr("nan")), ".nan"));
    assert(str_eq((rt_string)rt_map_get(round_trip, rt_const_cstr("end")), "..."));
    assert(str_eq((rt_string)rt_map_get(round_trip, rt_const_cstr("multi")), "line 1\nline 2\n"));
    assert(str_eq((rt_string)rt_map_get(round_trip, rt_const_cstr("trail")), "value "));

    release_obj(round_trip);
    release_obj((void *)formatted);
    release_obj(map);
}

static void test_format_preserves_double_precision_and_string_bytes() {
    // VDOC-027: finite doubles must format with round-trip precision, and
    // strings with embedded NUL bytes must be emitted (escaped) in full.
    rt_string src = make_str("v: 1.0000000000000002");
    void *doc = rt_yaml_parse(src);
    release_obj((void *)src);
    assert(doc != NULL);
    rt_string out = rt_yaml_format(doc);
    assert(out != NULL);
    assert(std::strstr(rt_string_cstr(out), "1.0000000000000002") != NULL);
    release_obj((void *)out);
    release_obj(doc);

    void *map = rt_map_new();
    rt_string key = make_str("k");
    rt_string val = rt_string_from_bytes("a\0b", 3);
    rt_map_set(map, key, val);
    rt_string nul_out = rt_yaml_format(map);
    assert(nul_out != NULL);
    const char *s = rt_string_cstr(nul_out);
    // The NUL byte is escaped, and the trailing byte after it survives.
    assert(std::strstr(s, "\\u0000") != NULL);
    assert(std::strstr(s, "b\"") != NULL);
    release_obj((void *)nul_out);
    release_obj((void *)key);
    release_obj((void *)val);
    release_obj(map);
}

static void test_anchor_alias_syntax_is_rejected() {
    // VDOC-028: anchors/aliases are unsupported and must invalidate the
    // document explicitly instead of being silently misparsed.
    rt_string anchored = make_str("base: &b {x: 1}\ncopy: *b");
    assert(rt_yaml_parse(anchored) == NULL);
    release_obj((void *)anchored);

    rt_string alias_only = make_str("a: *ref");
    assert(rt_yaml_parse(alias_only) == NULL);
    release_obj((void *)alias_only);

    // Quoted and mid-token '&'/'*' remain ordinary string content.
    rt_string quoted = make_str("q: \"a&b\"\nm: x*y");
    void *doc = rt_yaml_parse(quoted);
    assert(doc != NULL);
    release_obj(doc);
    release_obj((void *)quoted);
}

static void test_format_bounds_cyclic_containers() {
    // VDOC-033: a self-referencing container must fail the format (empty
    // result) instead of recursing without bound.
    void *map = rt_map_new();
    rt_string key = make_str("self");
    rt_map_set(map, key, map);
    rt_string out = rt_yaml_format(map);
    assert(out != NULL);
    assert(rt_str_len(out) == 0);
    release_obj((void *)out);
    release_obj((void *)key);
    // Break the cycle before releasing so the map tears down normally.
    rt_string self_key = make_str("self");
    rt_map_remove(map, self_key);
    release_obj((void *)self_key);
    release_obj(map);
}

static void test_scalar_with_embedded_nul_stays_string() {
    // VDOC-039: a scalar span containing a NUL can never be numeric; the whole
    // span must stay a string instead of strtoll silently stopping at the NUL.
    void *v = parse_scalar("1\0garbage", 9);
    assert(v != NULL);
    assert(rt_string_is_handle(v));
    assert(rt_str_len((rt_string)v) == 9);
    release_obj(v);
}

static void test_parse_rejects_malformed_utf8() {
    // VDOC-040: YAML requires a valid UTF-8 stream; a standalone 0xFF byte is
    // not UTF-8 and must invalidate the document.
    rt_string bad = rt_string_from_bytes("x: \xFF", 4);
    assert(rt_yaml_parse(bad) == NULL);
    release_obj((void *)bad);

    // Valid multibyte text still parses (Euro sign value).
    rt_string good = rt_string_from_bytes("x: \xE2\x82\xAC", 6);
    void *doc = rt_yaml_parse(good);
    assert(doc != NULL);
    release_obj(doc);
    release_obj((void *)good);
}

int main() {
    test_scalar_with_embedded_nul_stays_string();
    test_parse_rejects_malformed_utf8();
    test_format_bounds_cyclic_containers();
    test_anchor_alias_syntax_is_rejected();
    test_format_preserves_double_precision_and_string_bytes();
    test_parse_nested_mapping_and_sequence();
    test_sequence_preserves_null_items();
    test_multi_document_stream_returns_sequence();
    test_invalid_yaml_sets_error_and_is_invalid();
    test_format_round_trip();
    test_type_of_reports_public_contract_names();
    test_valid_null_and_invalid_indentation();
    test_flow_collections_round_trip();
    test_quoted_scalars_comments_and_escapes();
    test_overflow_numbers_remain_strings();
    test_duplicate_keys_are_invalid();
    test_yaml_12_boolean_keywords_and_plain_hashes();
    test_inline_document_marker_is_plain_scalar();
    test_malformed_flow_collections_are_invalid();
    test_flow_depth_limit_is_invalid();
    test_formatter_quotes_ambiguous_and_multiline_strings();
    std::printf("RTYamlTests passed.\n");
    return 0;
}
