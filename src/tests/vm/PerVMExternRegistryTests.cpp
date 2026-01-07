//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
//===----------------------------------------------------------------------===//
// File: src/tests/vm/PerVMExternRegistryTests.cpp
//
// Purpose:
//   Verify per-VM extern registry isolation. Each VM can have its own
//   ExternRegistry holding a distinct set of external functions, independent
//   of the process-global registry shared by VMs without a per-VM registry.
//
// Key invariants:
//   - VMs with per-VM registries resolve externs from their own registry first
//   - VMs without per-VM registries fall back to the process-global registry
//   - Per-VM registries are independent: changes in one don't affect others
//   - The process-global registry remains unchanged by per-VM operations
//
// Links: include/viper/vm/RuntimeBridge.hpp
//===----------------------------------------------------------------------===//
#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt_context.h"
#include "tests/TestHarness.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <string>
#include <thread>

using namespace il::vm;
using il::runtime::signatures::make_signature;
using il::runtime::signatures::Signature;
using il::runtime::signatures::SigParam;

// Helper to create a simple void -> i64 signature for test externs
static Signature makeVoidToI64Sig(const std::string &name)
{
    return make_signature(name, {}, {SigParam::I64});
}

//===----------------------------------------------------------------------===//
// Test extern implementations
//===----------------------------------------------------------------------===//

// Returns 100 - identifies as "global" extern
static void extern_global_fn(void **args, void *result)
{
    *static_cast<int64_t *>(result) = 100;
}

// Returns 200 - identifies as "VM A" extern
static void extern_vm_a_fn(void **args, void *result)
{
    *static_cast<int64_t *>(result) = 200;
}

// Returns 300 - identifies as "VM B" extern
static void extern_vm_b_fn(void **args, void *result)
{
    *static_cast<int64_t *>(result) = 300;
}

// Returns 400 - identifies as "per-VM only" extern (not in global)
static void extern_per_vm_only_fn(void **args, void *result)
{
    *static_cast<int64_t *>(result) = 400;
}

//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//

TEST(PerVMExternRegistry, CreateAndDestroy)
{
    // Verify that we can create and destroy per-VM registries
    ExternRegistryPtr reg = createExternRegistry();
    ASSERT_NE(reg.get(), nullptr);

    // Register a function
    ExternDesc desc;
    desc.name = "test_extern";
    desc.signature = makeVoidToI64Sig("test_extern");
    desc.fn = reinterpret_cast<void *>(extern_global_fn);
    registerExternIn(*reg, desc);

    // Look it up
    const ExternDesc *found = findExternIn(*reg, "test_extern");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "test_extern");
    EXPECT_EQ(found->fn, desc.fn);

    // Destroy happens automatically when unique_ptr goes out of scope
}

TEST(PerVMExternRegistry, TwoVMsWithDifferentExterns)
{
    // Create two per-VM registries with different extern implementations
    ExternRegistryPtr regA = createExternRegistry();
    ExternRegistryPtr regB = createExternRegistry();

    // Register "get_value" in both with different implementations
    ExternDesc descA;
    descA.name = "get_value";
    descA.signature = makeVoidToI64Sig("get_value");
    descA.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    registerExternIn(*regA, descA);

    ExternDesc descB;
    descB.name = "get_value";
    descB.signature = makeVoidToI64Sig("get_value");
    descB.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    registerExternIn(*regB, descB);

    // Verify they have different function pointers
    const ExternDesc *foundA = findExternIn(*regA, "get_value");
    const ExternDesc *foundB = findExternIn(*regB, "get_value");

    ASSERT_NE(foundA, nullptr);
    ASSERT_NE(foundB, nullptr);
    EXPECT_NE(foundA->fn, foundB->fn);
    EXPECT_EQ(foundA->fn, reinterpret_cast<void *>(extern_vm_a_fn));
    EXPECT_EQ(foundB->fn, reinterpret_cast<void *>(extern_vm_b_fn));
}

