//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTypeRegistryTests.cpp
// Purpose: Validate rt_type_is_a, rt_type_implements, and base class wiring
//          in the runtime type registry.
// Key invariants: Base classes must be registered before derived classes.
//                 Interface bindings are inherited through the base chain.
// Ownership/Lifetime: Uses runtime library only.
// Links: src/runtime/rt_type_registry.c, src/runtime/rt_oop.h
//
//===----------------------------------------------------------------------===//

#include "rt_oop.h"
#include <assert.h>
#include <csetjmp>
#include <stddef.h>
#include <stdint.h>
#include <string>

extern "C" void rt_trap_set_recovery(jmp_buf *buf);
extern "C" void rt_trap_clear_recovery(void);
extern "C" const char *rt_trap_get_error(void);

// Mock vtables for test classes
static void *vtable_base[1] = {nullptr};
static void *vtable_derived[1] = {nullptr};
static void *vtable_leaf[1] = {nullptr};
static void *vtable_unrelated[1] = {nullptr};
static void *vtable_conflict[1] = {nullptr};

// Mock interface table
static void mock_iface_method(void) {}

static void *itable_base[1] = {(void *)mock_iface_method};
static void *itable_conflict[1] = {(void *)mock_iface_method};

static void expect_trap(void (*fn)(), const char *text) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    } else {
        std::string message = rt_trap_get_error();
        rt_trap_clear_recovery();
        assert(message.find(text) != std::string::npos);
    }
}

static void register_duplicate_class_conflict() {
    rt_register_class_with_base(100, vtable_conflict, "Test.BaseConflict", 0, -1);
}

static void register_missing_base() {
    rt_register_class_with_base(300, vtable_conflict, "Test.MissingBase", 0, 299);
}

static void bind_unknown_class() {
    rt_bind_interface(9999, 1, itable_base);
}

static void bind_unknown_interface() {
    rt_bind_interface(100, 9999, itable_base);
}

static void bind_duplicate_conflict() {
    rt_bind_interface(100, 1, itable_conflict);
}

