//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// LateCleanup fixpoint behaviour and size tracking.
//
// Verifies the pass runs to a bounded fixpoint, records IL size per iteration,
// and stops once no further reductions are observed.
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/LateCleanup.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include "tests/TestHarness.hpp"
#include <iostream>

using namespace il::core;
using il::transform::LateCleanup;
using il::transform::LateCleanupStats;

namespace
{

LateCleanupStats runCleanup(il::core::Module &m)
{
    il::transform::AnalysisRegistry registry;
    il::transform::AnalysisManager am(m, registry);
    LateCleanup pass;
    LateCleanupStats stats;
    pass.setStats(&stats);
    pass.run(m, am);
    return stats;
}

void verifyOrDie(const Module &m)
{
    auto result = il::verify::Verifier::verify(m);
    if (!result)
    {
        il::support::printDiag(result.error(), std::cerr);
        ASSERT_TRUE(false && "module verification failed");
    }
}

} // namespace

TEST(IL, testSingleIterationNoChange)
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("noop", Type(Type::Kind::I64), {});
    b.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks.front();
    b.setInsertPoint(entry);

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    verifyOrDie(m);
    auto stats = runCleanup(m);
    ASSERT_EQ(stats.iterations, 1);
    ASSERT_EQ(stats.instrBefore, stats.instrAfter);
    ASSERT_EQ(stats.blocksBefore, stats.blocksAfter);
    ASSERT_EQ(stats.instrPerIter.size(), 1);
    ASSERT_EQ(stats.blocksPerIter.size(), 1);
}

TEST(IL, testTwoIterationsDCEOnly)
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("deadcode", Type(Type::Kind::I64), {});
    b.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks.front();
    b.setInsertPoint(entry);

    const unsigned slot = b.reserveTempId();
    Instr alloca;
    alloca.result = slot;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(alloca));

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::I64);
    store.operands = {Value::temp(slot), Value::constInt(7)};
    entry.instructions.push_back(std::move(store));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::constInt(0));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    verifyOrDie(m);
    auto stats = runCleanup(m);
    ASSERT_EQ(stats.iterations, 2);
    ASSERT_TRUE(stats.instrBefore > stats.instrAfter);
    ASSERT_EQ(stats.blocksBefore, stats.blocksAfter);
    ASSERT_EQ(stats.instrPerIter.size(), stats.iterations);
    for (size_t i = 1; i < stats.instrPerIter.size(); ++i)
    {
        ASSERT_TRUE(stats.instrPerIter[i] <= stats.instrPerIter[i - 1]);
        ASSERT_TRUE(stats.blocksPerIter[i] <= stats.blocksPerIter[i - 1]);
    }
    ASSERT_TRUE(stats.iterations <= 4);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
