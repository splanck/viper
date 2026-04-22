//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTIntMapTests.cpp
// Purpose: Tests for Viper.Collections.IntMap runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_box.h"
#include "rt_internal.h"
#include "rt_intmap.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static void release_obj(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void test_set_get_remove() {
    void *map = rt_intmap_new();
    assert(map != nullptr);
    assert(rt_intmap_is_empty(map) == 1);

    rt_string a = make_str("a");
    rt_string b = make_str("b");
    rt_intmap_set(map, -7, a);
    rt_intmap_set(map, 42, b);

    assert(rt_intmap_len(map) == 2);
    assert(rt_intmap_get(map, -7) == a);
    assert(rt_intmap_get(map, 42) == b);
    assert(rt_intmap_has(map, 9) == 0);
    assert(rt_intmap_remove(map, -7) == 1);
    assert(rt_intmap_len(map) == 1);
    assert(rt_intmap_get(map, -7) == nullptr);

    rt_string_unref(a);
    rt_string_unref(b);
    release_obj(map);
}

static void test_keys_are_boxed_i64_values() {
    void *map = rt_intmap_new();
    rt_string a = make_str("a");
    rt_string b = make_str("b");
    rt_intmap_set(map, INT64_MIN + 1, a);
    rt_intmap_set(map, INT64_MAX - 1, b);

    void *keys = rt_intmap_keys(map);
    assert(rt_seq_len(keys) == 2);

    bool found_min = false;
    bool found_max = false;
    for (int64_t i = 0; i < rt_seq_len(keys); ++i) {
        void *boxed = rt_seq_get(keys, i);
        assert(rt_box_type(boxed) == RT_BOX_I64);
        int64_t key = rt_unbox_i64(boxed);
        found_min = found_min || key == INT64_MIN + 1;
        found_max = found_max || key == INT64_MAX - 1;
    }
    assert(found_min);
    assert(found_max);

    void *values = rt_intmap_values(map);
    assert(rt_seq_len(values) == 2);

    rt_string_unref(a);
    rt_string_unref(b);
    release_obj(values);
    release_obj(keys);
    release_obj(map);
}

int main() {
    test_set_get_remove();
    test_keys_are_boxed_i64_values();
    return 0;
}
