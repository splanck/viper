//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_threading_model.cpp
// Purpose: Verify VM concurrency model: single-threaded per instance, multi-VM
//          parallelism, and ActiveVMGuard thread-local semantics.
// Key invariants: Each VM instance is single-threaded; thread-local state is
//                 correctly managed by ActiveVMGuard.
// Ownership/Lifetime: Constructs independent modules per thread.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "support/source_location.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

using namespace il;

namespace
{

/// @brief Build a simple module that returns a constant value.
/// @param retVal The constant i64 value to return from main.
/// @return A module with a main function returning @p retVal.
core::Module buildSimpleModule(int64_t retVal)
{
    core::Module module;
    build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", core::Type(core::Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);
    std::optional<core::Value> ret = core::Value::constInt(retVal);
    builder.emitRet(ret, {});
    return module;
}

/// @brief Build a module that returns a computed sum.
/// @param a First constant.
/// @param b Second constant.
/// @return A module with main returning a + b.
core::Module buildAddModule(int64_t a, int64_t b)
{
    // Simply return the precomputed sum for simplicity
    return buildSimpleModule(a + b);
}

} // namespace

/// @brief Test that two VMs on different threads execute independently.
/// @details Creates two threads, each with its own VM instance. Both execute
///          concurrently and should produce correct, isolated results.
void testMultiThreadedVMExecution()
{
    std::atomic<int64_t> result1{-1};
    std::atomic<int64_t> result2{-1};

    auto module1 = buildSimpleModule(42);
    auto module2 = buildSimpleModule(99);

    std::thread t1([&]()
                   {
        vm::VM vm(module1);
        result1 = vm.run();
    });

    std::thread t2([&]()
                   {
        vm::VM vm(module2);
        result2 = vm.run();
    });

    t1.join();
    t2.join();

    assert(result1 == 42 && "Thread 1 should return 42");
    assert(result2 == 99 && "Thread 2 should return 99");
}

/// @brief Test that activeVMInstance() returns correct thread-local values.
/// @details Verifies that each thread sees its own active VM instance, and
///          that activeVMInstance() returns nullptr when no VM is active.
void testActiveInstanceIsolation()
{
    std::atomic<bool> t1SawNull{false};
    std::atomic<bool> t2SawNull{false};
    std::atomic<bool> t1SawCorrectVM{false};
    std::atomic<bool> t2SawCorrectVM{false};

    auto module1 = buildSimpleModule(1);
    auto module2 = buildSimpleModule(2);

    std::thread t1([&]()
                   {
        // Before VM activation, activeVMInstance should be null
        t1SawNull = (vm::activeVMInstance() == nullptr);

        vm::VM vm(module1);
        // Run creates an ActiveVMGuard internally
        // We can test by creating our own guard
        {
            vm::ActiveVMGuard guard(&vm);
            t1SawCorrectVM = (vm::activeVMInstance() == &vm);
        }
        // After guard, activeVMInstance should be null again
        assert(vm::activeVMInstance() == nullptr);
    });

    std::thread t2([&]()
                   {
        // Before VM activation, activeVMInstance should be null
        t2SawNull = (vm::activeVMInstance() == nullptr);

        vm::VM vm(module2);
        {
            vm::ActiveVMGuard guard(&vm);
            t2SawCorrectVM = (vm::activeVMInstance() == &vm);
        }
        assert(vm::activeVMInstance() == nullptr);
    });

    t1.join();
    t2.join();

    assert(t1SawNull && "Thread 1 should see null before activation");
    assert(t2SawNull && "Thread 2 should see null before activation");
    assert(t1SawCorrectVM && "Thread 1 should see its own VM");
    assert(t2SawCorrectVM && "Thread 2 should see its own VM");
}

/// @brief Test nested ActiveVMGuard on the same VM (legitimate nesting).
/// @details The VM interpreter creates nested guards during recursive function
///          calls. This tests that nesting the same VM is permitted.
void testNestedGuardsSameVM()
{
    auto module = buildSimpleModule(100);
    vm::VM vm(module);

    assert(vm::activeVMInstance() == nullptr);

    {
        vm::ActiveVMGuard outer(&vm);
        assert(vm::activeVMInstance() == &vm);

        {
            // Nested guard with the same VM is allowed
            vm::ActiveVMGuard inner(&vm);
            assert(vm::activeVMInstance() == &vm);
        }

        // After inner guard, should still see vm
        assert(vm::activeVMInstance() == &vm);
    }

    assert(vm::activeVMInstance() == nullptr);
}

/// @brief Test that multiple VMs can compute correct results in parallel.
/// @details Spawns N threads, each with its own module and VM. Each computes
///          a different sum and we verify all results are correct.
void testParallelComputation()
{
    constexpr int kNumThreads = 4;
    std::vector<std::atomic<int64_t>> results(kNumThreads);
    std::vector<std::thread> threads;

    for (int i = 0; i < kNumThreads; ++i)
    {
        results[i] = -1;
        threads.emplace_back(
            [&results, i]()
            {
                auto module = buildAddModule(i * 10, i * 10 + 1);
                vm::VM vm(module);
                results[i] = vm.run();
            });
    }

    for (auto &t : threads)
        t.join();

    for (int i = 0; i < kNumThreads; ++i)
    {
        int64_t expected = i * 10 + i * 10 + 1; // i*10 + i*10 + 1 = 20i + 1
        assert(results[i] == expected && "Parallel computation should produce correct result");
    }
}

/// @brief Test that guard destructor correctly restores nullptr.
void testGuardRestoresNull()
{
    auto module = buildSimpleModule(1);
    vm::VM vm(module);

    assert(vm::activeVMInstance() == nullptr);

    {
        vm::ActiveVMGuard guard(&vm);
        assert(vm::activeVMInstance() == &vm);
    }

    assert(vm::activeVMInstance() == nullptr &&
           "ActiveVMGuard should restore null after destruction");
}

int main()
{
    testGuardRestoresNull();
    testNestedGuardsSameVM();
    testActiveInstanceIsolation();
    testMultiThreadedVMExecution();
    testParallelComputation();

    return 0;
}
