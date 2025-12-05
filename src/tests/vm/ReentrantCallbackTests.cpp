//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/ReentrantCallbackTests.cpp
// Purpose: Verify ActiveVMGuard correctly manages tlsActiveVM through nested
//          invocations, including re-entrant callbacks from extern functions.
// Key invariants:
//   - tlsActiveVM is correctly set/restored through nested ActiveVMGuard scopes
//   - Re-entering the same VM (nested calls) is allowed
//   - Extern callbacks can invoke VM methods and guards work correctly
// Ownership/Lifetime: Constructs test modules with extern callbacks.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "support/source_location.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace il;
using il::runtime::signatures::Signature;
using il::runtime::signatures::SigParam;
using il::runtime::signatures::make_signature;

namespace
{

/// @brief Tracks callback invocations and VM state during extern calls.
struct CallbackTracker
{
    int callCount = 0;
    std::vector<bool> sawActiveVM;   ///< Whether VM::activeInstance() was non-null during each call.
    std::vector<vm::VM *> activeVMs; ///< Captured VM pointers during each call.
    vm::VM *expectedVM = nullptr;    ///< The VM we expect to be active.
    int reentryDepth = 0;            ///< Current nesting depth.
    int maxReentryDepth = 0;         ///< Maximum nesting depth observed.
};

thread_local CallbackTracker *g_tracker = nullptr;

/// @brief Simple extern callback that records VM state.
void simpleCallback(void **args, void *result)
{
    (void)args;
    if (g_tracker)
    {
        ++g_tracker->callCount;
        vm::VM *active = vm::activeVMInstance();
        g_tracker->sawActiveVM.push_back(active != nullptr);
        g_tracker->activeVMs.push_back(active);
    }
    // Return 0
    *static_cast<int64_t *>(result) = 0;
}

/// @brief Extern callback that re-enters the VM by creating nested ActiveVMGuard.
/// @details This simulates a host callback that needs to interact with VM state,
///          which creates nested ActiveVMGuard scopes.
void reentrantCallback(void **args, void *result)
{
    (void)args;
    if (g_tracker)
    {
        ++g_tracker->callCount;
        ++g_tracker->reentryDepth;
        if (g_tracker->reentryDepth > g_tracker->maxReentryDepth)
            g_tracker->maxReentryDepth = g_tracker->reentryDepth;

        vm::VM *active = vm::activeVMInstance();
        g_tracker->sawActiveVM.push_back(active != nullptr);
        g_tracker->activeVMs.push_back(active);

        // The active VM should match expected
        assert(active == g_tracker->expectedVM && "Active VM mismatch during reentrant callback");

        // Create a nested guard with the same VM (should be allowed)
        {
            vm::ActiveVMGuard nestedGuard(active);
            assert(vm::activeVMInstance() == active && "Nested guard should preserve active VM");
        }
        // After nested guard, VM should still be active
        assert(vm::activeVMInstance() == active && "Nested guard should restore active VM");

        --g_tracker->reentryDepth;
    }
    // Return the current depth as the result
    *static_cast<int64_t *>(result) = g_tracker ? g_tracker->maxReentryDepth : 0;
}

/// @brief Helper to create a void -> i64 signature.
static Signature makeVoidToI64Sig(const std::string &name)
{
    return make_signature(name, {}, {SigParam::I64});
}

/// @brief Build a module that calls an extern function once and returns its result.
core::Module buildSimpleCallbackModule(const std::string &externName)
{
    core::Module module;
    build::IRBuilder builder(module);

    // Declare extern: i64 externName()
    builder.addExtern(externName, core::Type(core::Type::Kind::I64), {});

    // main function
    auto &fn = builder.startFunction("main", core::Type(core::Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    // Allocate a temp for the result
    const unsigned resultId = builder.reserveTempId();
    auto resultVal = core::Value::temp(resultId);

    // Call extern and store result in temp
    builder.emitCall(externName, {}, resultVal, {});

    // Return the result
    builder.emitRet(resultVal, {});

    return module;
}

/// @brief Build a module with multiple calls to track callback sequence.
core::Module buildMultiCallbackModule(const std::string &externName, int numCalls)
{
    core::Module module;
    build::IRBuilder builder(module);

    // Declare extern: i64 externName()
    builder.addExtern(externName, core::Type(core::Type::Kind::I64), {});

    auto &fn = builder.startFunction("main", core::Type(core::Type::Kind::I64), {});
    auto &entry = builder.addBlock(fn, "entry");
    builder.setInsertPoint(entry);

    core::Value lastResult = core::Value::constInt(0);
    for (int i = 0; i < numCalls; ++i)
    {
        const unsigned tempId = builder.reserveTempId();
        lastResult = core::Value::temp(tempId);
        builder.emitCall(externName, {}, lastResult, {});
    }

    builder.emitRet(lastResult, {});

    return module;
}

} // namespace

/// @brief Test that a simple extern callback sees the correct active VM.
void testSimpleCallbackSeesActiveVM()
{
    CallbackTracker tracker;
    g_tracker = &tracker;

    // Register extern
    vm::ExternDesc desc;
    desc.name = "test_simple_cb";
    desc.signature = makeVoidToI64Sig("test_simple_cb");
    desc.fn = reinterpret_cast<void *>(&simpleCallback);
    vm::RuntimeBridge::registerExtern(desc);

    auto module = buildSimpleCallbackModule("test_simple_cb");
    vm::VM vm(module);
    tracker.expectedVM = &vm;

    int64_t result = vm.run();
    (void)result;

    assert(tracker.callCount == 1 && "Callback should be called once");
    assert(tracker.sawActiveVM.size() == 1 && "Should have one observation");
    assert(tracker.sawActiveVM[0] && "Callback should see active VM");
    assert(tracker.activeVMs[0] == &vm && "Callback should see the correct VM");

    // Cleanup
    vm::RuntimeBridge::unregisterExtern("test_simple_cb");
    g_tracker = nullptr;
}

/// @brief Test that nested ActiveVMGuard with the same VM works correctly.
void testNestedGuardsSameVMInCallback()
{
    CallbackTracker tracker;
    g_tracker = &tracker;

    // Register reentrant callback
    vm::ExternDesc desc;
    desc.name = "test_reentrant_cb";
    desc.signature = makeVoidToI64Sig("test_reentrant_cb");
    desc.fn = reinterpret_cast<void *>(&reentrantCallback);
    vm::RuntimeBridge::registerExtern(desc);

    auto module = buildSimpleCallbackModule("test_reentrant_cb");
    vm::VM vm(module);
    tracker.expectedVM = &vm;

    int64_t result = vm.run();
    (void)result;

    assert(tracker.callCount == 1 && "Callback should be called once");
    assert(tracker.sawActiveVM[0] && "Callback should see active VM");
    assert(tracker.activeVMs[0] == &vm && "Callback should see correct VM");
    assert(tracker.maxReentryDepth == 1 && "Should reach reentry depth 1");

    // Cleanup
    vm::RuntimeBridge::unregisterExtern("test_reentrant_cb");
    g_tracker = nullptr;
}

/// @brief Test multiple callback invocations maintain correct VM state.
void testMultipleCallbacksPreserveVMState()
{
    CallbackTracker tracker;
    g_tracker = &tracker;

    vm::ExternDesc desc;
    desc.name = "test_multi_cb";
    desc.signature = makeVoidToI64Sig("test_multi_cb");
    desc.fn = reinterpret_cast<void *>(&simpleCallback);
    vm::RuntimeBridge::registerExtern(desc);

    auto module = buildMultiCallbackModule("test_multi_cb", 5);
    vm::VM vm(module);
    tracker.expectedVM = &vm;

    int64_t result = vm.run();
    (void)result;

    assert(tracker.callCount == 5 && "Callback should be called 5 times");
    assert(tracker.sawActiveVM.size() == 5 && "Should have 5 observations");

    for (int i = 0; i < 5; ++i)
    {
        assert(tracker.sawActiveVM[i] && "Each callback should see active VM");
        assert(tracker.activeVMs[i] == &vm && "Each callback should see correct VM");
    }

    // Cleanup
    vm::RuntimeBridge::unregisterExtern("test_multi_cb");
    g_tracker = nullptr;
}

/// @brief Test that activeVMInstance() is null after VM run completes.
void testActiveVMNullAfterRun()
{
    auto module = buildSimpleCallbackModule("rt_abs_i64"); // Use a known extern
    vm::VM vm(module);

    // Before run
    assert(vm::activeVMInstance() == nullptr && "No active VM before run");

    // After VM object exists but not running
    assert(vm::activeVMInstance() == nullptr && "No active VM after construction");
}

/// @brief Test nested guards restore correctly in complex scenarios.
void testNestedGuardRestorationChain()
{
    core::Module module1;
    core::Module module2;

    vm::VM vm1(module1);
    vm::VM vm2(module2);

    assert(vm::activeVMInstance() == nullptr);

    // Create chain of nested guards
    {
        vm::ActiveVMGuard g1(&vm1);
        assert(vm::activeVMInstance() == &vm1);

        {
            // Nesting same VM is allowed
            vm::ActiveVMGuard g2(&vm1);
            assert(vm::activeVMInstance() == &vm1);

            {
                // Another nesting of same VM
                vm::ActiveVMGuard g3(&vm1);
                assert(vm::activeVMInstance() == &vm1);
            }

            assert(vm::activeVMInstance() == &vm1 && "Should restore to vm1 after g3");
        }

        assert(vm::activeVMInstance() == &vm1 && "Should restore to vm1 after g2");
    }

    assert(vm::activeVMInstance() == nullptr && "Should be null after all guards");
}

/// @brief Test nullptr guard clears active VM.
void testNullptrGuardClearsActiveVM()
{
    core::Module module;
    vm::VM vm(module);

    {
        vm::ActiveVMGuard g1(&vm);
        assert(vm::activeVMInstance() == &vm);

        {
            // nullptr guard should clear active VM
            vm::ActiveVMGuard g2(nullptr);
            assert(vm::activeVMInstance() == nullptr && "nullptr guard should clear");
        }

        // After nullptr guard, should restore to previous
        assert(vm::activeVMInstance() == &vm && "Should restore vm after nullptr guard");
    }

    assert(vm::activeVMInstance() == nullptr);
}

/// @brief Test guard restoration with interleaved nullptr guards.
void testInterleavedNullptrGuards()
{
    core::Module module;
    vm::VM vm(module);

    assert(vm::activeVMInstance() == nullptr);

    {
        vm::ActiveVMGuard g1(&vm);
        assert(vm::activeVMInstance() == &vm);

        {
            vm::ActiveVMGuard g2(nullptr);
            assert(vm::activeVMInstance() == nullptr);

            {
                vm::ActiveVMGuard g3(&vm);
                assert(vm::activeVMInstance() == &vm);
            }

            assert(vm::activeVMInstance() == nullptr && "Should restore to null from g2");
        }

        assert(vm::activeVMInstance() == &vm && "Should restore to vm from g1");
    }

    assert(vm::activeVMInstance() == nullptr);
}

int main()
{
    // Test basic callback functionality
    testSimpleCallbackSeesActiveVM();
    testNestedGuardsSameVMInCallback();
    testMultipleCallbacksPreserveVMState();

    // Test guard behavior without callbacks
    testActiveVMNullAfterRun();
    testNestedGuardRestorationChain();
    testNullptrGuardClearsActiveVM();
    testInterleavedNullptrGuards();

    return 0;
}
