//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTypeRegistryContextIsolationTests.cpp
// Purpose: Ensure legacy type registry state migrates into a bound RtContext and
//          back out on unbind to preserve pre-context runtime behaviour.
// Key invariants: Entries registered with no active context remain visible after
//                 binding/unbinding a fresh context.
// Ownership/Lifetime: Uses runtime library only.
// Links: src/runtime/rt_context.c, src/runtime/rt_type_registry.c
//
//===----------------------------------------------------------------------===//

#include "rt_context.h"
#include "rt_oop.h"
#include <assert.h>

static void *vtable_dummy[1] = {nullptr};

int main()
{
    constexpr int TYPE_ID = 1000001;

    RtContext ctx;
    rt_context_init(&ctx);

    rt_set_current_context(nullptr);

    // Register in the legacy registry (no active context).
    rt_register_class_direct(TYPE_ID, vtable_dummy, "Test.Legacy", 0);
    assert(rt_get_class_vtable(TYPE_ID) == vtable_dummy);

    // Bind a fresh context; it should adopt the legacy registry.
    rt_set_current_context(&ctx);
    assert(rt_get_class_vtable(TYPE_ID) == vtable_dummy);

    // Unbind; state should be moved back to legacy.
    rt_set_current_context(nullptr);
    assert(rt_get_class_vtable(TYPE_ID) == vtable_dummy);

    rt_context_cleanup(&ctx);
    return 0;
}
