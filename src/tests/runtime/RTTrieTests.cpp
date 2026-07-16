//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTTrieTests.cpp
// Purpose: Tests for Viper.Collections.Trie runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_box.h"
#include "rt_option.h"
#include "rt_trie.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static void rt_release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void *new_obj() {
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static rt_string make_key(const char *text) {
    return rt_string_from_bytes(text, strlen(text));
}

static rt_string make_key_bytes(const char *bytes, size_t len) {
    return rt_string_from_bytes(bytes, len);
}

static bool str_eq(rt_string s, const char *expected) {
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

static bool str_bytes_eq(rt_string s, const char *expected, size_t len) {
    const char *data = rt_string_cstr(s);
    return data && rt_str_len(s) == (int64_t)len && memcmp(data, expected, len) == 0;
}

static void test_new() {
    void *t = rt_trie_new();
    assert(t != nullptr);
    assert(rt_trie_len(t) == 0);
    assert(rt_trie_is_empty(t) == 1);
    rt_release_obj(t);
}

static void test_put_and_get() {
    void *t = rt_trie_new();
    rt_string k1 = make_key("hello");
    rt_string k2 = make_key("help");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_trie_set(t, k1, v1);
    rt_trie_set(t, k2, v2);

    assert(rt_trie_len(t) == 2);
    assert(rt_trie_get(t, k1) == v1);
    assert(rt_trie_get(t, k2) == v2);

    rt_string missing = make_key("missing");
    assert(rt_trie_get(t, missing) == NULL);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(missing);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(t);
}

static void test_has() {
    void *t = rt_trie_new();
    rt_string k = make_key("apple");
    void *v = new_obj();

    rt_trie_set(t, k, v);
    assert(rt_trie_has(t, k) == 1);

    rt_string nope = make_key("app");
    assert(rt_trie_has(t, nope) == 0); // "app" is a prefix, not a complete key

    rt_string_unref(k);
    rt_string_unref(nope);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_overwrite() {
    void *t = rt_trie_new();
    rt_string k = make_key("key");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_trie_set(t, k, v1);
    assert(rt_trie_get(t, k) == v1);
    assert(rt_trie_len(t) == 1);

    rt_trie_set(t, k, v2);
    assert(rt_trie_get(t, k) == v2);
    assert(rt_trie_len(t) == 1); // Count unchanged

    rt_string_unref(k);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(t);
}

static void test_has_prefix() {
    void *t = rt_trie_new();
    rt_string k1 = make_key("apple");
    rt_string k2 = make_key("application");
    rt_string k3 = make_key("banana");
    void *v = new_obj();

    rt_trie_set(t, k1, v);
    rt_trie_set(t, k2, v);
    rt_trie_set(t, k3, v);

    rt_string prefix = make_key("app");
    assert(rt_trie_has_prefix(t, prefix) == 1);

    rt_string no_prefix = make_key("cherry");
    assert(rt_trie_has_prefix(t, no_prefix) == 0);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
    rt_string_unref(prefix);
    rt_string_unref(no_prefix);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_with_prefix() {
    void *t = rt_trie_new();
    rt_string k1 = make_key("apple");
    rt_string k2 = make_key("application");
    rt_string k3 = make_key("apply");
    rt_string k4 = make_key("banana");
    void *v = new_obj();

    rt_trie_set(t, k1, v);
    rt_trie_set(t, k2, v);
    rt_trie_set(t, k3, v);
    rt_trie_set(t, k4, v);

    rt_string prefix = make_key("app");
    void *results = rt_trie_with_prefix(t, prefix);
    assert(rt_seq_len(results) == 3);
    // Results should be in sorted order (lexicographic)

    rt_release_obj(results);
    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
    rt_string_unref(k4);
    rt_string_unref(prefix);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_longest_prefix() {
    void *t = rt_trie_new();
    void *v = new_obj();

    rt_string k1 = make_key("a");
    rt_string k2 = make_key("ab");
    rt_string k3 = make_key("abc");
    rt_string k4 = make_key("abcdef");

    rt_trie_set(t, k1, v);
    rt_trie_set(t, k2, v);
    rt_trie_set(t, k3, v);
    rt_trie_set(t, k4, v);

    rt_string query = make_key("abcde");
    rt_string result = rt_trie_longest_prefix(t, query);
    assert(str_eq(result, "abc")); // "abcdef" doesn't match, but "abc" does
    rt_string_unref(result);

    rt_string query2 = make_key("xyz");
    rt_string result2 = rt_trie_longest_prefix(t, query2);
    assert(str_eq(result2, ""));
    rt_string_unref(result2);

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
    rt_string_unref(k4);
    rt_string_unref(query);
    rt_string_unref(query2);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_remove() {
    void *t = rt_trie_new();
    rt_string k1 = make_key("hello");
    rt_string k2 = make_key("help");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_trie_set(t, k1, v1);
    rt_trie_set(t, k2, v2);
    assert(rt_trie_len(t) == 2);

    assert(rt_trie_remove(t, k1) == 1);
    assert(rt_trie_len(t) == 1);
    assert(rt_trie_has(t, k1) == 0);
    assert(rt_trie_has(t, k2) == 1);

    assert(rt_trie_remove(t, k1) == 0); // Already removed

    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(t);
}

static void test_clear() {
    void *t = rt_trie_new();
    rt_string k = make_key("test");
    void *v = new_obj();

    rt_trie_set(t, k, v);
    rt_trie_clear(t);

    assert(rt_trie_len(t) == 0);
    assert(rt_trie_is_empty(t) == 1);
    assert(rt_trie_has(t, k) == 0);

    rt_string_unref(k);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_keys() {
    void *t = rt_trie_new();
    void *v = new_obj();
    rt_string k1 = make_key("banana");
    rt_string k2 = make_key("apple");
    rt_string k3 = make_key("cherry");

    rt_trie_set(t, k1, v);
    rt_trie_set(t, k2, v);
    rt_trie_set(t, k3, v);

    void *keys = rt_trie_keys(t);
    assert(rt_seq_len(keys) == 3);
    // Trie traversal produces lexicographic order
    assert(str_eq((rt_string)rt_seq_get(keys, 0), "apple"));
    assert(str_eq((rt_string)rt_seq_get(keys, 1), "banana"));
    assert(str_eq((rt_string)rt_seq_get(keys, 2), "cherry"));

    rt_release_obj(keys);
    rt_string_unref(k1);
    rt_string_unref(k2);
    rt_string_unref(k3);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_empty_key() {
    void *t = rt_trie_new();
    rt_string k = make_key("");
    void *v = new_obj();

    rt_trie_set(t, k, v);
    assert(rt_trie_len(t) == 1);
    assert(rt_trie_has(t, k) == 1);
    assert(rt_trie_get(t, k) == v);

    rt_string_unref(k);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_set_retain_overflow_leaves_key_absent() {
    void *t = rt_trie_new();
    rt_string k = make_key("boom");
    void *value = new_obj();

    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    EXPECT_TRAP(rt_trie_set(t, k, value));
    assert(rt_trie_len(t) == 0);
    assert(rt_trie_has(t, k) == 0);
    assert(rt_trie_get(t, k) == NULL);

    hdr->refcnt = 1;
    rt_string_unref(k);
    rt_release_obj(value);
    rt_release_obj(t);
}

static void test_clone_retain_overflow_keeps_source_unchanged() {
    void *t = rt_trie_new();
    rt_string k = make_key("clone-boom");
    void *value = new_obj();

    rt_trie_set(t, k, value);
    rt_heap_hdr_t *hdr = rt_heap_hdr(value);
    hdr->refcnt = RT_HEAP_MAX_MORTAL_REFCNT;

    EXPECT_TRAP((void)rt_trie_clone(t));
    assert(rt_trie_len(t) == 1);
    assert(rt_trie_has(t, k) == 1);
    assert(rt_trie_get(t, k) == value);
    assert(hdr->refcnt == RT_HEAP_MAX_MORTAL_REFCNT);

    hdr->refcnt = 2;
    rt_string_unref(k);
    rt_release_obj(value);
    rt_release_obj(t);
}

static void test_embedded_nul_keys() {
    void *t = rt_trie_new();
    void *v = new_obj();
    const char key_bytes[] = {'a', '\0', 'b'};
    const char prefix_bytes[] = {'a', '\0'};
    const char query_bytes[] = {'a', '\0', 'b', 'x'};
    rt_string key = make_key_bytes(key_bytes, sizeof(key_bytes));
    rt_string prefix = make_key_bytes(prefix_bytes, sizeof(prefix_bytes));
    rt_string query = make_key_bytes(query_bytes, sizeof(query_bytes));

    rt_trie_set(t, key, v);
    assert(rt_trie_len(t) == 1);
    assert(rt_trie_has(t, key) == 1);
    assert(rt_trie_get(t, key) == v);
    assert(rt_trie_has_prefix(t, prefix) == 1);

    void *matches = rt_trie_with_prefix(t, prefix);
    assert(rt_seq_len(matches) == 1);
    assert(str_bytes_eq((rt_string)rt_seq_get(matches, 0), key_bytes, sizeof(key_bytes)));

    rt_string longest = rt_trie_longest_prefix(t, query);
    assert(str_bytes_eq(longest, key_bytes, sizeof(key_bytes)));
    assert(rt_trie_remove(t, key) == 1);
    assert(rt_trie_len(t) == 0);

    rt_release_obj(matches);
    rt_string_unref(longest);
    rt_string_unref(key);
    rt_string_unref(prefix);
    rt_string_unref(query);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_null_safety() {
    rt_string k = make_key("test");
    assert(rt_trie_len(NULL) == 0);
    assert(rt_trie_is_empty(NULL) == 1);
    assert(rt_trie_get(NULL, k) == NULL);
    assert(rt_trie_has(NULL, k) == 0);
    assert(rt_trie_has_prefix(NULL, k) == 0);
    assert(rt_trie_remove(NULL, k) == 0);
    rt_trie_set(NULL, k, NULL);
    rt_trie_clear(NULL);
    rt_string_unref(k);
}

static void test_longest_prefix_option() {
    // VDOC-103: the Option form distinguishes an empty-key match from no match.
    void *trie = rt_trie_new();
    rt_string empty = rt_string_from_bytes("", 0);
    rt_string probe = rt_string_from_bytes("zzz", 3);

    void *none = rt_trie_longest_prefix_option(trie, probe);
    assert(rt_option_is_none(none) == 1);

    rt_trie_set(trie, empty, rt_box_i64(1));
    void *some = rt_trie_longest_prefix_option(trie, probe);
    assert(rt_option_is_some(some) == 1);
    rt_string match = rt_option_unwrap_str(some);
    assert(rt_str_len(match) == 0);

    rt_string_unref(empty);
    rt_string_unref(probe);
}

int main() {
    test_longest_prefix_option();
    test_new();
    test_put_and_get();
    test_has();
    test_overwrite();
    test_has_prefix();
    test_with_prefix();
    test_longest_prefix();
    test_remove();
    test_clear();
    test_keys();
    test_empty_key();
    test_set_retain_overflow_leaves_key_absent();
    test_clone_retain_overflow_keeps_source_unchanged();
    test_embedded_nul_keys();
    test_null_safety();
    return 0;
}
