//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_toml.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void test_parse_simple() {
    rt_string src = make_str("title = \"TOML Test\"\nversion = \"1.0\"");
    void *root = rt_toml_parse(src);
    assert(root != NULL);
    assert(rt_map_len(root) == 2);

    rt_string k1 = make_str("title");
    rt_string k2 = make_str("version");
    void *v1 = rt_map_get(root, k1);
    void *v2 = rt_map_get(root, k2);
    assert(v1 != NULL);
    assert(v2 != NULL);
    assert(strcmp(rt_string_cstr((rt_string)v1), "TOML Test") == 0);
    assert(strcmp(rt_string_cstr((rt_string)v2), "1.0") == 0);

    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_parse_section() {
    rt_string src = make_str("[server]\nhost = \"localhost\"\nport = 8080\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string sk = make_str("server");
    void *section = rt_map_get(root, sk);
    assert(section != NULL);

    rt_string hk = make_str("host");
    void *host = rt_map_get(section, hk);
    assert(host != NULL);
    assert(strcmp(rt_string_cstr((rt_string)host), "localhost") == 0);

    rt_string_unref(sk);
    rt_string_unref(hk);
}

static void test_parse_comments() {
    rt_string src = make_str("# This is a comment\nkey = \"value\"\n# Another comment\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);
    assert(rt_map_len(root) == 1);
}

static void test_parse_quoted_values() {
    rt_string src = make_str("name = \"hello world\"\npath = 'C:\\Users\\test'\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string nk = make_str("name");
    void *name = rt_map_get(root, nk);
    assert(strcmp(rt_string_cstr((rt_string)name), "hello world") == 0);

    rt_string_unref(nk);
}

static void test_parse_bare_values() {
    rt_string src = make_str("count = 42\nenabled = true\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string ck = make_str("count");
    void *count = rt_map_get(root, ck);
    assert(strcmp(rt_string_cstr((rt_string)count), "42") == 0);

    rt_string ek = make_str("enabled");
    void *enabled = rt_map_get(root, ek);
    assert(strcmp(rt_string_cstr((rt_string)enabled), "true") == 0);

    rt_string_unref(ck);
    rt_string_unref(ek);
}

static void test_parse_inline_array_values_survive() {
    rt_string src = make_str("items = [alpha, \"beta\", gamma]\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string key = make_str("items");
    void *items = rt_map_get(root, key);
    assert(items != NULL);
    assert(rt_seq_len(items) == 3);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(items, 0)), "alpha") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(items, 1)), "beta") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(items, 2)), "gamma") == 0);

    rt_string_unref(key);
    rt_string_unref(src);
}

static void test_is_valid() {
    rt_string valid = make_str("key = \"value\"\n");
    assert(rt_toml_is_valid(valid) == 1);
}

static void test_get_dotted() {
    rt_string src = make_str("[database]\nhost = \"db.example.com\"\nport = 5432\n");
    void *root = rt_toml_parse(src);

    rt_string path = make_str("database.host");
    void *val = rt_toml_get(root, path);
    assert(val != NULL);
    assert(strcmp(rt_string_cstr((rt_string)val), "db.example.com") == 0);

    rt_string_unref(path);
}

static void test_get_deep_dotted_section() {
    rt_string src = make_str("[a.b.c]\nvalue = \"ok\"\n");
    void *root = rt_toml_parse(src);
    assert(root != NULL);

    rt_string path = make_str("a.b.c.value");
    rt_string val = rt_toml_get_str(root, path);
    assert(strcmp(rt_string_cstr(val), "ok") == 0);

    rt_string_unref(path);
}

static void test_invalid_syntax_returns_null() {
    rt_string missing_section_close = make_str("[server\nhost = \"localhost\"\n");
    assert(rt_toml_parse(missing_section_close) == NULL);
    assert(rt_toml_is_valid(missing_section_close) == 0);

    rt_string missing_array_close = make_str("values = [1, 2\n");
    assert(rt_toml_parse(missing_array_close) == NULL);
    assert(rt_toml_is_valid(missing_array_close) == 0);

    rt_string duplicate_key = make_str("key = \"one\"\nkey = \"two\"\n");
    assert(rt_toml_parse(duplicate_key) == NULL);
    assert(rt_toml_is_valid(duplicate_key) == 0);

    rt_string trailing_section_junk = make_str("[server] junk\nhost = \"localhost\"\n");
    assert(rt_toml_parse(trailing_section_junk) == NULL);
    assert(rt_toml_is_valid(trailing_section_junk) == 0);

    rt_string table_conflict = make_str("a = \"scalar\"\n[a.b]\nvalue = \"bad\"\n");
    assert(rt_toml_parse(table_conflict) == NULL);
    assert(rt_toml_is_valid(table_conflict) == 0);

    rt_string missing_equals = make_str("key \"value\"\n");
    assert(rt_toml_parse(missing_equals) == NULL);
    assert(rt_toml_is_valid(missing_equals) == 0);
}

static void test_null_safety() {
    assert(rt_toml_parse(NULL) == NULL);
    assert(rt_toml_is_valid(NULL) == 0);
    assert(rt_toml_get(NULL, NULL) == NULL);
}

