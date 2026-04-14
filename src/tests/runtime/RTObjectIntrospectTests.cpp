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
#include "rt_heap.h"
#include "rt_internal.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

    rt_obj_release_check0(obj);
    rt_obj_free(obj);

    return 0;
}
