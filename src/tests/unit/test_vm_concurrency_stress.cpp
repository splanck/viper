//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_concurrency_stress.cpp
// Purpose: Stress test for VM concurrency model with many VMs across threads.
// Key invariants: Each VM instance is isolated; trap reports are thread-correct.
// Ownership/Lifetime: Each thread constructs and owns its VM instance.
// Links: docs/vm.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_location.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace il;
using namespace il::core;

namespace
{

/// @brief Configuration for the stress test.
struct StressConfig
{
    int numThreads = 8;            ///< Number of concurrent threads.
    int iterationsPerThread = 100; ///< Number of VM runs per thread.
    bool enableDebugLogging = false; ///< Enable verbose debug output.
};

/// @brief Thread-safe logging for debug output.
class DebugLog
{
  public:
    void log(int threadId, const std::string &msg)
    {
        if (!enabled)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "[T" << threadId << "] " << msg << "\n";
    }

    bool enabled = false;

  private:
    std::mutex mutex;
};

DebugLog gDebugLog;

/// @brief Build a simple module that returns a constant value encoding thread and iteration.
/// @param threadId Thread identifier embedded in the result.
/// @param iteration Iteration number embedded in the result.
/// @return Module returning threadId * 10000 + iteration.
Module buildSimpleModule(int threadId, int iteration)
{
    Module module;
    build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // Return threadId * 10000 + iteration
    int64_t result = static_cast<int64_t>(threadId) * 10000 + iteration;
    std::optional<Value> ret = Value::constInt(result);
    builder.emitRet(ret, {});
    return module;
}

/// @brief Build a module with arithmetic operations.
/// @param threadId Thread identifier.
/// @param iteration Iteration number.
/// @return Module that computes threadId * 10000 + iteration using actual IL ops.
Module buildArithmeticModule(int threadId, int iteration)
{
    Module module;
    build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    // %t0 = iadd.ovf threadId*10000, iteration
    Instr add;
    add.result = builder.reserveTempId();
    add.op = Opcode::IAddOvf;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt(static_cast<int64_t>(threadId) * 10000));
    add.operands.push_back(Value::constInt(static_cast<int64_t>(iteration)));
    add.loc = {static_cast<uint32_t>(threadId), static_cast<uint32_t>(iteration), 1};
    bb.instructions.push_back(add);

    // ret %t0
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*add.result));
    ret.loc = {static_cast<uint32_t>(threadId), static_cast<uint32_t>(iteration), 2};
    bb.instructions.push_back(ret);

    return module;
}

/// @brief Build a module that calls a runtime function.
/// @param threadId Thread identifier for unique labeling.
/// @param iteration Iteration for unique labeling.
/// @return Module that calls Viper.Math.AbsInt and returns a known value.
Module buildRuntimeCallModule(int threadId, int iteration)
{
    Module module;
    build::IRBuilder builder(module);

    // Add extern for runtime abs function
    builder.addExtern("Viper.Math.AbsInt",
                      Type(Type::Kind::I64),
                      {Type(Type::Kind::I64)});

    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    std::string blockLabel = "block_t" + std::to_string(threadId) + "_i" + std::to_string(iteration);
    auto &bb = builder.addBlock(fn, blockLabel);
    builder.setInsertPoint(bb);

    support::SourceLoc loc{static_cast<uint32_t>(threadId), static_cast<uint32_t>(iteration), 1};

    // Call abs(-42) -> should return 42
    builder.emitCall("Viper.Math.AbsInt", {Value::constInt(-42)}, Value::temp(builder.reserveTempId()), loc);
    unsigned absResult = builder.reserveTempId() - 1; // The last reserved temp

    // Add threadId * 10000 + iteration to the result
    Instr addThread;
    addThread.result = builder.reserveTempId();
    addThread.op = Opcode::IAddOvf;
    addThread.type = Type(Type::Kind::I64);
    addThread.operands.push_back(Value::temp(absResult));
    addThread.operands.push_back(Value::constInt(static_cast<int64_t>(threadId) * 10000 + iteration));
    addThread.loc = loc;
    bb.instructions.push_back(addThread);

    // ret result
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*addThread.result));
    ret.loc = loc;
    bb.instructions.push_back(ret);

    return module;
}

