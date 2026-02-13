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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main()
{
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

    rt_obj_release_check0(obj);
    rt_obj_free(obj);

    return 0;
}
