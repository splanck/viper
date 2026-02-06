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
#include "rt_trie.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void rt_release_obj(void *p)
{
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static void *new_obj()
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

static rt_string make_key(const char *text)
{
    return rt_string_from_bytes(text, strlen(text));
}

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

static void test_new()
{
    void *t = rt_trie_new();
    assert(t != nullptr);
    assert(rt_trie_len(t) == 0);
    assert(rt_trie_is_empty(t) == 1);
    rt_release_obj(t);
}

static void test_put_and_get()
{
    void *t = rt_trie_new();
    rt_string k1 = make_key("hello");
    rt_string k2 = make_key("help");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_trie_put(t, k1, v1);
    rt_trie_put(t, k2, v2);

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

static void test_has()
{
    void *t = rt_trie_new();
    rt_string k = make_key("apple");
    void *v = new_obj();

    rt_trie_put(t, k, v);
    assert(rt_trie_has(t, k) == 1);

    rt_string nope = make_key("app");
    assert(rt_trie_has(t, nope) == 0); // "app" is a prefix, not a complete key

    rt_string_unref(k);
    rt_string_unref(nope);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_overwrite()
{
    void *t = rt_trie_new();
    rt_string k = make_key("key");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_trie_put(t, k, v1);
    assert(rt_trie_get(t, k) == v1);
    assert(rt_trie_len(t) == 1);

    rt_trie_put(t, k, v2);
    assert(rt_trie_get(t, k) == v2);
    assert(rt_trie_len(t) == 1); // Count unchanged

    rt_string_unref(k);
    rt_release_obj(v1);
    rt_release_obj(v2);
    rt_release_obj(t);
}

static void test_has_prefix()
{
    void *t = rt_trie_new();
    rt_string k1 = make_key("apple");
    rt_string k2 = make_key("application");
    rt_string k3 = make_key("banana");
    void *v = new_obj();

    rt_trie_put(t, k1, v);
    rt_trie_put(t, k2, v);
    rt_trie_put(t, k3, v);

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

static void test_with_prefix()
{
    void *t = rt_trie_new();
    rt_string k1 = make_key("apple");
    rt_string k2 = make_key("application");
    rt_string k3 = make_key("apply");
    rt_string k4 = make_key("banana");
    void *v = new_obj();

    rt_trie_put(t, k1, v);
    rt_trie_put(t, k2, v);
    rt_trie_put(t, k3, v);
    rt_trie_put(t, k4, v);

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

static void test_longest_prefix()
{
    void *t = rt_trie_new();
    void *v = new_obj();

    rt_string k1 = make_key("a");
    rt_string k2 = make_key("ab");
    rt_string k3 = make_key("abc");
    rt_string k4 = make_key("abcdef");

    rt_trie_put(t, k1, v);
    rt_trie_put(t, k2, v);
    rt_trie_put(t, k3, v);
    rt_trie_put(t, k4, v);

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

static void test_remove()
{
    void *t = rt_trie_new();
    rt_string k1 = make_key("hello");
    rt_string k2 = make_key("help");
    void *v1 = new_obj();
    void *v2 = new_obj();

    rt_trie_put(t, k1, v1);
    rt_trie_put(t, k2, v2);
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

static void test_clear()
{
    void *t = rt_trie_new();
    rt_string k = make_key("test");
    void *v = new_obj();

    rt_trie_put(t, k, v);
    rt_trie_clear(t);

    assert(rt_trie_len(t) == 0);
    assert(rt_trie_is_empty(t) == 1);
    assert(rt_trie_has(t, k) == 0);

    rt_string_unref(k);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_keys()
{
    void *t = rt_trie_new();
    void *v = new_obj();
    rt_string k1 = make_key("banana");
    rt_string k2 = make_key("apple");
    rt_string k3 = make_key("cherry");

    rt_trie_put(t, k1, v);
    rt_trie_put(t, k2, v);
    rt_trie_put(t, k3, v);

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

static void test_empty_key()
{
    void *t = rt_trie_new();
    rt_string k = make_key("");
    void *v = new_obj();

    rt_trie_put(t, k, v);
    assert(rt_trie_len(t) == 1);
    assert(rt_trie_has(t, k) == 1);
    assert(rt_trie_get(t, k) == v);

    rt_string_unref(k);
    rt_release_obj(v);
    rt_release_obj(t);
}

static void test_null_safety()
{
    rt_string k = make_key("test");
    assert(rt_trie_len(NULL) == 0);
    assert(rt_trie_is_empty(NULL) == 1);
    assert(rt_trie_get(NULL, k) == NULL);
    assert(rt_trie_has(NULL, k) == 0);
    assert(rt_trie_has_prefix(NULL, k) == 0);
    assert(rt_trie_remove(NULL, k) == 0);
    rt_trie_put(NULL, k, NULL);
    rt_trie_clear(NULL);
    rt_string_unref(k);
}

int main()
{
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
    test_null_safety();
    return 0;
}
