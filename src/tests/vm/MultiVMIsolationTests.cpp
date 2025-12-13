//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/MultiVMIsolationTests.cpp
// Purpose: Verify per-VM isolation for RNG, module variables, file channels,
//          and runtime type registry (class/interface registration).
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include "rt_context.h"
#include "rt_args.h"
#include "rt_file.h"
#include "rt_modvar.h"
#include "rt_oop.h"
#include "rt_random.h"
#include "viper/runtime/rt.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

static std::string makeTempPath(const char *tag)
{
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path();
    auto path = dir / (std::string("viper_multivm_") + tag + ".txt");
    return path.string();
}

TEST(MultiVMIsolation, RNG_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);

    // Seed and advance A
    rt_set_current_context(&a);
    rt_randomize_i64(42);
    double a0 = rt_rnd();
    double a1 = rt_rnd();
    double a2 = rt_rnd();

    // Seed and advance B – must not be affected by A
    rt_set_current_context(&b);
    rt_randomize_i64(42);
    double b0 = rt_rnd();
    double b1 = rt_rnd();
    double b2 = rt_rnd();

    auto approx_eq = [](double x, double y) { return std::fabs(x - y) < 1e-12; };
    ASSERT_TRUE(approx_eq(a0, b0));
    ASSERT_TRUE(approx_eq(a1, b1));
    ASSERT_TRUE(approx_eq(a2, b2));

    // Advance A further; B should continue its own sequence unaffected
    rt_set_current_context(&a);
    double a3 = rt_rnd();
    (void)a3; // unused but ensures state advances
    rt_set_current_context(&b);
    double b3 = rt_rnd();
    // Recompute expected b3 by reseeding a temp context
    RtContext tmp{};
    rt_context_init(&tmp);
    rt_set_current_context(&tmp);
    rt_randomize_i64(42);
    (void)rt_rnd();
    (void)rt_rnd();
    (void)rt_rnd();
    double expected_b3 = rt_rnd();
    ASSERT_TRUE(approx_eq(b3, expected_b3));
}

TEST(MultiVMIsolation, Modvar_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);

    // Increment X twice in A
    rt_set_current_context(&a);
    rt_string name = rt_const_cstr("X");
    auto *xa = (int64_t *)rt_modvar_addr_i64(name);
    *xa += 1;
    ASSERT_EQ(*xa, 1);
    *xa += 1;
    ASSERT_EQ(*xa, 2);

    // Increment X once in B
    rt_set_current_context(&b);
    rt_string name2 = rt_const_cstr("X");
    auto *xb = (int64_t *)rt_modvar_addr_i64(name2);
    *xb += 1;
    ASSERT_EQ(*xb, 1);

    // Switch back to A and ensure it remained at 2
    rt_set_current_context(&a);
    ASSERT_EQ(*xa, 2);
}

TEST(MultiVMIsolation, FileChannels_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);
    const int ch = 5; // same channel ID used in both contexts deliberately

    // Context A writes to fileA on channel 5
    std::string fileA = makeTempPath("A");
    {
        rt_set_current_context(&a);
        rt_string p = rt_const_cstr(fileA.c_str());
        ASSERT_EQ(0, rt_open_err_vstr(p, RT_F_OUTPUT, ch));
        ASSERT_EQ(0, rt_write_ch_err(ch, rt_const_cstr("HelloA")));
        ASSERT_EQ(0, rt_close_err(ch));
    }

    // Context B writes to fileB on same channel 5
    std::string fileB = makeTempPath("B");
    {
        rt_set_current_context(&b);
        rt_string p = rt_const_cstr(fileB.c_str());
        ASSERT_EQ(0, rt_open_err_vstr(p, RT_F_OUTPUT, ch));
        ASSERT_EQ(0, rt_write_ch_err(ch, rt_const_cstr("HelloB")));
        ASSERT_EQ(0, rt_close_err(ch));
    }

    // Verify contents are as expected and not swapped/interfered
    {
        std::ifstream inA(fileA, std::ios::binary);
        std::string sA;
        std::getline(inA, sA, '\0');
        std::ifstream inB(fileB, std::ios::binary);
        std::string sB;
        std::getline(inB, sB, '\0');
        ASSERT_EQ("HelloA", sA);
        ASSERT_EQ("HelloB", sB);
    }

    std::filesystem::remove(fileA);
    std::filesystem::remove(fileB);
}

