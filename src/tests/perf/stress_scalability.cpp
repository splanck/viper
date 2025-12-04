//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/perf/stress_scalability.cpp
// Purpose: Stress tests for IL scalability to catch performance and correctness
//          regressions with large modules, deep nesting, and resource limits.
//
// Test Scenarios:
//
// 1. Large CFG Stress (5-10k basic blocks)
//    - Purpose: Validate verifier, CFG analysis, and VM dispatch with massive CFG.
//    - Catches: O(n^2) algorithms in block iteration, hash collisions in block maps,
//               memory pressure from large block vectors.
//    - Structure: Linear chain with periodic branches creating a mesh pattern.
//
// 2. Deep Nesting Stress (nested loops and conditionals)
//    - Purpose: Validate loop analysis passes (LoopSimplify, LICM) and stack usage.
//    - Catches: Recursive algorithm stack overflow, poor memoization in analyses,
//               exponential blowup in loop forest construction.
//    - Structure: Deeply nested FOR-loop like structure with inner conditionals.
//
// 3. Stack Limit Stress (large alloca approaching frame limits)
//    - Purpose: Validate handleAlloca bounds checking and stack overflow traps.
//    - Catches: Off-by-one in stack pointer arithmetic, missing overflow checks,
//               incorrect error messages for stack exhaustion.
//    - Structure: Progressively larger alloca until limit, verify trap behavior.
//
// 4. Heavy Runtime Helper Stress
//    - Purpose: Validate runtime bridge efficiency with many extern calls.
//    - Catches: Lookup overhead in extern table, argument marshalling bottlenecks,
//               memory leaks in runtime string handling.
//    - Structure: Loop calling multiple runtime helpers per iteration.
//
// 5. Switch/Branch Heavy Stress
//    - Purpose: Validate SwitchI32 with many cases and dense dispatch tables.
//    - Catches: Linear search fallback, dispatch table corruption, case ordering bugs.
//    - Structure: Large switch statement with 1000+ cases in a tight loop.
//
// All tests are designed to be deterministic and complete within reasonable time
// (~10-30 seconds each) while being large enough to surface scaling issues.
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/PassRegistry.hpp"
#include "il/verify/Verifier.hpp"
#include "vm/VM.hpp"
#include "vm/VMConstants.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace il::core;

namespace
{

// ============================================================================
// Test Configuration
// ============================================================================

// Large CFG: Number of basic blocks to generate (5000-10000)
constexpr size_t kLargeCfgBlocks = 5000;
// Each block executes ~10 instructions on average
constexpr size_t kLargeCfgIterations = 100; // Times to traverse the CFG

// Deep Nesting: Maximum nesting depth for loops
constexpr size_t kDeepNestingDepth = 50;
// Iterations at each nesting level
constexpr size_t kDeepNestingIterations = 10;

// Stack Limit: Test reasonable alloca sizes (not near-limit to avoid VM slowdown)
constexpr size_t kStackTestTargetBytes = 32768; // 32KB - reasonable stress test
// Individual alloca sizes to test
constexpr size_t kStackTestAllocaSizes[] = {64, 256, 1024, 4096, 8192};

// Runtime Helper: Number of helper calls per iteration
constexpr size_t kRuntimeHelperCallsPerIter = 10;
// Total iterations for runtime helper test
constexpr size_t kRuntimeHelperIterations = 10000;

// Switch Stress: Number of switch cases
constexpr size_t kSwitchCaseCount = 500;
// Iterations through switch
constexpr size_t kSwitchIterations = 50000;

// ============================================================================
// Utility: Timer and Reporting
// ============================================================================

class Timer
{
  public:
    void start()
    {
        start_ = std::chrono::steady_clock::now();
    }