static void test_empty() {
    rt_string src = make_str("");
    void *root = rt_toml_parse(src);
    assert(root != NULL);
    assert(rt_map_len(root) == 0);
}

static void test_depth_limit() {
    // Build a TOML section name with 201 dot-separated parts (depth = 201 > TOML_MAX_DEPTH=200)
    // e.g. [a.a.a....a] with 201 'a's separated by dots
    char deep[512];
    int pos = 0;
    deep[pos++] = '[';
    for (int i = 0; i < 201; i++) {
        if (i > 0)
            deep[pos++] = '.';
        deep[pos++] = 'a';
    }
    deep[pos++] = ']';
    deep[pos++] = '\n';
    deep[pos++] = '\0';

    rt_string src = make_str(deep);
    // The overly-deep section should trigger the error flag
    assert(rt_toml_is_valid(src) == 0);

    // Verify that a section at exactly depth 200 is accepted
    char ok[512];
    pos = 0;
    ok[pos++] = '[';
    for (int i = 0; i < 200; i++) {
        if (i > 0)
            ok[pos++] = '.';
        ok[pos++] = 'b';
    }
    ok[pos++] = ']';
    ok[pos++] = '\n';
    ok[pos++] = '\0';

    rt_string src2 = make_str(ok);
    assert(rt_toml_is_valid(src2) == 1);
}

static void test_format_scalar_values() {
    void *root = rt_map_new();

    rt_string name_key = make_str("name");
    rt_string name_value = make_str("Alice");
    rt_map_set(root, name_key, (void *)name_value);

    rt_string age_key = make_str("age");
    rt_map_set(root, age_key, rt_box_i64(30));

    rt_string enabled_key = make_str("enabled");
    rt_map_set(root, enabled_key, rt_box_i1(1));

    void *items = rt_seq_new();
    rt_seq_set_owns_elements(items, 1);
    rt_string item = make_str("x");
    rt_seq_push(items, item);
    rt_seq_push(items, rt_box_i64(7));

    rt_string items_key = make_str("items");
    rt_map_set(root, items_key, items);

    rt_string formatted = rt_toml_format(root);
    assert(rt_toml_is_valid(formatted) == 1);
    assert(strstr(rt_string_cstr(formatted), "name = \"Alice\"") != NULL);
    assert(strstr(rt_string_cstr(formatted), "age = 30") != NULL);
    assert(strstr(rt_string_cstr(formatted), "enabled = true") != NULL);
    assert(strstr(rt_string_cstr(formatted), "items = [\"x\", 7]") != NULL);

    rt_string not_map = make_str("not a map");
    rt_string empty = rt_toml_format((void *)not_map);
    assert(rt_str_len(empty) == 0);
}

static void test_format_escapes_embedded_nul_keys_and_values() {
    void *root = rt_map_new();
    const char key_bytes[] = {'a', '\0', 'b'};
    const char value_bytes[] = {'x', '\0', 'y'};
    rt_string key = rt_string_from_bytes(key_bytes, sizeof(key_bytes));
    rt_string value = rt_string_from_bytes(value_bytes, sizeof(value_bytes));
    rt_map_set(root, key, value);

    rt_string formatted = rt_toml_format(root);
    assert(strcmp(rt_string_cstr(formatted), "\"a\\u0000b\" = \"x\\u0000y\"\n") == 0);

    rt_string_unref(key);
    rt_string_unref(value);
}

static void test_format_escapes_embedded_nul_boxed_string() {
    void *root = rt_map_new();
    const char value_bytes[] = {'x', '\0', 'y'};
    rt_string key = make_str("value");
    rt_string value = rt_string_from_bytes(value_bytes, sizeof(value_bytes));
    rt_map_set(root, key, rt_box_str(value));

    rt_string formatted = rt_toml_format(root);
    assert(strcmp(rt_string_cstr(formatted), "value = \"x\\u0000y\"\n") == 0);

    rt_string_unref(key);
    rt_string_unref(value);
}

static void test_get_path_preserves_embedded_nul_segments() {
    void *root = rt_map_new();
    const char key_bytes[] = {'a', '\0', 'b'};
    rt_string key = rt_string_from_bytes(key_bytes, sizeof(key_bytes));
    rt_string value = make_str("ok");
    rt_map_set(root, key, value);

    void *got = rt_toml_get(root, key);
    assert(got != NULL);
    assert(strcmp(rt_string_cstr((rt_string)got), "ok") == 0);

    rt_string_unref(key);
    rt_string_unref(value);
}

/// @brief Main.
int main() {
    test_parse_simple();
    test_parse_section();
    test_parse_comments();
    test_parse_quoted_values();
    test_parse_bare_values();
    test_parse_inline_array_values_survive();
    test_is_valid();
    test_get_dotted();
    test_get_deep_dotted_section();
    test_invalid_syntax_returns_null();
    test_null_safety();
    test_empty();
    test_depth_limit();
    test_format_scalar_values();
    test_format_escapes_embedded_nul_keys_and_values();
    test_format_escapes_embedded_nul_boxed_string();
    test_get_path_preserves_embedded_nul_segments();
    return 0;
}
