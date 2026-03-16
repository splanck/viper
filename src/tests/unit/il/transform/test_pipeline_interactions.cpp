//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_pipeline_interactions.cpp
// Purpose: Verify that optimizer pass combinations don't crash and produce
//          valid IL. Constructs IL manually, runs through various pass
//          combinations, and verifies the module remains structurally sound.
// Key invariants:
//   - Passes don't crash when composed in different orders.
//   - Module structure is preserved after optimization.
// Ownership/Lifetime: Transient modules.
// Links: il/transform/
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/ConstFold.hpp"
#include "il/transform/DCE.hpp"
#include "il/transform/EHOpt.hpp"
#include "il/transform/Reassociate.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

/// Build a simple module with redundant arithmetic for optimizer testing.
Module buildTestModule()
{
    Module mod;
    Function fn;
    fn.name = "test_fn";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;
    Param xParam;
    xParam.name = "x";
    xParam.type = Type(Type::Kind::I64);
    xParam.id = nextId++;
    fn.params.push_back(xParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[xParam.id] = "x";

    BasicBlock entry;
    entry.label = "entry";

    // %1 = add %x, 0 (identity — constfold target)
    Instr add0;
    add0.op = Opcode::Add;
    add0.result = nextId++;
    add0.operands.push_back(Value::temp(xParam.id));
    add0.operands.push_back(Value::constInt(0));
    entry.instructions.push_back(std::move(add0));

    // %2 = add 1, %1 (reassociate target: const first)
    Instr add1;
    add1.op = Opcode::Add;
    add1.result = nextId++;
    add1.operands.push_back(Value::constInt(1));
    add1.operands.push_back(Value::temp(nextId - 2));
    entry.instructions.push_back(std::move(add1));

    // ret %2
    Instr ret;
    ret.op = Opcode::Ret;
    ret.operands.push_back(Value::temp(nextId - 1));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    mod.functions.push_back(std::move(fn));
    return mod;
}

} // namespace

TEST(PipelineInteractions, ReassociateThenConstFold)
{
    Module mod = buildTestModule();
    il::transform::reassociate(mod);
    il::transform::constFold(mod);
    EXPECT_EQ(mod.functions.size(), 1U);
    EXPECT_FALSE(mod.functions[0].blocks.empty());
}

TEST(PipelineInteractions, ConstFoldThenReassociate)
{
    Module mod = buildTestModule();
    il::transform::constFold(mod);
    il::transform::reassociate(mod);
    EXPECT_EQ(mod.functions.size(), 1U);
    EXPECT_FALSE(mod.functions[0].blocks.empty());
}

TEST(PipelineInteractions, EHOptThenDCE)
{
    Module mod = buildTestModule();
    il::transform::ehOpt(mod);
    il::transform::dce(mod);
    EXPECT_EQ(mod.functions.size(), 1U);
    EXPECT_FALSE(mod.functions[0].blocks.empty());
}

TEST(PipelineInteractions, AllPassesSequential)
{
    Module mod = buildTestModule();
    il::transform::reassociate(mod);
    il::transform::constFold(mod);
    il::transform::ehOpt(mod);
    il::transform::dce(mod);
    EXPECT_EQ(mod.functions.size(), 1U);
    EXPECT_FALSE(mod.functions[0].blocks.empty());
}

TEST(PipelineInteractions, DCEThenConstFold)
{
    Module mod = buildTestModule();
    il::transform::dce(mod);
    il::transform::constFold(mod);
    EXPECT_EQ(mod.functions.size(), 1U);
    EXPECT_FALSE(mod.functions[0].blocks.empty());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