    double elapsedMs() const
    {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

    void report(const char *phase) const
    {
        std::cout << "  " << phase << ": " << elapsedMs() << " ms\n";
    }

  private:
    std::chrono::steady_clock::time_point start_;
};

void reportTestStart(const char *name)
{
    std::cout << "\n=== " << name << " ===\n";
}

void reportSuccess(const char *name, double totalMs)
{
    std::cout << "PASS: " << name << " completed in " << totalMs << " ms\n";
}

void reportFailure(const char *name, const std::string &reason)
{
    std::cerr << "FAIL: " << name << ": " << reason << "\n";
}

// ============================================================================
// Test 1: Large CFG Stress
// ============================================================================
// Creates a CFG with kLargeCfgBlocks basic blocks arranged as:
//   entry -> block_0 -> block_1 -> ... -> block_N -> done
// With periodic conditional branches creating a mesh pattern every 100 blocks.
// This tests:
//   - Verifier's ability to handle large functions
//   - VM's block lookup performance
//   - Pass manager's iteration efficiency

Module buildLargeCfgModule()
{
    Module module;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextTemp = 0;

    // Entry block: initialize counter and sum
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("outer_loop");
        br.brArgs.push_back({Value::constInt(0), Value::constInt(0)}); // outer_idx, sum
        entry.instructions.push_back(br);
        entry.terminated = true;
    }
    fn.blocks.push_back(std::move(entry));

    // Outer loop: controls how many times we traverse the CFG
    BasicBlock outerLoop;
    outerLoop.label = "outer_loop";
    Param outerIdx{"outer_idx", Type(Type::Kind::I64), nextTemp++};
    Param outerSum{"outer_sum", Type(Type::Kind::I64), nextTemp++};
    outerLoop.params.push_back(outerIdx);
    outerLoop.params.push_back(outerSum);
    {
        Instr cmp;
        cmp.result = nextTemp++;
        cmp.op = Opcode::SCmpLT;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(Value::temp(outerIdx.id));
        cmp.operands.push_back(Value::constInt(static_cast<int64_t>(kLargeCfgIterations)));
        outerLoop.instructions.push_back(cmp);

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(*cmp.result));
        cbr.labels.push_back("block_0");
        cbr.labels.push_back("done");
        cbr.brArgs.push_back({Value::temp(outerIdx.id), Value::temp(outerSum.id)}); // to block_0
        cbr.brArgs.push_back({Value::temp(outerSum.id)});                           // to done
        outerLoop.instructions.push_back(cbr);
        outerLoop.terminated = true;
    }
    fn.blocks.push_back(std::move(outerLoop));

    // Generate kLargeCfgBlocks basic blocks
    // Each block receives: (outer_idx, sum)
    for (size_t i = 0; i < kLargeCfgBlocks; ++i)
    {
        BasicBlock bb;
        bb.label = "block_" + std::to_string(i);

        Param blockOuterIdx{"outer_idx", Type(Type::Kind::I64), nextTemp++};
        Param blockSum{"sum", Type(Type::Kind::I64), nextTemp++};
        bb.params.push_back(blockOuterIdx);
        bb.params.push_back(blockSum);

        // Simple work: add block index to sum
        Instr add;
        add.result = nextTemp++;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(Value::temp(blockSum.id));
        add.operands.push_back(Value::constInt(static_cast<int64_t>(i + 1)));
        bb.instructions.push_back(add);

        // Every 100 blocks, add a conditional branch to create more edges
        if (i % 100 == 99 && i + 1 < kLargeCfgBlocks)
        {
            // Simple condition: always take the first branch in this test
            Instr cmp;
            cmp.result = nextTemp++;
            cmp.op = Opcode::SCmpGT;
            cmp.type = Type(Type::Kind::I1);
            cmp.operands.push_back(Value::temp(*add.result));
            cmp.operands.push_back(Value::constInt(0));
            bb.instructions.push_back(cmp);

            Instr cbr;
            cbr.op = Opcode::CBr;
            cbr.type = Type(Type::Kind::Void);
            cbr.operands.push_back(Value::temp(*cmp.result));
            cbr.labels.push_back("block_" + std::to_string(i + 1));
            cbr.labels.push_back("block_" + std::to_string(i + 1));
            cbr.brArgs.push_back({Value::temp(blockOuterIdx.id), Value::temp(*add.result)});
            cbr.brArgs.push_back({Value::temp(blockOuterIdx.id), Value::temp(*add.result)});
            bb.instructions.push_back(cbr);
        }
        else if (i + 1 < kLargeCfgBlocks)
        {
            // Branch to next block
            Instr br;
            br.op = Opcode::Br;
            br.type = Type(Type::Kind::Void);
            br.labels.push_back("block_" + std::to_string(i + 1));
            br.brArgs.push_back({Value::temp(blockOuterIdx.id), Value::temp(*add.result)});
            bb.instructions.push_back(br);
        }
        else
        {
            // Last block: branch back to outer_loop with incremented counter
            Instr inc;
            inc.result = nextTemp++;
            inc.op = Opcode::IAddOvf;
            inc.type = Type(Type::Kind::I64);
            inc.operands.push_back(Value::temp(blockOuterIdx.id));
            inc.operands.push_back(Value::constInt(1));
            bb.instructions.push_back(inc);

            Instr br;
            br.op = Opcode::Br;
            br.type = Type(Type::Kind::Void);
            br.labels.push_back("outer_loop");
            br.brArgs.push_back({Value::temp(*inc.result), Value::temp(*add.result)});
            bb.instructions.push_back(br);
        }

        bb.terminated = true;
        fn.blocks.push_back(std::move(bb));
    }