/// @brief Build a module with more complex arithmetic for stress testing.
/// @param threadId Thread identifier.
/// @param iteration Iteration number.
/// @return Module that does multiple operations and returns threadId * 10000 + iteration.
Module buildComplexArithmeticModule(int threadId, int iteration)
{
    Module module;
    build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    support::SourceLoc loc{static_cast<uint32_t>(threadId), static_cast<uint32_t>(iteration), 1};

    // Do some arithmetic to stress the VM
    // %t0 = imul.ovf threadId, 10000
    Instr mul;
    mul.result = builder.reserveTempId();
    mul.op = Opcode::IMulOvf;
    mul.type = Type(Type::Kind::I64);
    mul.operands.push_back(Value::constInt(static_cast<int64_t>(threadId)));
    mul.operands.push_back(Value::constInt(10000));
    mul.loc = loc;
    bb.instructions.push_back(mul);

    // %t1 = iadd.ovf %t0, iteration
    Instr add;
    add.result = builder.reserveTempId();
    add.op = Opcode::IAddOvf;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::temp(*mul.result));
    add.operands.push_back(Value::constInt(static_cast<int64_t>(iteration)));
    add.loc = loc;
    bb.instructions.push_back(add);

    // %t2 = isub.ovf %t1, 0 (identity op, just for stress)
    Instr sub;
    sub.result = builder.reserveTempId();
    sub.op = Opcode::ISubOvf;
    sub.type = Type(Type::Kind::I64);
    sub.operands.push_back(Value::temp(*add.result));
    sub.operands.push_back(Value::constInt(0));
    sub.loc = loc;
    bb.instructions.push_back(sub);

    // ret %t2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(*sub.result));
    ret.loc = loc;
    bb.instructions.push_back(ret);

    return module;
}

/// @brief Statistics collected during stress test.
struct StressStats
{
    std::atomic<int64_t> successfulRuns{0};
    std::atomic<int64_t> failedRuns{0};
    std::atomic<int64_t> contextMismatches{0};
    std::atomic<int64_t> trapHandled{0};
};

/// @brief Run a single thread's worth of stress iterations.
/// @param threadId Unique identifier for this thread.
/// @param config Test configuration.
/// @param stats Shared statistics for verification.
void runStressThread(int threadId, const StressConfig &config, StressStats &stats)
{
    std::mt19937 rng(threadId * 12345 + 1);
    std::uniform_int_distribution<int> testDist(0, 2);

    for (int iter = 0; iter < config.iterationsPerThread; ++iter)
    {
        int testType = testDist(rng);
        gDebugLog.log(threadId, "Iteration " + std::to_string(iter) + " type " + std::to_string(testType));

        try
        {
            switch (testType)
            {
                case 0:
                {
                    // Simple arithmetic test
                    auto module = buildArithmeticModule(threadId, iter);
                    vm::VM vm(module);
                    int64_t result = vm.run();
                    int64_t expected = static_cast<int64_t>(threadId) * 10000 + iter;
                    if (result != expected)
                    {
                        gDebugLog.log(threadId, "MISMATCH: got " + std::to_string(result) +
                                                    " expected " + std::to_string(expected));
                        stats.failedRuns++;
                    }
                    else
                    {
                        stats.successfulRuns++;
                    }
                    break;
                }
                case 1:
                {
                    // Runtime call test
                    auto module = buildRuntimeCallModule(threadId, iter);
                    vm::VM vm(module);
                    int64_t result = vm.run();
                    // abs(-42) + threadId*10000 + iter = 42 + threadId*10000 + iter
                    int64_t expected = 42 + static_cast<int64_t>(threadId) * 10000 + iter;
                    if (result != expected)
                    {
                        gDebugLog.log(threadId, "RUNTIME MISMATCH: got " + std::to_string(result) +
                                                    " expected " + std::to_string(expected));
                        stats.failedRuns++;
                    }
                    else
                    {
                        stats.successfulRuns++;
                    }
                    break;
                }
                case 2:
                {
                    // Complex arithmetic test
                    auto module = buildComplexArithmeticModule(threadId, iter);
                    vm::VM vm(module);
                    int64_t result = vm.run();
                    int64_t expected = static_cast<int64_t>(threadId) * 10000 + iter;
                    if (result != expected)
                    {
                        gDebugLog.log(threadId, "COMPLEX MISMATCH: got " + std::to_string(result) +
                                                    " expected " + std::to_string(expected));
                        stats.failedRuns++;
                    }
                    else
                    {
                        stats.successfulRuns++;
                    }
                    break;
                }
            }

            // Verify thread-local state is clean after each run
            if (vm::activeVMInstance() != nullptr)
            {
                gDebugLog.log(threadId, "ERROR: Active VM not null after run!");
                stats.contextMismatches++;
            }
        }
        catch (const std::exception &e)
        {
            gDebugLog.log(threadId, "Exception: " + std::string(e.what()));
            stats.failedRuns++;
        }
    }
}

/// @brief Test ActiveVMGuard nesting across callbacks.
void testNestedCallbackGuards()
{
    auto module = buildSimpleModule(999, 0);
    vm::VM vm(module);

    // Outer guard
    {
        vm::ActiveVMGuard outer(&vm);
        assert(vm::activeVMInstance() == &vm);

        // Simulate nested callback (same VM is allowed)
        {
            vm::ActiveVMGuard inner(&vm);
            assert(vm::activeVMInstance() == &vm);

            // Even deeper nesting
            {
                vm::ActiveVMGuard innermost(&vm);
                assert(vm::activeVMInstance() == &vm);
            }
            assert(vm::activeVMInstance() == &vm);
        }
        assert(vm::activeVMInstance() == &vm);
    }
    assert(vm::activeVMInstance() == nullptr);
}