TEST(MultiVMIsolation, Args_IsolatedPerContext)
{
    RtContext a{}, b{};
    rt_context_init(&a);
    rt_context_init(&b);

    // Ensure the legacy store is empty so adoption doesn't interfere.
    rt_set_current_context(nullptr);
    rt_args_clear();
    ASSERT_EQ(rt_args_count(), 0);

    // Context A stores ["a0"].
    rt_set_current_context(&a);
    rt_args_clear();
    rt_string a0 = rt_const_cstr("a0");
    rt_args_push(a0);
    rt_string_unref(a0);
    ASSERT_EQ(rt_args_count(), 1);
    rt_string gotA0 = rt_args_get(0);
    ASSERT_TRUE(std::strcmp(rt_string_cstr(gotA0), "a0") == 0);
    rt_string_unref(gotA0);

    // Context B stores ["b0", "b1"].
    rt_set_current_context(&b);
    rt_args_clear();
    rt_string b0 = rt_const_cstr("b0");
    rt_string b1 = rt_const_cstr("b1");
    rt_args_push(b0);
    rt_args_push(b1);
    rt_string_unref(b0);
    rt_string_unref(b1);
    ASSERT_EQ(rt_args_count(), 2);
    rt_string gotB0 = rt_args_get(0);
    rt_string gotB1 = rt_args_get(1);
    ASSERT_TRUE(std::strcmp(rt_string_cstr(gotB0), "b0") == 0);
    ASSERT_TRUE(std::strcmp(rt_string_cstr(gotB1), "b1") == 0);
    rt_string_unref(gotB0);
    rt_string_unref(gotB1);

    // Switching back to A should restore A's view.
    rt_set_current_context(&a);
    ASSERT_EQ(rt_args_count(), 1);
    rt_string gotA0Again = rt_args_get(0);
    ASSERT_TRUE(std::strcmp(rt_string_cstr(gotA0Again), "a0") == 0);
    rt_string_unref(gotA0Again);

    rt_context_cleanup(&a);
    rt_context_cleanup(&b);
    rt_set_current_context(nullptr);
}

// =============================================================================
// Type Registry Isolation Tests
// =============================================================================

// Mock vtables for test classes - each context gets different vtables
static void *vtable_class_a_ctx1[1] = {nullptr};
static void *vtable_class_b_ctx1[1] = {nullptr};
static void *vtable_class_a_ctx2[1] = {nullptr};
static void *vtable_class_c_ctx2[1] = {nullptr};

// Mock interface tables
static void mock_iface_method_ctx1(void) {}

static void mock_iface_method_ctx2(void) {}

static void *itable_ctx1[1] = {(void *)mock_iface_method_ctx1};
static void *itable_ctx2[1] = {(void *)mock_iface_method_ctx2};

TEST(MultiVMIsolation, TypeRegistry_ClassRegistrationIsolated)
{
    RtContext ctx1{}, ctx2{};
    rt_context_init(&ctx1);
    rt_context_init(&ctx2);

    // Type IDs: ctx1 registers type 100 and 101, ctx2 registers type 100 and 102
    // Same type ID (100) with different vtables to test isolation
    const int TYPE_A = 100;
    const int TYPE_B = 101;
    const int TYPE_C = 102;

    // Register classes in context 1
    rt_set_current_context(&ctx1);
    rt_register_class_direct(TYPE_A, vtable_class_a_ctx1, "Ctx1.ClassA", 0);
    rt_register_class_direct(TYPE_B, vtable_class_b_ctx1, "Ctx1.ClassB", 0);

    // Verify ctx1 can look up its own classes
    void **vtbl_a_in_ctx1 = rt_get_class_vtable(TYPE_A);
    void **vtbl_b_in_ctx1 = rt_get_class_vtable(TYPE_B);
    ASSERT_EQ(vtbl_a_in_ctx1, vtable_class_a_ctx1);
    ASSERT_EQ(vtbl_b_in_ctx1, vtable_class_b_ctx1);

    // Register different classes in context 2 (reusing TYPE_A id deliberately)
    rt_set_current_context(&ctx2);
    rt_register_class_direct(TYPE_A, vtable_class_a_ctx2, "Ctx2.ClassA", 0);
    rt_register_class_direct(TYPE_C, vtable_class_c_ctx2, "Ctx2.ClassC", 0);

    // Verify ctx2 sees its own classes, not ctx1's
    void **vtbl_a_in_ctx2 = rt_get_class_vtable(TYPE_A);
    void **vtbl_c_in_ctx2 = rt_get_class_vtable(TYPE_C);
    ASSERT_EQ(vtbl_a_in_ctx2, vtable_class_a_ctx2);
    ASSERT_EQ(vtbl_c_in_ctx2, vtable_class_c_ctx2);

    // ctx2 should NOT see TYPE_B (only registered in ctx1)
    void **vtbl_b_in_ctx2 = rt_get_class_vtable(TYPE_B);
    ASSERT_EQ(vtbl_b_in_ctx2, nullptr);

    // Switch back to ctx1 and verify its registrations are unchanged
    rt_set_current_context(&ctx1);
    ASSERT_EQ(rt_get_class_vtable(TYPE_A), vtable_class_a_ctx1);
    ASSERT_EQ(rt_get_class_vtable(TYPE_B), vtable_class_b_ctx1);
    // ctx1 should NOT see TYPE_C (only registered in ctx2)
    ASSERT_EQ(rt_get_class_vtable(TYPE_C), nullptr);

    rt_context_cleanup(&ctx1);
    rt_context_cleanup(&ctx2);
}