int main() {
    // Type IDs for our test classes
    const int TYPE_BASE = 100;
    const int TYPE_DERIVED = 101;
    const int TYPE_LEAF = 102;
    const int TYPE_UNRELATED = 200;
    const int IFACE_TESTABLE = 1;

    // Register base class first (no base)
    rt_register_class_with_base(TYPE_BASE, vtable_base, "Test.Base", 0, -1);

    // Register derived class with base
    rt_register_class_with_base(TYPE_DERIVED, vtable_derived, "Test.Derived", 0, TYPE_BASE);

    // Register leaf class (3-level chain: Leaf -> Derived -> Base)
    rt_register_class_with_base(TYPE_LEAF, vtable_leaf, "Test.Leaf", 0, TYPE_DERIVED);

    // Register unrelated class (no base)
    rt_register_class_with_base(TYPE_UNRELATED, vtable_unrelated, "Test.Unrelated", 0, -1);

    // Register an interface
    rt_register_interface_direct(IFACE_TESTABLE, "Test.ITestable", 1);

    // Bind interface only to Base class
    rt_bind_interface(TYPE_BASE, IFACE_TESTABLE, itable_base);
    rt_register_class_with_base(TYPE_BASE, vtable_base, "Test.Base", 0, -1);
    rt_register_interface_direct(IFACE_TESTABLE, "Test.ITestable", 1);
    rt_bind_interface(TYPE_BASE, IFACE_TESTABLE, itable_base);

    assert(rt_typeid_of(NULL) == 0);
    assert(rt_type_is_a(9999, 9999) == 0);
    assert(rt_type_implements(-1, IFACE_TESTABLE) == 0);
    assert(rt_type_implements(TYPE_BASE, -1) == 0);
    assert(rt_cast_as(nullptr, -1) == nullptr);
    assert(rt_cast_as_iface(nullptr, -1) == nullptr);
    assert(rt_get_class_vtable(-1) == nullptr);
    expect_trap(register_duplicate_class_conflict, "duplicate class type id");
    expect_trap(register_missing_base, "base class is not registered");
    expect_trap(bind_unknown_class, "unknown class");
    expect_trap(bind_unknown_interface, "unknown interface");
    expect_trap(bind_duplicate_conflict, "duplicate interface binding");

    // =====================================================================
    // Test 1: rt_type_is_a for same type
    // =====================================================================
    assert(rt_type_is_a(TYPE_BASE, TYPE_BASE) == 1);
    assert(rt_type_is_a(TYPE_DERIVED, TYPE_DERIVED) == 1);
    assert(rt_type_is_a(TYPE_LEAF, TYPE_LEAF) == 1);

    // =====================================================================
    // Test 2: rt_type_is_a for direct inheritance (Derived -> Base)
    // =====================================================================
    assert(rt_type_is_a(TYPE_DERIVED, TYPE_BASE) == 1);
    assert(rt_type_is_a(TYPE_BASE, TYPE_DERIVED) == 0); // Base is NOT a Derived

    // =====================================================================
    // Test 3: rt_type_is_a for deep inheritance chain (Leaf -> Derived -> Base)
    // =====================================================================
    assert(rt_type_is_a(TYPE_LEAF, TYPE_BASE) == 1);
    assert(rt_type_is_a(TYPE_LEAF, TYPE_DERIVED) == 1);
    assert(rt_type_is_a(TYPE_BASE, TYPE_LEAF) == 0);
    assert(rt_type_is_a(TYPE_DERIVED, TYPE_LEAF) == 0);

    // =====================================================================
    // Test 4: rt_type_is_a for unrelated classes
    // =====================================================================
    assert(rt_type_is_a(TYPE_UNRELATED, TYPE_BASE) == 0);
    assert(rt_type_is_a(TYPE_BASE, TYPE_UNRELATED) == 0);
    assert(rt_type_is_a(TYPE_DERIVED, TYPE_UNRELATED) == 0);
    assert(rt_type_is_a(TYPE_LEAF, TYPE_UNRELATED) == 0);

    // =====================================================================
    // Test 5: rt_type_implements for direct binding
    // =====================================================================
    assert(rt_type_implements(TYPE_BASE, IFACE_TESTABLE) == 1);

    // =====================================================================
    // Test 6: rt_type_implements inherited through base class chain
    // =====================================================================
    assert(rt_type_implements(TYPE_DERIVED, IFACE_TESTABLE) == 1);
    assert(rt_type_implements(TYPE_LEAF, IFACE_TESTABLE) == 1);

    // =====================================================================
    // Test 7: rt_type_implements for unrelated class (not bound)
    // =====================================================================
    assert(rt_type_implements(TYPE_UNRELATED, IFACE_TESTABLE) == 0);

    // =====================================================================
    // Test 8: rt_itable_lookup through inheritance
    // =====================================================================
    // Create mock objects with the vtables
    rt_object obj_base = {.vptr = vtable_base};
    rt_object obj_derived = {.vptr = vtable_derived};
    rt_object obj_leaf = {.vptr = vtable_leaf};
    rt_object obj_unrelated = {.vptr = vtable_unrelated};

    // Base should return its own itable
    void **itable_from_base = rt_itable_lookup(&obj_base, IFACE_TESTABLE);
    assert(itable_from_base == itable_base);

    // Derived should find the interface through Base
    void **itable_from_derived = rt_itable_lookup(&obj_derived, IFACE_TESTABLE);
    assert(itable_from_derived == itable_base);

    // Leaf should find the interface through Derived -> Base
    void **itable_from_leaf = rt_itable_lookup(&obj_leaf, IFACE_TESTABLE);
    assert(itable_from_leaf == itable_base);

    // Unrelated should return NULL
    void **itable_from_unrelated = rt_itable_lookup(&obj_unrelated, IFACE_TESTABLE);
    assert(itable_from_unrelated == NULL);

    // =====================================================================
    // Test 9: rt_cast_as with inheritance
    // =====================================================================
    void *cast_derived_to_base = rt_cast_as(&obj_derived, TYPE_BASE);
    assert(cast_derived_to_base == &obj_derived);

    void *cast_leaf_to_base = rt_cast_as(&obj_leaf, TYPE_BASE);
    assert(cast_leaf_to_base == &obj_leaf);

    void *cast_base_to_derived = rt_cast_as(&obj_base, TYPE_DERIVED);
    assert(cast_base_to_derived == NULL); // Base is not Derived

    // =====================================================================
    // Test 10: rt_cast_as_iface with inherited interface
    // =====================================================================
    void *iface_cast_base = rt_cast_as_iface(&obj_base, IFACE_TESTABLE);
    assert(iface_cast_base == &obj_base);

    void *iface_cast_derived = rt_cast_as_iface(&obj_derived, IFACE_TESTABLE);
    assert(iface_cast_derived == &obj_derived);

    void *iface_cast_leaf = rt_cast_as_iface(&obj_leaf, IFACE_TESTABLE);
    assert(iface_cast_leaf == &obj_leaf);

    void *iface_cast_unrelated = rt_cast_as_iface(&obj_unrelated, IFACE_TESTABLE);
    assert(iface_cast_unrelated == NULL);

    return 0;
}
