//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTOOPDispatchTests.cpp
// Purpose: Validate rt_get_vfunc bounds checking and null handling. 
// Key invariants: Out-of-bounds slot access returns NULL; null object returns NULL.
// Ownership/Lifetime: Uses runtime library only.
// Links: docs/runtime-vm.md, docs/oop.md
//
//===----------------------------------------------------------------------===//

#include "runtime/rt_oop.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// Mock vtable with 3 function pointers
static void mock_func0(void) {}

static void mock_func1(void) {}

static void mock_func2(void) {}

static void *mock_vtable[3] = {(void *)mock_func0, (void *)mock_func1, (void *)mock_func2};

// Mock class info
static rt_class_info mock_class = {
    .type_id = 1, .qname = "TestClass", .base = NULL, .vtable = mock_vtable, .vtable_len = 3};

// Mock object
static rt_object mock_obj = {.vptr = mock_vtable};

int main()
{
    // Register the class so the runtime knows about it
    rt_register_class(&mock_class);

    // Test 1: Valid slot access should return function pointer
    void *func0 = rt_get_vfunc(&mock_obj, 0);
    assert(func0 == (void *)mock_func0);

    void *func1 = rt_get_vfunc(&mock_obj, 1);
    assert(func1 == (void *)mock_func1);

    void *func2 = rt_get_vfunc(&mock_obj, 2);
    assert(func2 == (void *)mock_func2);

    // Test 2: Out-of-bounds slot access should return NULL (BEFORE FIX: undefined behavior)
    void *func_invalid = rt_get_vfunc(&mock_obj, 3);
    assert(func_invalid == NULL);

    void *func_large = rt_get_vfunc(&mock_obj, 999);
    assert(func_large == NULL);

    // Test 3: Negative slot should return NULL
    void *func_negative = rt_get_vfunc(&mock_obj, -1);
    assert(func_negative == NULL);

    // Test 4: Null object should return NULL
    void *func_null_obj = rt_get_vfunc(NULL, 0);
    assert(func_null_obj == NULL);

    // Test 5: Object with null vptr should return NULL
    rt_object null_vptr_obj = {.vptr = NULL};
    void *func_null_vptr = rt_get_vfunc(&null_vptr_obj, 0);
    assert(func_null_vptr == NULL);

    return 0;
}
