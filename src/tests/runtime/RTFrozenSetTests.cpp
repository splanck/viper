//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_frozenset.h"
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
    void *fs = rt_frozenset_empty();
    assert(fs != NULL);
    assert(rt_frozenset_len(fs) == 0);
    assert(rt_frozenset_is_empty(fs) == 1);
}

static void test_from_seq()
{
    void *seq = rt_seq_new();
    rt_seq_push(seq, make_str("apple"));
    rt_seq_push(seq, make_str("banana"));
    rt_seq_push(seq, make_str("cherry"));

    void *fs = rt_frozenset_from_seq(seq);
    assert(rt_frozenset_len(fs) == 3);
    assert(rt_frozenset_is_empty(fs) == 0);
}

static void test_has()
{
    void *seq = rt_seq_new();
    rt_seq_push(seq, make_str("alpha"));
    rt_seq_push(seq, make_str("beta"));

    void *fs = rt_frozenset_from_seq(seq);
    rt_string a = make_str("alpha");
    rt_string b = make_str("beta");
    rt_string c = make_str("gamma");

    assert(rt_frozenset_has(fs, a) == 1);
    assert(rt_frozenset_has(fs, b) == 1);
    assert(rt_frozenset_has(fs, c) == 0);

    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(c);
}

static void test_dedup()
{
    void *seq = rt_seq_new();
    rt_seq_push(seq, make_str("dup"));
    rt_seq_push(seq, make_str("dup"));
    rt_seq_push(seq, make_str("dup"));
    rt_seq_push(seq, make_str("unique"));

    void *fs = rt_frozenset_from_seq(seq);
    assert(rt_frozenset_len(fs) == 2);
}

static void test_items()
{
    void *seq = rt_seq_new();
    rt_seq_push(seq, make_str("x"));
    rt_seq_push(seq, make_str("y"));

    void *fs = rt_frozenset_from_seq(seq);
    void *items = rt_frozenset_items(fs);
    assert(rt_seq_len(items) == 2);
}

static void test_union()
{
    void *s1 = rt_seq_new();
    rt_seq_push(s1, make_str("a"));
    rt_seq_push(s1, make_str("b"));

    void *s2 = rt_seq_new();
    rt_seq_push(s2, make_str("b"));
    rt_seq_push(s2, make_str("c"));

    void *fs1 = rt_frozenset_from_seq(s1);
    void *fs2 = rt_frozenset_from_seq(s2);

    void *u = rt_frozenset_union(fs1, fs2);
    assert(rt_frozenset_len(u) == 3);

    rt_string a = make_str("a");
    rt_string b = make_str("b");
    rt_string c = make_str("c");
    assert(rt_frozenset_has(u, a) == 1);
    assert(rt_frozenset_has(u, b) == 1);
    assert(rt_frozenset_has(u, c) == 1);

    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(c);
}

static void test_intersect()
{
    void *s1 = rt_seq_new();
    rt_seq_push(s1, make_str("a"));
    rt_seq_push(s1, make_str("b"));
    rt_seq_push(s1, make_str("c"));

    void *s2 = rt_seq_new();
    rt_seq_push(s2, make_str("b"));
    rt_seq_push(s2, make_str("c"));
    rt_seq_push(s2, make_str("d"));

    void *fs1 = rt_frozenset_from_seq(s1);
    void *fs2 = rt_frozenset_from_seq(s2);

    void *inter = rt_frozenset_intersect(fs1, fs2);
    assert(rt_frozenset_len(inter) == 2);

    rt_string b = make_str("b");
    rt_string c = make_str("c");
    assert(rt_frozenset_has(inter, b) == 1);
    assert(rt_frozenset_has(inter, c) == 1);

    rt_string_unref(b);
    rt_string_unref(c);
}

static void test_diff()
{
    void *s1 = rt_seq_new();
    rt_seq_push(s1, make_str("a"));
    rt_seq_push(s1, make_str("b"));
    rt_seq_push(s1, make_str("c"));

    void *s2 = rt_seq_new();
    rt_seq_push(s2, make_str("b"));

    void *fs1 = rt_frozenset_from_seq(s1);
    void *fs2 = rt_frozenset_from_seq(s2);

    void *d = rt_frozenset_diff(fs1, fs2);
    assert(rt_frozenset_len(d) == 2);

    rt_string a = make_str("a");
    rt_string b = make_str("b");
    rt_string c = make_str("c");
    assert(rt_frozenset_has(d, a) == 1);
    assert(rt_frozenset_has(d, b) == 0);
    assert(rt_frozenset_has(d, c) == 1);

    rt_string_unref(a);
    rt_string_unref(b);
    rt_string_unref(c);
}

static void test_is_subset()
{
    void *s1 = rt_seq_new();
    rt_seq_push(s1, make_str("a"));
    rt_seq_push(s1, make_str("b"));

    void *s2 = rt_seq_new();
    rt_seq_push(s2, make_str("a"));
    rt_seq_push(s2, make_str("b"));
    rt_seq_push(s2, make_str("c"));

    void *fs1 = rt_frozenset_from_seq(s1);
    void *fs2 = rt_frozenset_from_seq(s2);

    assert(rt_frozenset_is_subset(fs1, fs2) == 1);
    assert(rt_frozenset_is_subset(fs2, fs1) == 0);
}

static void test_equals()
{
    void *s1 = rt_seq_new();
    rt_seq_push(s1, make_str("x"));
    rt_seq_push(s1, make_str("y"));

    void *s2 = rt_seq_new();
    rt_seq_push(s2, make_str("y"));
    rt_seq_push(s2, make_str("x"));

    void *fs1 = rt_frozenset_from_seq(s1);
    void *fs2 = rt_frozenset_from_seq(s2);

    assert(rt_frozenset_equals(fs1, fs2) == 1);

    void *s3 = rt_seq_new();
    rt_seq_push(s3, make_str("x"));
    void *fs3 = rt_frozenset_from_seq(s3);
    assert(rt_frozenset_equals(fs1, fs3) == 0);
}

static void test_null_safety()
{
    assert(rt_frozenset_len(NULL) == 0);
    assert(rt_frozenset_is_empty(NULL) == 1);
    assert(rt_frozenset_has(NULL, NULL) == 0);
    assert(rt_frozenset_is_subset(NULL, NULL) == 1);
    assert(rt_frozenset_equals(NULL, NULL) == 1);
}

int main()
{
    test_empty();
    test_from_seq();
    test_has();
    test_dedup();
    test_items();
    test_union();
    test_intersect();
    test_diff();
    test_is_subset();
    test_equals();
    test_null_safety();
    return 0;
}