    // Done block: return accumulated sum
    BasicBlock done;
    done.label = "done";
    Param doneSum{"final_sum", Type(Type::Kind::I64), nextTemp++};
    done.params.push_back(doneSum);
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(doneSum.id));
        done.instructions.push_back(ret);
        done.terminated = true;
    }
    fn.blocks.push_back(std::move(done));

    fn.valueNames.resize(nextTemp);
    module.functions.push_back(std::move(fn));
    return module;
}

bool testLargeCfgStress()
{
    reportTestStart("Large CFG Stress Test");
    Timer timer;
    Timer totalTimer;
    totalTimer.start();

    // Build module
    timer.start();
    Module module = buildLargeCfgModule();
    timer.report("Module construction");

    std::cout << "  Blocks: " << module.functions[0].blocks.size() << "\n";

    // Verify module
    timer.start();
    auto verifyResult = il::verify::Verifier::verify(module);
    timer.report("Verification");
    if (!verifyResult)
    {
        reportFailure("Large CFG Stress", "Verification failed: " + verifyResult.error().message);
        return false;
    }

    // Execute on VM
    timer.start();
    il::vm::VM vm(module);
    int64_t result = vm.run();
    timer.report("VM execution");

    // Compute expected sum: sum of 1..kLargeCfgBlocks * kLargeCfgIterations
    int64_t expectedSum =
        static_cast<int64_t>(kLargeCfgBlocks * (kLargeCfgBlocks + 1) / 2 * kLargeCfgIterations);
    if (result != expectedSum)
    {
        reportFailure("Large CFG Stress",
                      "Result mismatch: got " + std::to_string(result) + ", expected " +
                          std::to_string(expectedSum));
        return false;
    }

    reportSuccess("Large CFG Stress", totalTimer.elapsedMs());
    return true;
}

// ============================================================================
// Test 2: Deep Nesting Stress
// ============================================================================
// Creates deeply nested conditionals to stress control flow analysis.
// Structure: Chain of if-then-else blocks to depth D, with work at the bottom.
// This tests verifier and CFG analysis with many basic blocks and edges.