TEST(MultiVMIsolation, TypeRegistry_TypeIsAIsolated)
{
    RtContext ctx1{}, ctx2{};
    rt_context_init(&ctx1);
    rt_context_init(&ctx2);

    const int TYPE_BASE = 200;
    const int TYPE_DERIVED = 201;

    // In ctx1: register Base and Derived with inheritance
    rt_set_current_context(&ctx1);
    rt_register_class_with_base(TYPE_BASE, vtable_class_a_ctx1, "Ctx1.Base", 0, -1);
    rt_register_class_with_base(TYPE_DERIVED, vtable_class_b_ctx1, "Ctx1.Derived", 0, TYPE_BASE);

    // Verify inheritance works in ctx1
    ASSERT_TRUE(rt_type_is_a(TYPE_DERIVED, TYPE_BASE));
    ASSERT_TRUE(rt_type_is_a(TYPE_BASE, TYPE_BASE));
    ASSERT_FALSE(rt_type_is_a(TYPE_BASE, TYPE_DERIVED));

    // In ctx2: only register Base, no Derived
    rt_set_current_context(&ctx2);
    rt_register_class_with_base(TYPE_BASE, vtable_class_a_ctx2, "Ctx2.Base", 0, -1);

    // In ctx2, TYPE_DERIVED was never registered, so is_a checks fail
    ASSERT_TRUE(rt_type_is_a(TYPE_BASE, TYPE_BASE));
    ASSERT_FALSE(rt_type_is_a(TYPE_DERIVED, TYPE_BASE)); // TYPE_DERIVED not in ctx2

    // Switch back to ctx1 - inheritance should still work
    rt_set_current_context(&ctx1);
    ASSERT_TRUE(rt_type_is_a(TYPE_DERIVED, TYPE_BASE));

    rt_context_cleanup(&ctx1);
    rt_context_cleanup(&ctx2);
}

TEST(MultiVMIsolation, TypeRegistry_InterfaceBindingIsolated)
{
    RtContext ctx1{}, ctx2{};
    rt_context_init(&ctx1);
    rt_context_init(&ctx2);

    const int TYPE_CLASS = 300;
    const int IFACE_ID = 1000;

    // In ctx1: register class and bind interface
    rt_set_current_context(&ctx1);
    rt_register_class_direct(TYPE_CLASS, vtable_class_a_ctx1, "Ctx1.MyClass", 0);
    rt_register_interface_direct(IFACE_ID, "Ctx1.IMyInterface", 1);
    rt_bind_interface(TYPE_CLASS, IFACE_ID, itable_ctx1);

    // Verify interface binding works in ctx1
    ASSERT_TRUE(rt_type_implements(TYPE_CLASS, IFACE_ID));

    // In ctx2: register the same class but DON'T bind the interface
    rt_set_current_context(&ctx2);
    rt_register_class_direct(TYPE_CLASS, vtable_class_a_ctx2, "Ctx2.MyClass", 0);
    // Note: deliberately not binding interface in ctx2

    // ctx2 should NOT see the interface binding from ctx1
    ASSERT_FALSE(rt_type_implements(TYPE_CLASS, IFACE_ID));

    // Switch back to ctx1 - binding should still exist
    rt_set_current_context(&ctx1);
    ASSERT_TRUE(rt_type_implements(TYPE_CLASS, IFACE_ID));

    rt_context_cleanup(&ctx1);
    rt_context_cleanup(&ctx2);
}

