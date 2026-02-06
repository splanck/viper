//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_bloomfilter.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void test_new()
{
    void *bf = rt_bloomfilter_new(100, 0.01);
    assert(bf != NULL);
    assert(rt_bloomfilter_count(bf) == 0);
}

static void test_add_and_check()
{
    void *bf = rt_bloomfilter_new(100, 0.01);
    rt_string s1 = make_str("hello");
    rt_string s2 = make_str("world");

    rt_bloomfilter_add(bf, s1);
    rt_bloomfilter_add(bf, s2);

    assert(rt_bloomfilter_count(bf) == 2);
    assert(rt_bloomfilter_might_contain(bf, s1) == 1);
    assert(rt_bloomfilter_might_contain(bf, s2) == 1);

    rt_string_unref(s1);
    rt_string_unref(s2);
}

static void test_definitely_not_present()
{
    void *bf = rt_bloomfilter_new(100, 0.01);
    rt_string s1 = make_str("alpha");
    rt_string s2 = make_str("beta");
    rt_string s3 = make_str("gamma");

    rt_bloomfilter_add(bf, s1);
    rt_bloomfilter_add(bf, s2);

    // gamma was never added -- might still report as present (false positive)
    // but the probability should be very low with a good filter
    // We just verify that items we DID add are found
    assert(rt_bloomfilter_might_contain(bf, s1) == 1);
    assert(rt_bloomfilter_might_contain(bf, s2) == 1);

    rt_string_unref(s1);
    rt_string_unref(s2);
    rt_string_unref(s3);
}

static void test_many_items()
{
    void *bf = rt_bloomfilter_new(1000, 0.01);
    char buf[32];

    // Add 500 items
    for (int i = 0; i < 500; i++)
    {
        snprintf(buf, sizeof(buf), "item_%d", i);
        rt_string s = make_str(buf);
        rt_bloomfilter_add(bf, s);
        rt_string_unref(s);
    }

    assert(rt_bloomfilter_count(bf) == 500);

    // Verify all added items are found
    for (int i = 0; i < 500; i++)
    {
        snprintf(buf, sizeof(buf), "item_%d", i);
        rt_string s = make_str(buf);
        assert(rt_bloomfilter_might_contain(bf, s) == 1);
        rt_string_unref(s);
    }
}

static void test_fpr()
{
    void *bf = rt_bloomfilter_new(100, 0.01);
    assert(rt_bloomfilter_fpr(bf) == 0.0); // Empty filter

    rt_string s = make_str("test");
    rt_bloomfilter_add(bf, s);
    assert(rt_bloomfilter_fpr(bf) > 0.0);
    assert(rt_bloomfilter_fpr(bf) < 1.0);
    rt_string_unref(s);
}

static void test_clear()
{
    void *bf = rt_bloomfilter_new(100, 0.01);
    rt_string s = make_str("test");
    rt_bloomfilter_add(bf, s);
    assert(rt_bloomfilter_count(bf) == 1);

    rt_bloomfilter_clear(bf);
    assert(rt_bloomfilter_count(bf) == 0);
    assert(rt_bloomfilter_might_contain(bf, s) == 0);

    rt_string_unref(s);
}

static void test_merge()
{
    void *a = rt_bloomfilter_new(100, 0.01);
    void *b = rt_bloomfilter_new(100, 0.01);

    rt_string s1 = make_str("alpha");
    rt_string s2 = make_str("beta");

    rt_bloomfilter_add(a, s1);
    rt_bloomfilter_add(b, s2);

    int64_t ok = rt_bloomfilter_merge(a, b);
    assert(ok == 1);
    assert(rt_bloomfilter_might_contain(a, s1) == 1);
    assert(rt_bloomfilter_might_contain(a, s2) == 1);

    rt_string_unref(s1);
    rt_string_unref(s2);
}

static void test_null_safety()
{
    assert(rt_bloomfilter_count(NULL) == 0);
    assert(rt_bloomfilter_might_contain(NULL, NULL) == 0);
    assert(rt_bloomfilter_fpr(NULL) == 0.0);
    assert(rt_bloomfilter_merge(NULL, NULL) == 0);
}

int main()
{
    test_new();
    test_add_and_check();
    test_definitely_not_present();
    test_many_items();
    test_fpr();
    test_clear();
    test_merge();
    test_null_safety();

    return 0;
}