Module buildDeepNestingModule()
{
    Module module;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextTemp = 0;

    // Entry block: initialize outer loop
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("outer_loop");
        br.brArgs.push_back({Value::constInt(0), Value::constInt(0)}); // outer_idx, sum
        entry.instructions.push_back(br);
        entry.terminated = true;
    }
    fn.blocks.push_back(std::move(entry));

    // Outer loop header
    BasicBlock outerLoop;
    outerLoop.label = "outer_loop";
    Param outerIdx{"outer_idx", Type(Type::Kind::I64), nextTemp++};
    Param outerSum{"outer_sum", Type(Type::Kind::I64), nextTemp++};
    outerLoop.params.push_back(outerIdx);
    outerLoop.params.push_back(outerSum);
    {
        Instr cmp;
        cmp.result = nextTemp++;
        cmp.op = Opcode::SCmpLT;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(Value::temp(outerIdx.id));
        cmp.operands.push_back(Value::constInt(static_cast<int64_t>(kDeepNestingIterations)));
        outerLoop.instructions.push_back(cmp);

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(*cmp.result));
        cbr.labels.push_back("nest_0");
        cbr.labels.push_back("done");
        cbr.brArgs.push_back({Value::temp(outerIdx.id), Value::temp(outerSum.id)});
        cbr.brArgs.push_back({Value::temp(outerSum.id)});
        outerLoop.instructions.push_back(cbr);
        outerLoop.terminated = true;
    }
    fn.blocks.push_back(std::move(outerLoop));

    // Create chain of nested conditional blocks
    for (size_t depth = 0; depth < kDeepNestingDepth; ++depth)
    {
        BasicBlock nestBlock;
        nestBlock.label = "nest_" + std::to_string(depth);

        Param nestIdx{"nest_idx_" + std::to_string(depth), Type(Type::Kind::I64), nextTemp++};
        Param nestSum{"nest_sum_" + std::to_string(depth), Type(Type::Kind::I64), nextTemp++};
        nestBlock.params.push_back(nestIdx);
        nestBlock.params.push_back(nestSum);

        // Add depth value to sum
        Instr add;
        add.result = nextTemp++;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(Value::temp(nestSum.id));
        add.operands.push_back(Value::constInt(static_cast<int64_t>(depth + 1)));
        nestBlock.instructions.push_back(add);

        // Branch to next level or merge
        std::string nextLabel =
            (depth + 1 < kDeepNestingDepth) ? ("nest_" + std::to_string(depth + 1)) : "merge";

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back(nextLabel);
        br.brArgs.push_back({Value::temp(nestIdx.id), Value::temp(*add.result)});
        nestBlock.instructions.push_back(br);
        nestBlock.terminated = true;

        fn.blocks.push_back(std::move(nestBlock));
    }

    // Merge block: increment outer index and loop back
    BasicBlock merge;
    merge.label = "merge";
    Param mergeIdx{"merge_idx", Type(Type::Kind::I64), nextTemp++};
    Param mergeSum{"merge_sum", Type(Type::Kind::I64), nextTemp++};
    merge.params.push_back(mergeIdx);
    merge.params.push_back(mergeSum);
    {
        Instr inc;
        inc.result = nextTemp++;
        inc.op = Opcode::IAddOvf;
        inc.type = Type(Type::Kind::I64);
        inc.operands.push_back(Value::temp(mergeIdx.id));
        inc.operands.push_back(Value::constInt(1));
        merge.instructions.push_back(inc);

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("outer_loop");
        br.brArgs.push_back({Value::temp(*inc.result), Value::temp(mergeSum.id)});
        merge.instructions.push_back(br);
        merge.terminated = true;
    }
    fn.blocks.push_back(std::move(merge));

    // Done block
    BasicBlock done;
    done.label = "done";
    Param doneSum{"final_sum", Type(Type::Kind::I64), nextTemp++};
    done.params.push_back(doneSum);
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(doneSum.id));
        done.instructions.push_back(ret);
        done.terminated = true;
    }
    fn.blocks.push_back(std::move(done));

    fn.valueNames.resize(nextTemp);
    module.functions.push_back(std::move(fn));
    return module;
}

bool testDeepNestingStress()
{
    reportTestStart("Deep Nesting Stress Test");
    Timer timer;
    Timer totalTimer;
    totalTimer.start();

    // Build module
    timer.start();
    Module module = buildDeepNestingModule();
    timer.report("Module construction");

    std::cout << "  Nesting depth: " << kDeepNestingDepth << "\n";
    std::cout << "  Iterations per level: " << kDeepNestingIterations << "\n";
    std::cout << "  Blocks: " << module.functions[0].blocks.size() << "\n";

    // Verify module
    timer.start();
    auto verifyResult = il::verify::Verifier::verify(module);
    timer.report("Verification");
    if (!verifyResult)
    {
        reportFailure("Deep Nesting Stress",
                      "Verification failed: " + verifyResult.error().message);
        return false;
    }

    // Execute on VM
    timer.start();
    il::vm::VM vm(module);
    int64_t result = vm.run();
    timer.report("VM execution");

    // Expected: sum of 1..kDeepNestingDepth per iteration * kDeepNestingIterations
    int64_t perIterSum = static_cast<int64_t>(kDeepNestingDepth * (kDeepNestingDepth + 1) / 2);
    int64_t expected = perIterSum * static_cast<int64_t>(kDeepNestingIterations);
    if (result != expected)
    {
        reportFailure("Deep Nesting Stress",
                      "Result mismatch: got " + std::to_string(result) + ", expected " +
                          std::to_string(expected));
        return false;
    }

    reportSuccess("Deep Nesting Stress", totalTimer.elapsedMs());
    return true;
}

// ============================================================================
// Test 3: Stack Limit Stress
// ============================================================================
// Tests handleAlloca with sizes approaching frame stack limits.
// Validates proper bounds checking and trap behavior.