TEST(MultiVMIsolation, TypeRegistry_ItableLookupIsolated)
{
    RtContext ctx1{}, ctx2{};
    rt_context_init(&ctx1);
    rt_context_init(&ctx2);

    const int TYPE_CLASS = 400;
    const int IFACE_ID = 2000;

    // In ctx1: register class, interface, and bind with itable_ctx1
    rt_set_current_context(&ctx1);
    rt_register_class_direct(TYPE_CLASS, vtable_class_a_ctx1, "Ctx1.Widget", 0);
    rt_register_interface_direct(IFACE_ID, "Ctx1.IWidget", 1);
    rt_bind_interface(TYPE_CLASS, IFACE_ID, itable_ctx1);

    // Create mock object with ctx1's vtable
    rt_object obj_ctx1 = {.vptr = vtable_class_a_ctx1};
    void **itable_from_ctx1 = rt_itable_lookup(&obj_ctx1, IFACE_ID);
    ASSERT_EQ(itable_from_ctx1, itable_ctx1);

    // In ctx2: register class, interface, and bind with DIFFERENT itable_ctx2
    rt_set_current_context(&ctx2);
    rt_register_class_direct(TYPE_CLASS, vtable_class_a_ctx2, "Ctx2.Widget", 0);
    rt_register_interface_direct(IFACE_ID, "Ctx2.IWidget", 1);
    rt_bind_interface(TYPE_CLASS, IFACE_ID, itable_ctx2);

    // Create mock object with ctx2's vtable
    rt_object obj_ctx2 = {.vptr = vtable_class_a_ctx2};
    void **itable_from_ctx2 = rt_itable_lookup(&obj_ctx2, IFACE_ID);
    ASSERT_EQ(itable_from_ctx2, itable_ctx2);

    // Verify the itables are different (proving isolation)
    ASSERT_NE(itable_from_ctx1, itable_from_ctx2);

    // Switch back to ctx1 and verify its itable is still correct
    rt_set_current_context(&ctx1);
    ASSERT_EQ(rt_itable_lookup(&obj_ctx1, IFACE_ID), itable_ctx1);

    rt_context_cleanup(&ctx1);
    rt_context_cleanup(&ctx2);
}

TEST(MultiVMIsolation, TypeRegistry_ClassInfoFromVptrIsolated)
{
    RtContext ctx1{}, ctx2{};
    rt_context_init(&ctx1);
    rt_context_init(&ctx2);

    const int TYPE_A = 500;

    // In ctx1: register class with vtable_class_a_ctx1
    rt_set_current_context(&ctx1);
    rt_register_class_direct(TYPE_A, vtable_class_a_ctx1, "Ctx1.TypeA", 0);

    const rt_class_info *info_ctx1 = rt_get_class_info_from_vptr(vtable_class_a_ctx1);
    ASSERT_NE(info_ctx1, nullptr);
    ASSERT_EQ(info_ctx1->type_id, TYPE_A);
    ASSERT_EQ(std::strcmp(info_ctx1->qname, "Ctx1.TypeA"), 0);

    // In ctx2: register same type ID but with different vtable and name
    rt_set_current_context(&ctx2);
    rt_register_class_direct(TYPE_A, vtable_class_a_ctx2, "Ctx2.TypeA", 0);

    const rt_class_info *info_ctx2 = rt_get_class_info_from_vptr(vtable_class_a_ctx2);
    ASSERT_NE(info_ctx2, nullptr);
    ASSERT_EQ(info_ctx2->type_id, TYPE_A);
    ASSERT_EQ(std::strcmp(info_ctx2->qname, "Ctx2.TypeA"), 0);

    // ctx2 should NOT find ctx1's vtable
    const rt_class_info *cross_lookup = rt_get_class_info_from_vptr(vtable_class_a_ctx1);
    ASSERT_EQ(cross_lookup, nullptr);

    // Switch back to ctx1 - should NOT find ctx2's vtable
    rt_set_current_context(&ctx1);
    ASSERT_EQ(rt_get_class_info_from_vptr(vtable_class_a_ctx2), nullptr);
    // But should still find its own
    ASSERT_EQ(rt_get_class_info_from_vptr(vtable_class_a_ctx1), info_ctx1);

    rt_context_cleanup(&ctx1);
    rt_context_cleanup(&ctx2);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
