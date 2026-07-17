//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_json.h"
#include "rt_jsonpath.h"
#include "rt_map.h"
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

static void test_simple_key() {
    // Build: {"name": "Alice"}
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("name"), make_str("Alice"));

    rt_string path = make_str("name");
    void *val = rt_jsonpath_get(obj, path);
    assert(val != NULL);
    assert(strcmp(rt_string_cstr((rt_string)val), "Alice") == 0);
    rt_string_unref(path);
}

static void test_dotted_path() {
    // Build: {"user": {"name": "Bob"}}
    void *inner = rt_map_new();
    rt_map_set(inner, make_str("name"), make_str("Bob"));
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("user"), inner);

    rt_string path = make_str("user.name");
    void *val = rt_jsonpath_get(obj, path);
    assert(val != NULL);
    assert(strcmp(rt_string_cstr((rt_string)val), "Bob") == 0);
    rt_string_unref(path);
}

static void test_bracket_index() {
    // Build: {"items": ["a", "b", "c"]}
    void *arr = rt_seq_new();
    rt_seq_push(arr, make_str("a"));
    rt_seq_push(arr, make_str("b"));
    rt_seq_push(arr, make_str("c"));
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("items"), arr);

    rt_string path = make_str("items[1]");
    void *val = rt_jsonpath_get(obj, path);
    assert(val != NULL);
    assert(strcmp(rt_string_cstr((rt_string)val), "b") == 0);
    rt_string_unref(path);
}

static void test_has() {
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("x"), make_str("1"));

    rt_string p1 = make_str("x");
    rt_string p2 = make_str("y");
    assert(rt_jsonpath_has(obj, p1) == 1);
    assert(rt_jsonpath_has(obj, p2) == 0);
    rt_string_unref(p1);
    rt_string_unref(p2);
}

static void test_paths_with_embedded_nul_match_nothing() {
    // VDOC-038: a path whose runtime bytes continue past a NUL must not
    // silently address the shorter C-string prefix — it matches nothing.
    void *obj = rt_map_new();
    rt_string key = make_str("a");
    rt_string value = make_str("hit");
    rt_map_set(obj, key, value);

    rt_string sneaky = rt_string_from_bytes("a\0suffix", 8);
    assert(rt_jsonpath_get(obj, sneaky) == NULL);
    assert(rt_jsonpath_has(obj, sneaky) == 0);
    void *matches = rt_jsonpath_query(obj, sneaky);
    assert(matches != NULL && rt_seq_len(matches) == 0);

    rt_string plain = make_str("a");
    void *hit = rt_jsonpath_get(obj, plain);
    assert(hit != NULL);

    rt_string_unref(sneaky);
    rt_string_unref(plain);
    rt_string_unref(key);
    rt_string_unref(value);
}

static void test_has_and_get_or_distinguish_null_from_missing() {
    // VDOC-021: a present JSON null member must report Has == true and must not
    // be replaced by GetOr's default; only a missing path takes the default.
    rt_string json = make_str("{\"x\":null,\"y\":1}");
    void *doc = rt_json_parse(json);
    rt_string_unref(json);
    assert(doc != NULL);

    rt_string px = make_str("x");
    rt_string pz = make_str("z");
    rt_string pchild = make_str("x.child");
    assert(rt_jsonpath_has(doc, px) == 1);
    assert(rt_jsonpath_has(doc, pz) == 0);
    assert(rt_jsonpath_has(doc, pchild) == 0); // null has no children

    rt_string def = make_str("default");
    void *kept = rt_jsonpath_get_or(doc, px, def);
    assert(kept == NULL); // real null preserved
    void *defaulted = rt_jsonpath_get_or(doc, pz, def);
    assert(defaulted != NULL);
    assert(strcmp(rt_string_cstr((rt_string)defaulted), "default") == 0);
    rt_string_unref((rt_string)defaulted);
    rt_string_unref(def);

    rt_string_unref(px);
    rt_string_unref(pz);
    rt_string_unref(pchild);
}

static void test_get_or() {
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("x"), make_str("hello"));

    rt_string p1 = make_str("x");
    rt_string p2 = make_str("missing");
    rt_string def = make_str("default");

    void *v1 = rt_jsonpath_get_or(obj, p1, def);
    assert(strcmp(rt_string_cstr((rt_string)v1), "hello") == 0);

    void *v2 = rt_jsonpath_get_or(obj, p2, def);
    rt_string_unref(def);
    assert(strcmp(rt_string_cstr((rt_string)v2), "default") == 0);
    rt_string_unref((rt_string)v2);

    rt_string_unref(p1);
    rt_string_unref(p2);
}