TEST(PerVMExternRegistry, VMAssignmentAndRetrieval)
{
    // Create a VM and assign a per-VM registry
    il::core::Module module;
    VM vm(module);

    ExternRegistryPtr reg = createExternRegistry();
    ExternRegistry *rawReg = reg.get();

    // Initially VM has no per-VM registry
    EXPECT_EQ(vm.externRegistry(), nullptr);

    // Assign it
    vm.setExternRegistry(rawReg);
    EXPECT_EQ(vm.externRegistry(), rawReg);

    // Clear it
    vm.setExternRegistry(nullptr);
    EXPECT_EQ(vm.externRegistry(), nullptr);
}

TEST(PerVMExternRegistry, GlobalFallback)
{
    // Register in the global registry
    ExternDesc globalDesc;
    globalDesc.name = "global_only_extern";
    globalDesc.signature = makeVoidToI64Sig("global_only_extern");
    globalDesc.fn = reinterpret_cast<void *>(extern_global_fn);
    registerExternIn(processGlobalExternRegistry(), globalDesc);

    // Create a per-VM registry WITHOUT that extern
    ExternRegistryPtr perVmReg = createExternRegistry();

    // per-VM registry should NOT find it
    const ExternDesc *inPerVm = findExternIn(*perVmReg, "global_only_extern");
    EXPECT_EQ(inPerVm, nullptr);

    // global registry should find it
    const ExternDesc *inGlobal = findExternIn(processGlobalExternRegistry(), "global_only_extern");
    ASSERT_NE(inGlobal, nullptr);
    EXPECT_EQ(inGlobal->fn, reinterpret_cast<void *>(extern_global_fn));

    // Cleanup
    unregisterExternIn(processGlobalExternRegistry(), "global_only_extern");
}

TEST(PerVMExternRegistry, PerVMOverridesGlobal)
{
    // Register "shared_name" in global registry
    ExternDesc globalDesc;
    globalDesc.name = "shared_name";
    globalDesc.signature = makeVoidToI64Sig("shared_name");
    globalDesc.fn = reinterpret_cast<void *>(extern_global_fn);
    registerExternIn(processGlobalExternRegistry(), globalDesc);

    // Register same name in per-VM registry with different implementation
    ExternRegistryPtr perVmReg = createExternRegistry();
    ExternDesc perVmDesc;
    perVmDesc.name = "shared_name";
    perVmDesc.signature = makeVoidToI64Sig("shared_name");
    perVmDesc.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    registerExternIn(*perVmReg, perVmDesc);

    // per-VM registry sees its own version
    const ExternDesc *inPerVm = findExternIn(*perVmReg, "shared_name");
    ASSERT_NE(inPerVm, nullptr);
    EXPECT_EQ(inPerVm->fn, reinterpret_cast<void *>(extern_vm_a_fn));

    // global registry sees global version
    const ExternDesc *inGlobal = findExternIn(processGlobalExternRegistry(), "shared_name");
    ASSERT_NE(inGlobal, nullptr);
    EXPECT_EQ(inGlobal->fn, reinterpret_cast<void *>(extern_global_fn));

    // Cleanup
    unregisterExternIn(processGlobalExternRegistry(), "shared_name");
}

TEST(PerVMExternRegistry, UnregisterFromPerVM)
{
    ExternRegistryPtr reg = createExternRegistry();

    ExternDesc desc;
    desc.name = "removable";
    desc.signature = makeVoidToI64Sig("removable");
    desc.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    registerExternIn(*reg, desc);

    // Verify it exists
    ASSERT_NE(findExternIn(*reg, "removable"), nullptr);

    // Unregister
    bool removed = unregisterExternIn(*reg, "removable");
    EXPECT_TRUE(removed);

    // Verify it's gone
    EXPECT_EQ(findExternIn(*reg, "removable"), nullptr);

    // Unregistering again returns false
    EXPECT_FALSE(unregisterExternIn(*reg, "removable"));
}

TEST(PerVMExternRegistry, CaseInsensitiveLookup)
{
    ExternRegistryPtr reg = createExternRegistry();

    ExternDesc desc;
    desc.name = "MixedCase";
    desc.signature = makeVoidToI64Sig("MixedCase");
    desc.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    registerExternIn(*reg, desc);

    // All case variations should find it
    EXPECT_NE(findExternIn(*reg, "MixedCase"), nullptr);
    EXPECT_NE(findExternIn(*reg, "mixedcase"), nullptr);
    EXPECT_NE(findExternIn(*reg, "MIXEDCASE"), nullptr);
    EXPECT_NE(findExternIn(*reg, "mIxEdCaSe"), nullptr);
}

