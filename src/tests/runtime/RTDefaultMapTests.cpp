//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_defaultmap.h"
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

static void test_new()
{
    rt_string def = make_str("default");
    void *m = rt_defaultmap_new(def);
    assert(m != NULL);
    assert(rt_defaultmap_len(m) == 0);
}

static void test_get_default()
{
    rt_string def = make_str("DEFAULT");
    void *m = rt_defaultmap_new(def);

    rt_string key = make_str("missing");
    void *got = rt_defaultmap_get(m, key);
    assert(got == def);

    rt_string_unref(key);
}

static void test_set_and_get()
{
    rt_string def = make_str("DEFAULT");
    void *m = rt_defaultmap_new(def);

    rt_string k = make_str("key1");
    rt_string v = make_str("value1");
    rt_defaultmap_set(m, k, v);

    assert(rt_defaultmap_len(m) == 1);
    assert(rt_defaultmap_get(m, k) == v);

    rt_string_unref(k);
}

static void test_has()
{
    rt_string def = make_str("DEFAULT");
    void *m = rt_defaultmap_new(def);

    rt_string k = make_str("key");
    rt_string missing = make_str("nope");
    rt_string v = make_str("val");

    rt_defaultmap_set(m, k, v);
    assert(rt_defaultmap_has(m, k) == 1);
    assert(rt_defaultmap_has(m, missing) == 0);

    rt_string_unref(k);
    rt_string_unref(missing);
}

static void test_remove()
{
    rt_string def = make_str("DEFAULT");
    void *m = rt_defaultmap_new(def);

    rt_string k = make_str("key");
    rt_string v = make_str("val");

    rt_defaultmap_set(m, k, v);
    assert(rt_defaultmap_remove(m, k) == 1);
    assert(rt_defaultmap_len(m) == 0);

    // Now get should return default
    assert(rt_defaultmap_get(m, k) == def);

    rt_string_unref(k);
}

static void test_keys()
{
    rt_string def = make_str("D");
    void *m = rt_defaultmap_new(def);

    rt_string k1 = make_str("alpha");
    rt_string k2 = make_str("beta");
    rt_string v = make_str("v");

    rt_defaultmap_set(m, k1, v);
    rt_defaultmap_set(m, k2, v);

    void *keys = rt_defaultmap_keys(m);
    assert(rt_seq_len(keys) == 2);

    rt_string_unref(k1);
    rt_string_unref(k2);
}

static void test_get_default_value()
{
    rt_string def = make_str("MY_DEFAULT");
    void *m = rt_defaultmap_new(def);
    assert(rt_defaultmap_get_default(m) == def);
}

static void test_clear()
{
    rt_string def = make_str("D");
    void *m = rt_defaultmap_new(def);

    rt_string k = make_str("key");
    rt_string v = make_str("val");
    rt_defaultmap_set(m, k, v);

    rt_defaultmap_clear(m);
    assert(rt_defaultmap_len(m) == 0);

    rt_string_unref(k);
}

static void test_null_default()
{
    void *m = rt_defaultmap_new(NULL);
    rt_string k = make_str("key");
    assert(rt_defaultmap_get(m, k) == NULL);
    rt_string_unref(k);
}

static void test_null_safety()
{
    assert(rt_defaultmap_len(NULL) == 0);
    assert(rt_defaultmap_get(NULL, NULL) == NULL);
    assert(rt_defaultmap_has(NULL, NULL) == 0);
    assert(rt_defaultmap_remove(NULL, NULL) == 0);
    assert(rt_defaultmap_get_default(NULL) == NULL);
}

int main()
{
    test_new();
    test_get_default();
    test_set_and_get();
    test_has();
    test_remove();
    test_keys();
    test_get_default_value();
    test_clear();
    test_null_default();
    test_null_safety();

    return 0;
}