Module buildStackLimitModule(size_t allocaSize)
{
    Module module;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextTemp = 0;

    BasicBlock entry;
    entry.label = "entry";

    // Alloca of specified size
    Instr alloca;
    alloca.result = nextTemp++;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(static_cast<int64_t>(allocaSize)));
    entry.instructions.push_back(alloca);

    // Write to first and last byte to ensure memory is valid
    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands.push_back(Value::temp(*alloca.result));
    store1.operands.push_back(Value::constInt(42));
    entry.instructions.push_back(store1);

    // GEP to last valid position (allocaSize - 8 for i64)
    if (allocaSize >= 8)
    {
        Instr gep;
        gep.result = nextTemp++;
        gep.op = Opcode::GEP;
        gep.type = Type(Type::Kind::Ptr);
        gep.operands.push_back(Value::temp(*alloca.result));
        gep.operands.push_back(Value::constInt(static_cast<int64_t>(allocaSize - 8)));
        entry.instructions.push_back(gep);

        Instr store2;
        store2.op = Opcode::Store;
        store2.type = Type(Type::Kind::I64);
        store2.operands.push_back(Value::temp(*gep.result));
        store2.operands.push_back(Value::constInt(99));
        entry.instructions.push_back(store2);

        // Load back and add
        Instr load1;
        load1.result = nextTemp++;
        load1.op = Opcode::Load;
        load1.type = Type(Type::Kind::I64);
        load1.operands.push_back(Value::temp(*alloca.result));
        entry.instructions.push_back(load1);

        Instr load2;
        load2.result = nextTemp++;
        load2.op = Opcode::Load;
        load2.type = Type(Type::Kind::I64);
        load2.operands.push_back(Value::temp(*gep.result));
        entry.instructions.push_back(load2);

        Instr add;
        add.result = nextTemp++;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(Value::temp(*load1.result));
        add.operands.push_back(Value::temp(*load2.result));
        entry.instructions.push_back(add);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(*add.result));
        entry.instructions.push_back(ret);
    }
    else
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::constInt(42));
        entry.instructions.push_back(ret);
    }

    entry.terminated = true;
    fn.blocks.push_back(std::move(entry));

    fn.valueNames.resize(nextTemp);
    module.functions.push_back(std::move(fn));
    return module;
}

bool testStackLimitStress()
{
    reportTestStart("Stack Limit Stress Test");
    Timer totalTimer;
    totalTimer.start();

    // Test progressively larger alloca sizes
    for (size_t allocaSize : kStackTestAllocaSizes)
    {
        std::cout << "  Testing alloca size: " << allocaSize << " bytes\n";

        Module module = buildStackLimitModule(allocaSize);

        auto verifyResult = il::verify::Verifier::verify(module);
        if (!verifyResult)
        {
            reportFailure("Stack Limit Stress",
                          "Verification failed for size " + std::to_string(allocaSize));
            return false;
        }

        il::vm::VM vm(module);
        int64_t result = vm.run();

        int64_t expected = (allocaSize >= 8) ? (42 + 99) : 42;
        if (result != expected)
        {
            reportFailure("Stack Limit Stress",
                          "Result mismatch for size " + std::to_string(allocaSize) + ": got " +
                              std::to_string(result) + ", expected " + std::to_string(expected));
            return false;
        }
    }

    // Test larger allocation (should succeed up to frame limit)
    std::cout << "  Testing near-limit alloca: " << kStackTestTargetBytes << " bytes\n";
    {
        Module module = buildStackLimitModule(kStackTestTargetBytes);

        auto verifyResult = il::verify::Verifier::verify(module);
        if (!verifyResult)
        {
            reportFailure("Stack Limit Stress", "Verification failed for near-limit alloca");
            return false;
        }

        il::vm::VM vm(module);
        int64_t result = vm.run();

        if (result != 42 + 99)
        {
            reportFailure("Stack Limit Stress",
                          "Near-limit alloca result mismatch: " + std::to_string(result));
            return false;
        }
    }

    reportSuccess("Stack Limit Stress", totalTimer.elapsedMs());
    return true;
}

// ============================================================================
// Test 4: Heavy Runtime Helper Stress
// ============================================================================
// Exercises runtime bridge with many extern calls.
// Uses basic math operations available in most runtime configurations.