TEST(PerVMExternRegistry, MultipleRegistriesIndependent)
{
    // Create three registries: two per-VM and the global
    ExternRegistryPtr regA = createExternRegistry();
    ExternRegistryPtr regB = createExternRegistry();
    ExternRegistry &global = processGlobalExternRegistry();

    // Register different externs in each
    ExternDesc descA;
    descA.name = "only_in_a";
    descA.signature = makeVoidToI64Sig("only_in_a");
    descA.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    registerExternIn(*regA, descA);

    ExternDesc descB;
    descB.name = "only_in_b";
    descB.signature = makeVoidToI64Sig("only_in_b");
    descB.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    registerExternIn(*regB, descB);

    ExternDesc descGlobal;
    descGlobal.name = "only_in_global";
    descGlobal.signature = makeVoidToI64Sig("only_in_global");
    descGlobal.fn = reinterpret_cast<void *>(extern_global_fn);
    registerExternIn(global, descGlobal);

    // Each only sees its own
    EXPECT_NE(findExternIn(*regA, "only_in_a"), nullptr);
    EXPECT_EQ(findExternIn(*regA, "only_in_b"), nullptr);
    EXPECT_EQ(findExternIn(*regA, "only_in_global"), nullptr);

    EXPECT_EQ(findExternIn(*regB, "only_in_a"), nullptr);
    EXPECT_NE(findExternIn(*regB, "only_in_b"), nullptr);
    EXPECT_EQ(findExternIn(*regB, "only_in_global"), nullptr);

    EXPECT_EQ(findExternIn(global, "only_in_a"), nullptr);
    EXPECT_EQ(findExternIn(global, "only_in_b"), nullptr);
    EXPECT_NE(findExternIn(global, "only_in_global"), nullptr);

    // Cleanup global
    unregisterExternIn(global, "only_in_global");
}

TEST(PerVMExternRegistry, ActiveVMRegistryResolution)
{
    // This test verifies that currentExternRegistry() routes to the active VM's
    // registry when one is set, and falls back to global otherwise.

    il::core::Module module;
    VM vmWithReg(module);
    VM vmWithoutReg(module);

    ExternRegistryPtr perVmReg = createExternRegistry();
    vmWithReg.setExternRegistry(perVmReg.get());

    // Register different things in per-VM vs global
    ExternDesc perVmDesc;
    perVmDesc.name = "routing_test";
    perVmDesc.signature = makeVoidToI64Sig("routing_test");
    perVmDesc.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    registerExternIn(*perVmReg, perVmDesc);

    ExternDesc globalDesc;
    globalDesc.name = "routing_test";
    globalDesc.signature = makeVoidToI64Sig("routing_test");
    globalDesc.fn = reinterpret_cast<void *>(extern_global_fn);
    registerExternIn(processGlobalExternRegistry(), globalDesc);

    // When vmWithReg is active, currentExternRegistry should return perVmReg
    {
        ActiveVMGuard guard(&vmWithReg);
        ExternRegistry &current = currentExternRegistry();
        const ExternDesc *found = findExternIn(current, "routing_test");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->fn, reinterpret_cast<void *>(extern_vm_a_fn));
    }

    // When vmWithoutReg is active, currentExternRegistry should return global
    {
        ActiveVMGuard guard(&vmWithoutReg);
        ExternRegistry &current = currentExternRegistry();
        const ExternDesc *found = findExternIn(current, "routing_test");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->fn, reinterpret_cast<void *>(extern_global_fn));
    }

    // When no VM is active, currentExternRegistry should return global
    {
        ExternRegistry &current = currentExternRegistry();
        const ExternDesc *found = findExternIn(current, "routing_test");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->fn, reinterpret_cast<void *>(extern_global_fn));
    }

    // Cleanup
    unregisterExternIn(processGlobalExternRegistry(), "routing_test");
}

