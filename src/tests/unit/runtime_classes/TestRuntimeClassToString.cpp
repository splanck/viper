//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestRuntimeClassToString.cpp
// Purpose: Verify that registering a class enables Object.ToString to print
//          the qualified type name using the per-VM type registry.
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

extern "C"
{
#include "rt_context.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_oop.h"
#include "rt_string.h"
}

TEST(RuntimeClasses, ToString_UsesRegisteredQName)
{
    RtContext ctx{};
    rt_context_init(&ctx);
    rt_set_current_context(&ctx);

    // Allocate a dummy vtable (at least one slot for stability)
    void **vtbl = (void **)rt_heap_alloc(RT_HEAP_OBJECT, RT_ELEM_NONE, 1, 8, 8);
    ASSERT_TRUE(vtbl != nullptr);

    // Register class with qname "A.Person" and a fake type id
    rt_register_class_direct(1234, vtbl, "A.Person", 0);

    // Allocate object and set its vptr to the registered table
    void *obj = rt_obj_new_i64(1234, 8);
    ASSERT_TRUE(obj != nullptr);
    ((rt_object *)obj)->vptr = vtbl;

    rt_string s = rt_obj_to_string(obj);
    ASSERT_TRUE(s != nullptr);
    const char *bytes = rt_string_cstr(s);
    ASSERT_TRUE(bytes != nullptr);

    // Expect exact qualified name
    std::string got(bytes, bytes + rt_len(s));
    ASSERT_TRUE(got == "A.Person");

    rt_set_current_context(nullptr);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