static void test_get_str() {
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("msg"), make_str("world"));

    rt_string p1 = make_str("msg");
    rt_string p2 = make_str("nope");

    rt_string s1 = rt_jsonpath_get_str(obj, p1);
    assert(strcmp(rt_string_cstr(s1), "world") == 0);

    rt_string s2 = rt_jsonpath_get_str(obj, p2);
    assert(strlen(rt_string_cstr(s2)) == 0);

    rt_string_unref(p1);
    rt_string_unref(p2);
}

static void test_get_int() {
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("count"), make_str("42"));

    rt_string p = make_str("count");
    assert(rt_jsonpath_get_int(obj, p) == 42);
    rt_string_unref(p);
}

static void test_wildcard_query() {
    // Build: {"users": [{"name": "A"}, {"name": "B"}]}
    void *u1 = rt_map_new();
    rt_map_set(u1, make_str("name"), make_str("A"));
    void *u2 = rt_map_new();
    rt_map_set(u2, make_str("name"), make_str("B"));
    void *arr = rt_seq_new();
    rt_seq_push(arr, u1);
    rt_seq_push(arr, u2);
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("users"), arr);

    rt_string path = make_str("users.*.name");
    void *results = rt_jsonpath_query(obj, path);
    assert(rt_seq_len(results) == 2);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(results, 0)), "A") == 0);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(results, 1)), "B") == 0);
    rt_string_unref(path);
}

static void test_wildcard_query_over_map_skips_non_containers() {
    void *u1 = rt_map_new();
    rt_map_set(u1, make_str("name"), make_str("A"));
    void *obj = rt_map_new();
    rt_map_set(obj, make_str("user"), u1);
    rt_map_set(obj, make_str("scalar"), make_str("ignore"));

    rt_string path = make_str("*.name");
    void *results = rt_jsonpath_query(obj, path);
    assert(rt_seq_len(results) == 1);
    assert(strcmp(rt_string_cstr((rt_string)rt_seq_get(results, 0)), "A") == 0);
    rt_string_unref(path);
}

static void test_auto_parse_raw_json_getters_are_stable() {
    rt_string json = make_str("{\"user\":{\"name\":\"Ada\",\"count\":7}}");

    rt_string name_path = make_str("user.name");
    rt_string name = rt_jsonpath_get_str(json, name_path);
    assert(strcmp(rt_string_cstr(name), "Ada") == 0);

    rt_string count_path = make_str("user.count");
    int64_t count = rt_jsonpath_get_int(json, count_path);
    assert(count == 7);

    rt_string_unref(name);
    rt_string_unref(name_path);
    rt_string_unref(count_path);
    rt_string_unref(json);
}

static void test_null_safety() {
    assert(rt_jsonpath_get(NULL, NULL) == NULL);
    assert(rt_jsonpath_has(NULL, NULL) == 0);
    assert(rt_jsonpath_get_int(NULL, NULL) == 0);
}

static void test_get_int_from_parsed_json() {
    // JSON numbers are boxed f64 after parsing — previously crashed
    rt_string json = make_str("{\"ver\":42}");
    void *doc = rt_json_parse(json);
    assert(doc != NULL);

    rt_string path = make_str("ver");
    int64_t val = rt_jsonpath_get_int(doc, path);
    assert(val == 42);
    rt_string_unref(path);
    rt_string_unref(json);
}

static void test_get_str_from_parsed_json() {
    // Get string value from parsed JSON
    rt_string json = make_str("{\"name\":\"zanna\",\"ver\":1}");
    void *doc = rt_json_parse(json);
    assert(doc != NULL);

    rt_string p1 = make_str("name");
    rt_string s1 = rt_jsonpath_get_str(doc, p1);
    assert(strcmp(rt_string_cstr(s1), "zanna") == 0);

    // Get numeric value as string
    rt_string p2 = make_str("ver");
    rt_string s2 = rt_jsonpath_get_str(doc, p2);
    // Should be "1" (converted from boxed f64)
    assert(strlen(rt_string_cstr(s2)) > 0);

    rt_string_unref(p1);
    rt_string_unref(p2);
    rt_string_unref(json);
}

/// @brief Main.
int main() {
    test_simple_key();
    test_dotted_path();
    test_bracket_index();
    test_has();
    test_paths_with_embedded_nul_match_nothing();
    test_has_and_get_or_distinguish_null_from_missing();
    test_get_or();
    test_get_str();
    test_get_int();
    test_wildcard_query();
    test_wildcard_query_over_map_skips_non_containers();
    test_auto_parse_raw_json_getters_are_stable();
    test_null_safety();
    test_get_int_from_parsed_json();
    test_get_str_from_parsed_json();
    return 0;
}