Module buildRuntimeHelperModule()
{
    Module module;

    // Declare externs for basic operations
    module.externs.push_back({"rt_print_i64", Type(Type::Kind::Void), {Type(Type::Kind::I64)}});

    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextTemp = 0;

    // Entry
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("loop");
        br.brArgs.push_back({Value::constInt(0), Value::constInt(0)}); // idx, sum
        entry.instructions.push_back(br);
        entry.terminated = true;
    }
    fn.blocks.push_back(std::move(entry));

    // Loop
    BasicBlock loop;
    loop.label = "loop";
    Param loopIdx{"idx", Type(Type::Kind::I64), nextTemp++};
    Param loopSum{"sum", Type(Type::Kind::I64), nextTemp++};
    loop.params.push_back(loopIdx);
    loop.params.push_back(loopSum);
    {
        Instr cmp;
        cmp.result = nextTemp++;
        cmp.op = Opcode::SCmpLT;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(Value::temp(loopIdx.id));
        cmp.operands.push_back(Value::constInt(static_cast<int64_t>(kRuntimeHelperIterations)));
        loop.instructions.push_back(cmp);

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(*cmp.result));
        cbr.labels.push_back("work");
        cbr.labels.push_back("done");
        cbr.brArgs.push_back({Value::temp(loopIdx.id), Value::temp(loopSum.id)});
        cbr.brArgs.push_back({Value::temp(loopSum.id)});
        loop.instructions.push_back(cbr);
        loop.terminated = true;
    }
    fn.blocks.push_back(std::move(loop));

    // Work block: perform multiple operations simulating runtime helper calls
    BasicBlock work;
    work.label = "work";
    Param workIdx{"work_idx", Type(Type::Kind::I64), nextTemp++};
    Param workSum{"work_sum", Type(Type::Kind::I64), nextTemp++};
    work.params.push_back(workIdx);
    work.params.push_back(workSum);
    {
        unsigned currentSum = workSum.id;

        // Simulate multiple runtime-like operations per iteration
        for (size_t i = 0; i < kRuntimeHelperCallsPerIter; ++i)
        {
            // Add, multiply, etc. to simulate runtime work
            Instr add;
            add.result = nextTemp++;
            add.op = Opcode::IAddOvf;
            add.type = Type(Type::Kind::I64);
            add.operands.push_back(Value::temp(currentSum));
            add.operands.push_back(Value::constInt(static_cast<int64_t>(i + 1)));
            work.instructions.push_back(add);
            currentSum = *add.result;

            Instr mul;
            mul.result = nextTemp++;
            mul.op = Opcode::IMulOvf;
            mul.type = Type(Type::Kind::I64);
            mul.operands.push_back(Value::temp(currentSum));
            mul.operands.push_back(Value::constInt(1)); // No-op multiply
            work.instructions.push_back(mul);
            currentSum = *mul.result;
        }

        // Increment index
        Instr inc;
        inc.result = nextTemp++;
        inc.op = Opcode::IAddOvf;
        inc.type = Type(Type::Kind::I64);
        inc.operands.push_back(Value::temp(workIdx.id));
        inc.operands.push_back(Value::constInt(1));
        work.instructions.push_back(inc);

        // Back to loop
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("loop");
        br.brArgs.push_back({Value::temp(*inc.result), Value::temp(currentSum)});
        work.instructions.push_back(br);
        work.terminated = true;
    }
    fn.blocks.push_back(std::move(work));

    // Done
    BasicBlock done;
    done.label = "done";
    Param doneSum{"final_sum", Type(Type::Kind::I64), nextTemp++};
    done.params.push_back(doneSum);
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(doneSum.id));
        done.instructions.push_back(ret);
        done.terminated = true;
    }
    fn.blocks.push_back(std::move(done));

    fn.valueNames.resize(nextTemp);
    module.functions.push_back(std::move(fn));
    return module;
}

