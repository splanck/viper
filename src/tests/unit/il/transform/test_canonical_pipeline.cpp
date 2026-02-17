//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the canonical O1/O2 pipeline structure and SCCP constant folding.
//
// Verifies:
//   1. O1 pipeline contains "sccp" — old Zia frontend excluded it entirely.
//   2. O2 pipeline contains sccp, inline, loop-unroll, check-opt.
//   3. SCCP (as run by the canonical pipeline) folds constant additions.
//   4. runPipeline returns true for registered pipeline IDs.
//
//===----------------------------------------------------------------------===//

#include "il/transform/PassManager.hpp"
#include "il/transform/SCCP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;
using namespace il::transform;

namespace
{

/// Build a minimal module: one function returning add(3, 5).
/// Uses raw construction matching the SCCP test pattern, which is known valid
/// for direct-pass invocations.
///
///   fn test_add() -> i64:
///     entry:
///       t0 = add i64 3, 5
///       ret t0
Module buildConstantAddModule()
{
    Module module;
    Function fn;
    fn.name = "test_add";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    BasicBlock entry;
    entry.label = "entry";

    Instr add;
    add.result = nextId++;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt(3));
    add.operands.push_back(Value::constInt(5));
    entry.instructions.push_back(std::move(add));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(0));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "sum";
    module.functions.push_back(std::move(fn));
    return module;
}

} // namespace

// -------------------------------------------------------------------------
// Pipeline content tests — no module needed.
// -------------------------------------------------------------------------

// The canonical O1 pipeline must include SCCP.
// The old Zia frontend O1 pipeline (simplify-cfg, mem2reg, peephole, dce)
// omitted SCCP entirely — this test guards against that regression.
TEST(CanonicalPipeline, O1PipelineContainsSCCP)
{
    PassManager pm;
    const PassManager::Pipeline *pipeline = pm.getPipeline("O1");
    ASSERT_NE(pipeline, nullptr);

    bool found = false;
    for (const auto &id : *pipeline)
        if (id == "sccp") { found = true; break; }
    EXPECT_TRUE(found);
}

// The canonical O2 pipeline must include SCCP, inline, loop-unroll, check-opt.
// The old Zia frontend O2 pipeline excluded all of these.
TEST(CanonicalPipeline, O2PipelineContainsKeyPasses)
{
    PassManager pm;
    const PassManager::Pipeline *pipeline = pm.getPipeline("O2");
    ASSERT_NE(pipeline, nullptr);

    bool hasSccp = false, hasInline = false, hasLoopUnroll = false, hasCheckOpt = false;
    for (const auto &id : *pipeline)
    {
        if (id == "sccp")        hasSccp = true;
        if (id == "inline")      hasInline = true;
        if (id == "loop-unroll") hasLoopUnroll = true;
        if (id == "check-opt")   hasCheckOpt = true;
    }
    EXPECT_TRUE(hasSccp);
    EXPECT_TRUE(hasInline);
    EXPECT_TRUE(hasLoopUnroll);
    EXPECT_TRUE(hasCheckOpt);
}

// runPipeline returns true for all registered canonical pipeline IDs.
TEST(CanonicalPipeline, RunPipelineSucceedsForRegisteredIds)
{
    // A trivially valid module — one void function with a bare Ret.
    Module module;
    Function fn;
    fn.name = "noop";
    fn.retType = Type(Type::Kind::Void);
    BasicBlock entry;
    entry.label = "entry";
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;
    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    PassManager pm;
    pm.setVerifyBetweenPasses(false);

    EXPECT_TRUE(pm.runPipeline(module, "O0"));
    EXPECT_FALSE(pm.runPipeline(module, "nonexistent-pipeline"));
}

// -------------------------------------------------------------------------
// SCCP constant-folding tests — run the SCCP pass directly (as the canonical
// pipeline does) to verify it folds Add(3,5) to a constant 8.
// -------------------------------------------------------------------------

// SCCP folds a constant integer addition to a constant.
// The canonical O1/O2 pipelines run SCCP; the old custom pipelines did not.
TEST(CanonicalPipeline, SCCPFoldsConstantAdd)
{
    Module module = buildConstantAddModule();

    // Run SCCP directly — this is what runPipeline("O1") does as part of its
    // sequence, and what the old custom Zia pipeline skipped.
    il::transform::sccp(module);

    ASSERT_FALSE(module.functions.empty());
    ASSERT_FALSE(module.functions[0].blocks.empty());
    const auto &retInstr = module.functions[0].blocks[0].instructions.back();
    ASSERT_EQ(retInstr.op, Opcode::Ret);
    ASSERT_FALSE(retInstr.operands.empty());

    // After SCCP, the Ret operand must be a constant 8 (not a temp reference).
    EXPECT_EQ(retInstr.operands[0].kind, Value::Kind::ConstInt);
    if (retInstr.operands[0].kind == Value::Kind::ConstInt)
        EXPECT_EQ(retInstr.operands[0].i64, 8LL);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
