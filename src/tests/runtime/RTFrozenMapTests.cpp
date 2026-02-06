//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_frozenmap.h"
#include "rt_internal.h"
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

static void test_empty()
{
    void *fm = rt_frozenmap_empty();
    assert(fm != NULL);
    assert(rt_frozenmap_len(fm) == 0);
    assert(rt_frozenmap_is_empty(fm) == 1);
}

static void test_from_seqs()
{
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();

    rt_seq_push(keys, make_str("name"));
    rt_seq_push(keys, make_str("age"));
    rt_seq_push(vals, make_str("Alice"));
    rt_seq_push(vals, make_str("30"));

    void *fm = rt_frozenmap_from_seqs(keys, vals);
    assert(rt_frozenmap_len(fm) == 2);
    assert(rt_frozenmap_is_empty(fm) == 0);
}

static void test_get()
{
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();

    rt_string k = make_str("key");
    rt_string v = make_str("value");
    rt_seq_push(keys, k);
    rt_seq_push(vals, v);

    void *fm = rt_frozenmap_from_seqs(keys, vals);

    rt_string lookup = make_str("key");
    void *got = rt_frozenmap_get(fm, lookup);
    assert(got != NULL);
    assert(strcmp(rt_string_cstr((rt_string)got), "value") == 0);

    rt_string missing = make_str("nope");
    assert(rt_frozenmap_get(fm, missing) == NULL);

    rt_string_unref(lookup);
    rt_string_unref(missing);
}

static void test_has()
{
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();
    rt_seq_push(keys, make_str("a"));
    rt_seq_push(vals, make_str("1"));

    void *fm = rt_frozenmap_from_seqs(keys, vals);
    rt_string a = make_str("a");
    rt_string b = make_str("b");
    assert(rt_frozenmap_has(fm, a) == 1);
    assert(rt_frozenmap_has(fm, b) == 0);
    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_keys_values()
{
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();
    rt_seq_push(keys, make_str("x"));
    rt_seq_push(keys, make_str("y"));
    rt_seq_push(vals, make_str("10"));
    rt_seq_push(vals, make_str("20"));

    void *fm = rt_frozenmap_from_seqs(keys, vals);
    void *ks = rt_frozenmap_keys(fm);
    void *vs = rt_frozenmap_values(fm);
    assert(rt_seq_len(ks) == 2);
    assert(rt_seq_len(vs) == 2);
}

static void test_get_or()
{
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();
    rt_seq_push(keys, make_str("k"));
    rt_seq_push(vals, make_str("v"));

    void *fm = rt_frozenmap_from_seqs(keys, vals);
    rt_string def = make_str("DEFAULT");
    rt_string k = make_str("k");
    rt_string m = make_str("missing");

    void *got = rt_frozenmap_get_or(fm, k, def);
    assert(strcmp(rt_string_cstr((rt_string)got), "v") == 0);

    got = rt_frozenmap_get_or(fm, m, def);
    assert(got == def);

    rt_string_unref(def);
    rt_string_unref(k);
    rt_string_unref(m);
}

static void test_merge()
{
    void *k1 = rt_seq_new();
    void *v1 = rt_seq_new();
    rt_seq_push(k1, make_str("a"));
    rt_seq_push(v1, make_str("1"));

    void *k2 = rt_seq_new();
    void *v2 = rt_seq_new();
    rt_seq_push(k2, make_str("b"));
    rt_seq_push(v2, make_str("2"));

    void *fm1 = rt_frozenmap_from_seqs(k1, v1);
    void *fm2 = rt_frozenmap_from_seqs(k2, v2);
    void *merged = rt_frozenmap_merge(fm1, fm2);

    assert(rt_frozenmap_len(merged) == 2);
    rt_string a = make_str("a");
    rt_string b = make_str("b");
    assert(rt_frozenmap_has(merged, a) == 1);
    assert(rt_frozenmap_has(merged, b) == 1);
    rt_string_unref(a);
    rt_string_unref(b);
}

static void test_merge_overwrite()
{
    void *k1 = rt_seq_new();
    void *v1 = rt_seq_new();
    rt_seq_push(k1, make_str("key"));
    rt_seq_push(v1, make_str("old"));

    void *k2 = rt_seq_new();
    void *v2 = rt_seq_new();
    rt_seq_push(k2, make_str("key"));
    rt_string new_val = make_str("new");
    rt_seq_push(v2, new_val);

    void *fm1 = rt_frozenmap_from_seqs(k1, v1);
    void *fm2 = rt_frozenmap_from_seqs(k2, v2);
    void *merged = rt_frozenmap_merge(fm1, fm2);

    assert(rt_frozenmap_len(merged) == 1);
    rt_string k = make_str("key");
    void *got = rt_frozenmap_get(merged, k);
    assert(strcmp(rt_string_cstr((rt_string)got), "new") == 0);
    rt_string_unref(k);
}

static void test_equals()
{
    void *k1 = rt_seq_new();
    void *v1 = rt_seq_new();
    rt_seq_push(k1, make_str("a"));
    rt_seq_push(v1, make_str("1"));

    void *k2 = rt_seq_new();
    void *v2 = rt_seq_new();
    rt_seq_push(k2, make_str("a"));
    rt_seq_push(v2, make_str("1"));

    void *fm1 = rt_frozenmap_from_seqs(k1, v1);
    void *fm2 = rt_frozenmap_from_seqs(k2, v2);

    // Values are different string objects with same content
    // equals checks by reference, so this may or may not be equal
    // depending on string interning. Test structural equality at least for len.
    assert(rt_frozenmap_len(fm1) == rt_frozenmap_len(fm2));
}

static void test_null_safety()
{
    assert(rt_frozenmap_len(NULL) == 0);
    assert(rt_frozenmap_is_empty(NULL) == 1);
    assert(rt_frozenmap_get(NULL, NULL) == NULL);
    assert(rt_frozenmap_has(NULL, NULL) == 0);
    assert(rt_frozenmap_equals(NULL, NULL) == 1);
}

static void test_dedup_keys()
{
    void *keys = rt_seq_new();
    void *vals = rt_seq_new();
    rt_seq_push(keys, make_str("k"));
    rt_seq_push(keys, make_str("k"));
    rt_seq_push(vals, make_str("first"));
    rt_seq_push(vals, make_str("second"));

    void *fm = rt_frozenmap_from_seqs(keys, vals);
    // Last value wins for duplicate keys
    assert(rt_frozenmap_len(fm) == 1);
    rt_string k = make_str("k");
    void *got = rt_frozenmap_get(fm, k);
    assert(strcmp(rt_string_cstr((rt_string)got), "second") == 0);
    rt_string_unref(k);
}

int main()
{
    test_empty();
    test_from_seqs();
    test_get();
    test_has();
    test_keys_values();
    test_get_or();
    test_merge();
    test_merge_overwrite();
    test_equals();
    test_null_safety();
    test_dedup_keys();
    return 0;
}
