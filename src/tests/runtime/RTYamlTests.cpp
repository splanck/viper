//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_box.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_yaml.h"
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

int main() {
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
    std::printf("RTYamlTests passed.\n");
    return 0;
}