TEST(PerVMExternRegistry, ThreadSafeGlobalRegistry)
{
    // Verify that concurrent access to the global registry is safe
    constexpr int kNumThreads = 4;
    constexpr int kNumOpsPerThread = 100;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t)
    {
        threads.emplace_back(
            [t]()
            {
                for (int i = 0; i < kNumOpsPerThread; ++i)
                {
                    std::string name =
                        "thread_" + std::to_string(t) + "_extern_" + std::to_string(i);
                    ExternDesc desc;
                    desc.name = name;
                    desc.signature = makeVoidToI64Sig(name);
                    desc.fn = reinterpret_cast<void *>(extern_global_fn);

                    registerExternIn(processGlobalExternRegistry(), desc);
                    findExternIn(processGlobalExternRegistry(), name);
                    unregisterExternIn(processGlobalExternRegistry(), name);
                }
            });
    }

    for (auto &th : threads)
        th.join();

    // If we get here without deadlock or crash, the test passes
}

//===----------------------------------------------------------------------===//
// Strict Mode Tests
//===----------------------------------------------------------------------===//

// Helper to create a void -> f64 signature (different from void -> i64)
static Signature makeVoidToF64Sig(const std::string &name)
{
    return make_signature(name, {}, {SigParam::F64});
}

// Helper to create a i64 -> i64 signature
static Signature makeI64ToI64Sig(const std::string &name)
{
    return make_signature(name, {SigParam::I64}, {SigParam::I64});
}

TEST(ExternRegistryStrictMode, DefaultDisabled)
{
    ExternRegistryPtr reg = createExternRegistry();
    EXPECT_FALSE(isExternRegistryStrictMode(*reg));
}

TEST(ExternRegistryStrictMode, EnableDisable)
{
    ExternRegistryPtr reg = createExternRegistry();

    setExternRegistryStrictMode(*reg, true);
    EXPECT_TRUE(isExternRegistryStrictMode(*reg));

    setExternRegistryStrictMode(*reg, false);
    EXPECT_FALSE(isExternRegistryStrictMode(*reg));
}

TEST(ExternRegistryStrictMode, ReRegisterSameSignatureAllowed)
{
    ExternRegistryPtr reg = createExternRegistry();
    setExternRegistryStrictMode(*reg, true);

    // First registration
    ExternDesc desc1;
    desc1.name = "my_extern";
    desc1.signature = makeVoidToI64Sig("my_extern");
    desc1.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    auto result1 = registerExternIn(*reg, desc1);
    EXPECT_EQ(result1, ExternRegisterResult::Success);

    // Re-register with same signature but different function pointer
    ExternDesc desc2;
    desc2.name = "my_extern";
    desc2.signature = makeVoidToI64Sig("my_extern");
    desc2.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    auto result2 = registerExternIn(*reg, desc2);
    EXPECT_EQ(result2, ExternRegisterResult::Success);

    // Verify the new function pointer is used
    const ExternDesc *found = findExternIn(*reg, "my_extern");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fn, reinterpret_cast<void *>(extern_vm_b_fn));
}

TEST(ExternRegistryStrictMode, ReRegisterDifferentSignatureFails)
{
    ExternRegistryPtr reg = createExternRegistry();
    setExternRegistryStrictMode(*reg, true);

    // First registration: void -> i64
    ExternDesc desc1;
    desc1.name = "typed_extern";
    desc1.signature = makeVoidToI64Sig("typed_extern");
    desc1.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    auto result1 = registerExternIn(*reg, desc1);
    EXPECT_EQ(result1, ExternRegisterResult::Success);

    // Re-register with different signature: void -> f64
    ExternDesc desc2;
    desc2.name = "typed_extern";
    desc2.signature = makeVoidToF64Sig("typed_extern");
    desc2.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    auto result2 = registerExternIn(*reg, desc2);
    EXPECT_EQ(result2, ExternRegisterResult::SignatureMismatch);

    // Verify the original registration is unchanged
    const ExternDesc *found = findExternIn(*reg, "typed_extern");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fn, reinterpret_cast<void *>(extern_vm_a_fn));
}