/// @brief Test that clearing guard with nullptr works correctly.
void testNullGuard()
{
    auto module = buildSimpleModule(0, 0);
    vm::VM vm(module);

    {
        vm::ActiveVMGuard guard(&vm);
        assert(vm::activeVMInstance() == &vm);

        // Clearing with nullptr guard
        {
            vm::ActiveVMGuard nullGuard(nullptr);
            assert(vm::activeVMInstance() == nullptr);
        }
        assert(vm::activeVMInstance() == &vm);
    }
    assert(vm::activeVMInstance() == nullptr);
}

/// @brief Test rapid VM creation and destruction across threads.
void testRapidVMLifecycle()
{
    constexpr int kNumThreads = 4;
    constexpr int kIterations = 50;
    std::atomic<int> completedThreads{0};
    std::atomic<bool> anyFailure{false};

    std::vector<std::thread> threads;
    for (int t = 0; t < kNumThreads; ++t)
    {
        threads.emplace_back(
            [t, &completedThreads, &anyFailure]()
            {
                for (int i = 0; i < kIterations && !anyFailure; ++i)
                {
                    auto module = buildArithmeticModule(t, i);
                    vm::VM vm(module);
                    int64_t result = vm.run();
                    int64_t expected = static_cast<int64_t>(t) * 10000 + i;
                    if (result != expected)
                    {
                        anyFailure = true;
                    }
                    // Verify clean state
                    if (vm::activeVMInstance() != nullptr)
                    {
                        anyFailure = true;
                    }
                }
                completedThreads++;
            });
    }

    for (auto &th : threads)
        th.join();

    assert(completedThreads == kNumThreads && "All threads should complete");
    assert(!anyFailure && "No failures should occur");
}

/// @brief Test interleaved runtime calls across threads.
void testInterleavedRuntimeCalls()
{
    constexpr int kNumThreads = 4;
    std::atomic<int> successCount{0};
    std::atomic<bool> anyMismatch{false};

    std::vector<std::thread> threads;
    for (int t = 0; t < kNumThreads; ++t)
    {
        threads.emplace_back(
            [t, &successCount, &anyMismatch]()
            {
                for (int i = 0; i < 25 && !anyMismatch; ++i)
                {
                    auto module = buildRuntimeCallModule(t, i);
                    vm::VM vm(module);
                    int64_t result = vm.run();
                    int64_t expected = 42 + static_cast<int64_t>(t) * 10000 + i;
                    if (result != expected)
                    {
                        anyMismatch = true;
                    }
                    else
                    {
                        successCount++;
                    }
                }
            });
    }

    for (auto &th : threads)
        th.join();

    assert(!anyMismatch && "No runtime call context mismatches should occur");
    assert(successCount == kNumThreads * 25 && "All runtime call iterations should succeed");
}

} // namespace

int main(int argc, char **argv)
{
    StressConfig config;

    // Parse command line for debug mode
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--debug" || arg == "-d")
        {
            config.enableDebugLogging = true;
            gDebugLog.enabled = true;
        }
        else if (arg == "--threads" && i + 1 < argc)
        {
            config.numThreads = std::stoi(argv[++i]);
        }
        else if (arg == "--iterations" && i + 1 < argc)
        {
            config.iterationsPerThread = std::stoi(argv[++i]);
        }
    }

    std::cout << "VM Concurrency Stress Test\n";
    std::cout << "Threads: " << config.numThreads << ", Iterations: " << config.iterationsPerThread << "\n";

    // Run prerequisite tests
    std::cout << "Running prerequisite tests...\n";
    testNestedCallbackGuards();
    std::cout << "  [PASS] Nested callback guards\n";

    testNullGuard();
    std::cout << "  [PASS] Null guard handling\n";

    testRapidVMLifecycle();
    std::cout << "  [PASS] Rapid VM lifecycle\n";

    testInterleavedRuntimeCalls();
    std::cout << "  [PASS] Interleaved runtime calls\n";

    // Run main stress test
    std::cout << "Running main stress test...\n";
    auto startTime = std::chrono::steady_clock::now();

    StressStats stats;
    std::vector<std::thread> threads;
    threads.reserve(config.numThreads);

    for (int t = 0; t < config.numThreads; ++t)
    {
        threads.emplace_back(runStressThread, t, std::cref(config), std::ref(stats));
    }

    for (auto &th : threads)
        th.join();

    auto endTime = std::chrono::steady_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    // Report results
    std::cout << "\nResults:\n";
    std::cout << "  Successful runs: " << stats.successfulRuns << "\n";
    std::cout << "  Failed runs: " << stats.failedRuns << "\n";
    std::cout << "  Context mismatches: " << stats.contextMismatches << "\n";
    std::cout << "  Traps handled: " << stats.trapHandled << "\n";
    std::cout << "  Duration: " << durationMs << " ms\n";

    int64_t expectedRuns = static_cast<int64_t>(config.numThreads) * config.iterationsPerThread;
    int64_t totalRuns = stats.successfulRuns + stats.failedRuns;

    assert(totalRuns == expectedRuns && "All iterations should be accounted for");
    assert(stats.failedRuns == 0 && "No runs should fail");
    assert(stats.contextMismatches == 0 && "No context mismatches should occur");

    std::cout << "\n[PASS] All stress tests passed!\n";
    return 0;
}