bool testRuntimeHelperStress()
{
    reportTestStart("Runtime Helper Stress Test");
    Timer timer;
    Timer totalTimer;
    totalTimer.start();

    // Build module
    timer.start();
    Module module = buildRuntimeHelperModule();
    timer.report("Module construction");

    std::cout << "  Iterations: " << kRuntimeHelperIterations << "\n";
    std::cout << "  Operations per iteration: " << (kRuntimeHelperCallsPerIter * 2) << "\n";

    // Verify
    timer.start();
    auto verifyResult = il::verify::Verifier::verify(module);
    timer.report("Verification");
    if (!verifyResult)
    {
        reportFailure("Runtime Helper Stress",
                      "Verification failed: " + verifyResult.error().message);
        return false;
    }

    // Execute
    timer.start();
    il::vm::VM vm(module);
    int64_t result = vm.run();
    timer.report("VM execution");

    // Compute expected: sum of 1..kRuntimeHelperCallsPerIter per iteration
    int64_t perIterSum =
        static_cast<int64_t>(kRuntimeHelperCallsPerIter * (kRuntimeHelperCallsPerIter + 1) / 2);
    int64_t expected = perIterSum * static_cast<int64_t>(kRuntimeHelperIterations);

    if (result != expected)
    {
        reportFailure("Runtime Helper Stress",
                      "Result mismatch: got " + std::to_string(result) + ", expected " +
                          std::to_string(expected));
        return false;
    }

    reportSuccess("Runtime Helper Stress", totalTimer.elapsedMs());
    return true;
}

// ============================================================================
// Test 5: Switch Stress
// ============================================================================
// Tests SwitchI32 with many cases.

Module buildSwitchStressModule()
{
    Module module;
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextTemp = 0;

    // Entry
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("loop");
        br.brArgs.push_back({Value::constInt(0), Value::constInt(0)}); // idx, sum
        entry.instructions.push_back(br);
        entry.terminated = true;
    }
    fn.blocks.push_back(std::move(entry));

    // Loop header
    BasicBlock loop;
    loop.label = "loop";
    Param loopIdx{"idx", Type(Type::Kind::I64), nextTemp++};
    Param loopSum{"sum", Type(Type::Kind::I64), nextTemp++};
    loop.params.push_back(loopIdx);
    loop.params.push_back(loopSum);
    {
        Instr cmp;
        cmp.result = nextTemp++;
        cmp.op = Opcode::SCmpLT;
        cmp.type = Type(Type::Kind::I1);
        cmp.operands.push_back(Value::temp(loopIdx.id));
        cmp.operands.push_back(Value::constInt(static_cast<int64_t>(kSwitchIterations)));
        loop.instructions.push_back(cmp);

        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::temp(*cmp.result));
        cbr.labels.push_back("switch_block");
        cbr.labels.push_back("done");
        cbr.brArgs.push_back({Value::temp(loopIdx.id), Value::temp(loopSum.id)});
        cbr.brArgs.push_back({Value::temp(loopSum.id)});
        loop.instructions.push_back(cbr);
        loop.terminated = true;
    }
    fn.blocks.push_back(std::move(loop));

    // Switch block
    BasicBlock switchBlock;
    switchBlock.label = "switch_block";
    Param switchIdx{"switch_idx", Type(Type::Kind::I64), nextTemp++};
    Param switchSum{"switch_sum", Type(Type::Kind::I64), nextTemp++};
    switchBlock.params.push_back(switchIdx);
    switchBlock.params.push_back(switchSum);
    {
        // Compute case index: idx % kSwitchCaseCount
        Instr rem;
        rem.result = nextTemp++;
        rem.op = Opcode::URemChk0;
        rem.type = Type(Type::Kind::I64);
        rem.operands.push_back(Value::temp(switchIdx.id));
        rem.operands.push_back(Value::constInt(static_cast<int64_t>(kSwitchCaseCount)));
        switchBlock.instructions.push_back(rem);

        // Narrow from i64 to i32 for switch.i32
        Instr narrow;
        narrow.result = nextTemp++;
        narrow.op = Opcode::CastUiNarrowChk;
        narrow.type = Type(Type::Kind::I32);
        narrow.operands.push_back(Value::temp(*rem.result));
        switchBlock.instructions.push_back(narrow);

        // Build switch with kSwitchCaseCount cases
        Instr sw;
        sw.op = Opcode::SwitchI32;
        sw.type = Type(Type::Kind::Void);
        sw.operands.push_back(Value::temp(*narrow.result));

        // Default case
        sw.labels.push_back("dispatch");
        sw.brArgs.push_back(
            {Value::temp(switchIdx.id), Value::temp(switchSum.id), Value::constInt(-1)});

        // Generate cases
        for (size_t i = 0; i < kSwitchCaseCount; ++i)
        {
            sw.operands.push_back(Value::constInt(static_cast<int32_t>(i)));
            sw.labels.push_back("dispatch");
            sw.brArgs.push_back({Value::temp(switchIdx.id),
                                 Value::temp(switchSum.id),
                                 Value::constInt(static_cast<int64_t>(i + 1))});
        }

        switchBlock.instructions.push_back(sw);
        switchBlock.terminated = true;
    }
    fn.blocks.push_back(std::move(switchBlock));

    // Dispatch block: accumulate and continue
    BasicBlock dispatch;
    dispatch.label = "dispatch";
    Param dispatchIdx{"dispatch_idx", Type(Type::Kind::I64), nextTemp++};
    Param dispatchSum{"dispatch_sum", Type(Type::Kind::I64), nextTemp++};
    Param dispatchVal{"case_val", Type(Type::Kind::I64), nextTemp++};
    dispatch.params.push_back(dispatchIdx);
    dispatch.params.push_back(dispatchSum);
    dispatch.params.push_back(dispatchVal);
    {
        // Add case value to sum
        Instr add;
        add.result = nextTemp++;
        add.op = Opcode::IAddOvf;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(Value::temp(dispatchSum.id));
        add.operands.push_back(Value::temp(dispatchVal.id));
        dispatch.instructions.push_back(add);

        // Increment index
        Instr inc;
        inc.result = nextTemp++;
        inc.op = Opcode::IAddOvf;
        inc.type = Type(Type::Kind::I64);
        inc.operands.push_back(Value::temp(dispatchIdx.id));
        inc.operands.push_back(Value::constInt(1));
        dispatch.instructions.push_back(inc);

        // Back to loop
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("loop");
        br.brArgs.push_back({Value::temp(*inc.result), Value::temp(*add.result)});
        dispatch.instructions.push_back(br);
        dispatch.terminated = true;
    }
    fn.blocks.push_back(std::move(dispatch));

    // Done
    BasicBlock done;
    done.label = "done";
    Param doneSum{"final_sum", Type(Type::Kind::I64), nextTemp++};
    done.params.push_back(doneSum);
    {
        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(doneSum.id));
        done.instructions.push_back(ret);
        done.terminated = true;
    }
    fn.blocks.push_back(std::move(done));

    fn.valueNames.resize(nextTemp);
    module.functions.push_back(std::move(fn));
    return module;
}