TEST(ExternRegistryStrictMode, DifferentParamCountFails)
{
    ExternRegistryPtr reg = createExternRegistry();
    setExternRegistryStrictMode(*reg, true);

    // First registration: void -> i64
    ExternDesc desc1;
    desc1.name = "param_extern";
    desc1.signature = makeVoidToI64Sig("param_extern");
    desc1.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    auto result1 = registerExternIn(*reg, desc1);
    EXPECT_EQ(result1, ExternRegisterResult::Success);

    // Re-register with different params: i64 -> i64
    ExternDesc desc2;
    desc2.name = "param_extern";
    desc2.signature = makeI64ToI64Sig("param_extern");
    desc2.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    auto result2 = registerExternIn(*reg, desc2);
    EXPECT_EQ(result2, ExternRegisterResult::SignatureMismatch);
}

TEST(ExternRegistryStrictMode, NonStrictModeOverwrites)
{
    ExternRegistryPtr reg = createExternRegistry();
    // Strict mode is OFF by default

    // First registration: void -> i64
    ExternDesc desc1;
    desc1.name = "overwrite_extern";
    desc1.signature = makeVoidToI64Sig("overwrite_extern");
    desc1.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    auto result1 = registerExternIn(*reg, desc1);
    EXPECT_EQ(result1, ExternRegisterResult::Success);

    // Re-register with different signature: void -> f64
    // Should succeed in non-strict mode
    ExternDesc desc2;
    desc2.name = "overwrite_extern";
    desc2.signature = makeVoidToF64Sig("overwrite_extern");
    desc2.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    auto result2 = registerExternIn(*reg, desc2);
    EXPECT_EQ(result2, ExternRegisterResult::Success);

    // Verify the new registration is used
    const ExternDesc *found = findExternIn(*reg, "overwrite_extern");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fn, reinterpret_cast<void *>(extern_vm_b_fn));
}

TEST(ExternRegistryStrictMode, GlobalRegistryStrictMode)
{
    // Save and restore original state
    bool originalStrict = isExternRegistryStrictMode(processGlobalExternRegistry());

    // Enable strict mode on global registry
    setExternRegistryStrictMode(processGlobalExternRegistry(), true);
    EXPECT_TRUE(isExternRegistryStrictMode(processGlobalExternRegistry()));

    // First registration
    ExternDesc desc1;
    desc1.name = "global_strict_test";
    desc1.signature = makeVoidToI64Sig("global_strict_test");
    desc1.fn = reinterpret_cast<void *>(extern_global_fn);
    auto result1 = registerExternIn(processGlobalExternRegistry(), desc1);
    EXPECT_EQ(result1, ExternRegisterResult::Success);

    // Re-register with different signature should fail
    ExternDesc desc2;
    desc2.name = "global_strict_test";
    desc2.signature = makeVoidToF64Sig("global_strict_test");
    desc2.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    auto result2 = registerExternIn(processGlobalExternRegistry(), desc2);
    EXPECT_EQ(result2, ExternRegisterResult::SignatureMismatch);

    // Cleanup
    unregisterExternIn(processGlobalExternRegistry(), "global_strict_test");
    setExternRegistryStrictMode(processGlobalExternRegistry(), originalStrict);
}

TEST(ExternRegistryStrictMode, CaseInsensitiveNameMatching)
{
    ExternRegistryPtr reg = createExternRegistry();
    setExternRegistryStrictMode(*reg, true);

    // Register with lowercase
    ExternDesc desc1;
    desc1.name = "case_test";
    desc1.signature = makeVoidToI64Sig("case_test");
    desc1.fn = reinterpret_cast<void *>(extern_vm_a_fn);
    auto result1 = registerExternIn(*reg, desc1);
    EXPECT_EQ(result1, ExternRegisterResult::Success);

    // Re-register with different case and different signature should fail
    ExternDesc desc2;
    desc2.name = "CASE_TEST";
    desc2.signature = makeVoidToF64Sig("CASE_TEST");
    desc2.fn = reinterpret_cast<void *>(extern_vm_b_fn);
    auto result2 = registerExternIn(*reg, desc2);
    EXPECT_EQ(result2, ExternRegisterResult::SignatureMismatch);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
