//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for updated Inliner thresholds.
//
// The thresholds changed from:
//   instrThreshold=32, blockBudget=4, maxInlineDepth=2
// to:
//   instrThreshold=80, blockBudget=8, maxInlineDepth=3
//
// Tests verify:
//   1. Default InlineCostConfig uses new thresholds.
//   2. A 50-instruction callee is inlined (would have been rejected at 32).
//   3. A callee exceeding the new threshold (> 80 instrs) is not inlined.
//
//===----------------------------------------------------------------------===//

#include "il/transform/AnalysisManager.hpp"
#include "il/transform/Inline.hpp"
#include "il/transform/PassRegistry.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

unsigned countCallsInModule(const Module &module)
{
    unsigned count = 0;
    for (const auto &fn : module.functions)
        for (const auto &bb : fn.blocks)
            for (const auto &instr : bb.instructions)
                if (instr.op == Opcode::Call)
                    ++count;
    return count;
}

unsigned countInstructionsInFunction(const Function &fn)
{
    unsigned count = 0;
    for (const auto &bb : fn.blocks)
        count += bb.instructions.size();
    return count;
}

/// Build a module with a caller that calls `callee` once, where `callee` has
/// `instrCount` Add instructions plus a Ret.
///
///   fn callee(x: i64) -> i64:
///     entry:
///       t0 = add x, 0
///       t1 = add t0, 0
///       ...  (instrCount times)
///       ret tN
///
///   fn caller() -> i64:
///     entry:
///       t0 = call callee(42)
///       ret t0
Module buildCalleeWithNInstrs(unsigned instrCount)
{
    Module module;

    // Build callee
    Function callee;
    callee.name = "callee";
    callee.retType = Type(Type::Kind::I64);
    {
        Param p;
        p.id = 0;
        p.type = Type(Type::Kind::I64);
        p.name = "x";
        callee.params.push_back(std::move(p));
    }

    unsigned nextId = 1; // t0 is the param
    BasicBlock entry;
    entry.label = "entry";

    Value prev = Value::temp(0); // start from param
    for (unsigned i = 0; i < instrCount; ++i)
    {
        Instr add;
        add.result = nextId++;
        add.op = Opcode::Add;
        add.type = Type(Type::Kind::I64);
        add.operands.push_back(prev);
        add.operands.push_back(Value::constInt(0)); // add 0 = identity (foldable but not yet)
        prev = Value::temp(add.result.value());
        entry.instructions.push_back(std::move(add));
    }

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(prev);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    callee.blocks.push_back(std::move(entry));
    callee.valueNames.resize(nextId);
    callee.valueNames[0] = "x";
    module.functions.push_back(std::move(callee));

    // Build caller
    Function caller;
    caller.name = "caller";
    caller.retType = Type(Type::Kind::I64);

    unsigned callerNextId = 0;
    BasicBlock callerEntry;
    callerEntry.label = "entry";

    Instr call;
    call.result = callerNextId++;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "callee"; // direct-call target; inliner checks I.callee, not labels
    call.operands.push_back(Value::constInt(42));
    callerEntry.instructions.push_back(std::move(call));

    Instr callerRet;
    callerRet.op = Opcode::Ret;
    callerRet.type = Type(Type::Kind::Void);
    callerRet.operands.push_back(Value::temp(0));
    callerEntry.instructions.push_back(std::move(callerRet));
    callerEntry.terminated = true;

    caller.blocks.push_back(std::move(callerEntry));
    caller.valueNames.resize(callerNextId);
    module.functions.push_back(std::move(caller));

    return module;
}

} // namespace

// Default InlineCostConfig thresholds must match the new values.
TEST(InlinerThreshold, DefaultThresholdsAreUpdated)
{
    il::transform::InlineCostConfig cfg;
    EXPECT_EQ(cfg.instrThreshold, 80u);
    EXPECT_EQ(cfg.blockBudget, 8u);
    EXPECT_EQ(cfg.maxInlineDepth, 3u);
}

// A 50-instruction callee must be inlined with the new threshold (80).
// It would have been rejected at the old threshold (32).
TEST(InlinerThreshold, Inlines50InstrCallee)
{
    Module module = buildCalleeWithNInstrs(50);
    ASSERT_EQ(module.functions.size(), 2u); // callee + caller

    unsigned callsBefore = countCallsInModule(module);
    ASSERT_EQ(callsBefore, 1u);

    il::transform::Inliner inliner{il::transform::InlineCostConfig{}};
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(module, reg);
    inliner.run(module, AM);

    // After inlining, the call site must be gone
    unsigned callsAfter = countCallsInModule(module);
    EXPECT_EQ(callsAfter, 0u);
}

// A callee with > 80 instructions must NOT be inlined (exceeds new threshold).
// Even with singleUseBonus(10) + constArgBonus(4) = 14, a 100-Add callee has
// instrCount=101; adjustedCost = 101 - 14 = 87 > 80, so it stays un-inlined.
TEST(InlinerThreshold, DoesNotInlineOversizedCallee)
{
    Module module = buildCalleeWithNInstrs(100); // 101 instrs; adj cost 87 > 80
    ASSERT_EQ(module.functions.size(), 2u);

    unsigned callsBefore = countCallsInModule(module);
    ASSERT_EQ(callsBefore, 1u);

    il::transform::Inliner inliner{il::transform::InlineCostConfig{}};
    il::transform::AnalysisRegistry reg;
    il::transform::AnalysisManager AM(module, reg);
    inliner.run(module, AM);

    // Call must remain — callee is too large
    unsigned callsAfter = countCallsInModule(module);
    EXPECT_EQ(callsAfter, 1u);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
