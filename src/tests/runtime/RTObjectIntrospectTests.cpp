//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Object introspection methods: TypeName, TypeId, IsNull
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_box.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_msgbus.h"
#include "rt_option.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void msgbus_test_callback(void *data) {
    (void)data;
}

struct box_layout_probe {
    int64_t tag;
    int64_t payload;
};

int main() {
    // Test IsNull with null pointer
    assert(rt_obj_is_null(NULL) == 1);

    // Test IsNull with valid object
    void *obj = rt_obj_new_i64(42, 32);
    assert(obj != NULL);
    assert(rt_obj_is_null(obj) == 0);

    // Test TypeId returns class_id
    assert(rt_obj_type_id(obj) == 42);

    // Test TypeId with null
    assert(rt_obj_type_id(NULL) == 0);

    // Test TypeName with null
    rt_string name_null = rt_obj_type_name(NULL);
    assert(name_null != NULL);
    assert(strcmp(rt_string_cstr(name_null), "<null>") == 0);

    // Test TypeName with non-vtable object (falls back to "Object")
    rt_string name_obj = rt_obj_type_name(obj);
    assert(name_obj != NULL);
    assert(strcmp(rt_string_cstr(name_obj), "Object") == 0);
    rt_string_unref(name_obj);

    // String handles should report their built-in runtime type and ToString should retain.
    const char *heap_text =
        "this string is intentionally long enough to require a heap-backed runtime allocation";
    rt_string s = rt_string_from_bytes(heap_text, strlen(heap_text));
    rt_heap_hdr_t *hdr = s->heap;
    assert(hdr != NULL);
    size_t ref_before = hdr->refcnt;

    rt_string type_name = rt_obj_type_name(s);
    assert(strcmp(rt_string_cstr(type_name), "Viper.String") == 0);
    rt_string_unref(type_name);
    assert(rt_obj_type_id(s) == RT_STRING_CLASS_ID);

    rt_string str_again = rt_obj_to_string(s);
    assert(str_again == s);
    hdr = s->heap;
    assert(hdr != NULL);
    assert(hdr->refcnt == ref_before + 1);
    rt_string_unref(str_again);
    hdr = s->heap;
    assert(hdr != NULL);
    assert(hdr->refcnt == ref_before);
    assert(strcmp(rt_string_cstr(s), heap_text) == 0);
    rt_string_unref(s);

    void *box_a = rt_box_i64(42);
    void *box_b = rt_box_i64(42);
    assert(rt_obj_type_id(box_a) == RT_BOX_CLASS_ID);
    rt_string box_name = rt_obj_type_name(box_a);
    assert(strcmp(rt_string_cstr(box_name), "Viper.Core.Box") == 0);
    rt_string_unref(box_name);
    assert(rt_obj_equals(box_a, box_b) == 1);
    assert(rt_obj_get_hash_code(box_a) == rt_obj_get_hash_code(box_b));
    int64_t try_i64 = 0;
    double try_f64 = 0.0;
    assert(rt_box_try_to_i64(box_a, &try_i64) == 1);
    assert(try_i64 == 42);
    try_f64 = 123.0;
    assert(rt_box_try_to_f64(box_a, &try_f64) == 0);
    assert(try_f64 == 0.0);
    void *box_bool = rt_box_i1_bool(1);
    int8_t try_i1 = 0;
    assert(rt_box_try_to_i1(box_bool, &try_i1) == 1);
    assert(try_i1 == 1);
    void *zero_pos = rt_box_f64(0.0);
    void *zero_neg = rt_box_f64(-0.0);
    assert(rt_box_hash(zero_pos) == rt_box_hash(zero_neg));

    void *invalid_tag_box = rt_box_i64(5);
    ((box_layout_probe *)invalid_tag_box)->tag = 999;
    assert(rt_box_type(invalid_tag_box) == -1);
    int64_t invalid_try = 123;
    assert(rt_box_try_to_i64(invalid_tag_box, &invalid_try) == 0);
    assert(invalid_try == 0);

    rt_string same_a = rt_string_from_bytes("same string", 11);
    rt_string same_b = rt_string_from_bytes("same string", 11);
    assert(same_a != same_b);
    assert(rt_obj_equals(same_a, same_b) == 1);
    assert(rt_obj_get_hash_code(same_a) == rt_obj_get_hash_code(same_b));

    void *str_box = rt_box_str(same_a);
    rt_string try_str = NULL;
    assert(rt_box_try_to_str(str_box, &try_str) == 1);
    assert(try_str != NULL);
    assert(rt_str_eq(try_str, same_a) == 1);
    rt_string_unref(try_str);

    void *opt = rt_option_some_i64(7);
    assert(rt_obj_type_id(opt) == RT_OPTION_CLASS_ID);
    rt_string opt_name = rt_obj_type_name(opt);
    assert(strcmp(rt_string_cstr(opt_name), "Viper.Option") == 0);
    rt_string_unref(opt_name);
    rt_string opt_string = rt_obj_to_string(opt);
    assert(strcmp(rt_string_cstr(opt_string), "Viper.Option") == 0);
    rt_string_unref(opt_string);

    void *value_type = rt_box_value_type(8);
    assert(rt_obj_type_id(value_type) == RT_VALUE_TYPE_CLASS_ID);
    rt_string value_name = rt_obj_type_name(value_type);
    assert(strcmp(rt_string_cstr(value_name), "Viper.Core.ValueType") == 0);
    rt_string_unref(value_name);
    rt_string value_string = rt_obj_to_string(value_type);
    assert(strcmp(rt_string_cstr(value_string), "Viper.Core.ValueType") == 0);
    rt_string_unref(value_string);

    void *bus = rt_msgbus_new();
    assert(rt_obj_type_id(bus) == RT_MSGBUS_CLASS_ID);
    rt_string bus_name = rt_obj_type_name(bus);
    assert(strcmp(rt_string_cstr(bus_name), "Viper.Core.MessageBus") == 0);
    rt_string_unref(bus_name);
    rt_string bus_string = rt_obj_to_string(bus);
    assert(strcmp(rt_string_cstr(bus_string), "Viper.Core.MessageBus") == 0);
    rt_string_unref(bus_string);

    void *callback = rt_msgbus_callback_new(msgbus_test_callback);
    assert(rt_obj_type_id(callback) == RT_MSGBUS_CALLBACK_CLASS_ID);
    rt_string callback_name = rt_obj_type_name(callback);
    assert(strcmp(rt_string_cstr(callback_name), "Viper.Core.MessageBus.Callback") == 0);
    rt_string_unref(callback_name);

    rt_obj_release_check0(obj);
    rt_obj_free(obj);
    rt_obj_release_check0(box_a);
    rt_obj_free(box_a);
    rt_obj_release_check0(box_b);
    rt_obj_free(box_b);
    rt_obj_release_check0(box_bool);
    rt_obj_free(box_bool);
    rt_obj_release_check0(zero_pos);
    rt_obj_free(zero_pos);
    rt_obj_release_check0(zero_neg);
    rt_obj_free(zero_neg);
    rt_obj_release_check0(invalid_tag_box);
    rt_obj_free(invalid_tag_box);
    rt_obj_release_check0(str_box);
    rt_obj_free(str_box);
    rt_string_unref(same_a);
    rt_string_unref(same_b);
    rt_obj_release_check0(opt);
    rt_obj_free(opt);
    rt_obj_release_check0(value_type);
    rt_obj_free(value_type);
    rt_obj_release_check0(callback);
    rt_obj_free(callback);
    rt_obj_release_check0(bus);
    rt_obj_free(bus);

    return 0;
}