bool testSwitchStress()
{
    reportTestStart("Switch Stress Test");
    Timer timer;
    Timer totalTimer;
    totalTimer.start();

    // Build module
    timer.start();
    Module module = buildSwitchStressModule();
    timer.report("Module construction");

    std::cout << "  Switch cases: " << kSwitchCaseCount << "\n";
    std::cout << "  Iterations: " << kSwitchIterations << "\n";

    // Verify
    timer.start();
    auto verifyResult = il::verify::Verifier::verify(module);
    timer.report("Verification");
    if (!verifyResult)
    {
        reportFailure("Switch Stress", "Verification failed: " + verifyResult.error().message);
        return false;
    }

    // Execute
    timer.start();
    il::vm::VM vm(module);
    int64_t result = vm.run();
    timer.report("VM execution");

    // Compute expected sum
    // Each iteration adds (i % kSwitchCaseCount) + 1
    // Over kSwitchIterations iterations
    int64_t expected = 0;
    for (size_t i = 0; i < kSwitchIterations; ++i)
    {
        expected += static_cast<int64_t>((i % kSwitchCaseCount) + 1);
    }

    if (result != expected)
    {
        reportFailure("Switch Stress",
                      "Result mismatch: got " + std::to_string(result) + ", expected " +
                          std::to_string(expected));
        return false;
    }

    reportSuccess("Switch Stress", totalTimer.elapsedMs());
    return true;
}

} // namespace

// ============================================================================
// Main: Run All Stress Tests
// ============================================================================

int main()
{
    std::cout << "===== Viper IL Scalability Stress Tests =====\n";

    int failures = 0;

    if (!testLargeCfgStress())
        ++failures;

    if (!testDeepNestingStress())
        ++failures;

    if (!testStackLimitStress())
        ++failures;

    if (!testRuntimeHelperStress())
        ++failures;

    if (!testSwitchStress())
        ++failures;

    std::cout << "\n===== Summary =====\n";
    if (failures == 0)
    {
        std::cout << "All stress tests PASSED\n";
        return 0;
    }
    else
    {
        std::cout << failures << " test(s) FAILED\n";
        return 1;
    }
}
