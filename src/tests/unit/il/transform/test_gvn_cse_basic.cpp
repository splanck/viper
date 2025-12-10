//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_gvn_cse_basic.cpp
// Purpose: Lock in common expression identity between EarlyCSE and GVN,
//          including commutative normalisation and BasicAA-aware load reuse.
//
//===----------------------------------------------------------------------===//

#include "il/transform/EarlyCSE.hpp"
#include "il/transform/GVN.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/analysis/Liveness.hpp" // CFGInfo + buildCFG

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include "tests/unit/GTestStub.hpp"

using namespace il::core;

namespace
{

il::transform::AnalysisRegistry makeRegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fn) { return il::transform::buildCFG(mod, fn); });
    registry.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](Module &mod, Function &fn)
        {
            viper::analysis::CFGContext ctx(mod);
            return viper::analysis::computeDominatorTree(ctx, fn);
        });
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa", [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
    return registry;
}

void runGVN(Module &M, Function &F)
{
    il::transform::AnalysisRegistry registry = makeRegistry();
    il::transform::AnalysisManager manager(M, registry);
    il::transform::GVN gvn;
    gvn.run(F, manager);
}

} // namespace

TEST(GVNCSE, EarlyCSE_WithinBlock_Commutative)
{
    Module M;
    Function F;
    F.name = "cse_block";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;
    Param a{"a", Type(Type::Kind::I64), id++};
    Param b{"b", Type(Type::Kind::I64), id++};
    F.params = {a, b};
    F.valueNames.resize(id);

    BasicBlock entry;
    entry.label = "entry";

    Instr add1;
    add1.result = id++;
    add1.op = Opcode::Add;
    add1.type = Type(Type::Kind::I64);
    add1.operands = {Value::temp(a.id), Value::temp(b.id)};
    entry.instructions.push_back(std::move(add1));

    Instr add2;
    add2.result = id++;
    add2.op = Opcode::Add;
    add2.type = Type(Type::Kind::I64);
    add2.operands = {Value::temp(b.id), Value::temp(a.id)}; // commuted
    entry.instructions.push_back(std::move(add2));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(id - 1)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    F.blocks.push_back(std::move(entry));
    F.valueNames.resize(id);
    M.functions.push_back(std::move(F));

    il::transform::runEarlyCSE(M.functions.front());

    BasicBlock &B = M.functions.front().blocks.front();
    ASSERT_EQ(B.instructions.size(), 2U); // add + ret
    const Instr &keptAdd = B.instructions[0];
    ASSERT_TRUE(keptAdd.result.has_value());
    const unsigned keptId = *keptAdd.result;
    const Instr &finalRet = B.instructions.back();
    ASSERT_FALSE(finalRet.operands.empty());
    EXPECT_EQ(finalRet.operands[0].kind, Value::Kind::Temp);
    EXPECT_EQ(finalRet.operands[0].id, keptId);
}

TEST(GVNCSE, GVN_CommutativeAcrossBlocks)
{
    Module M;
    Function F;
    F.name = "gvn_dom";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;
    Param a{"a", Type(Type::Kind::I64), id++};
    Param b{"b", Type(Type::Kind::I64), id++};
    F.params = {a, b};
    F.valueNames.resize(id);

    BasicBlock entry;
    entry.label = "entry";
    Instr add1;
    add1.result = id++;
    add1.op = Opcode::Add;
    add1.type = Type(Type::Kind::I64);
    add1.operands = {Value::temp(a.id), Value::temp(b.id)};
    entry.instructions.push_back(std::move(add1));

    Instr br;
    br.op = Opcode::Br;
    br.type = Type(Type::Kind::Void);
    br.labels.push_back("next");
    br.brArgs.push_back(std::vector<Value>{});
    entry.instructions.push_back(std::move(br));
    entry.terminated = true;

    BasicBlock next;
    next.label = "next";
    Instr add2;
    add2.result = id++;
    add2.op = Opcode::Add;
    add2.type = Type(Type::Kind::I64);
    add2.operands = {Value::temp(b.id), Value::temp(a.id)};
    next.instructions.push_back(std::move(add2));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(id - 1)};
    next.instructions.push_back(std::move(ret));
    next.terminated = true;

    F.blocks.push_back(std::move(entry));
    F.blocks.push_back(std::move(next));
    F.valueNames.resize(id);
    M.functions.push_back(std::move(F));

    runGVN(M, M.functions.front());

    BasicBlock &entryBlock = M.functions.front().blocks[0];
    BasicBlock &nextBlock = M.functions.front().blocks[1];

    ASSERT_EQ(entryBlock.instructions.size(), 2U); // add1 + br
    ASSERT_EQ(nextBlock.instructions.size(), 1U);  // ret only (add2 removed)
    const Instr &retInstr = nextBlock.instructions.back();
    ASSERT_FALSE(retInstr.operands.empty());
    EXPECT_EQ(retInstr.operands[0].kind, Value::Kind::Temp);
    EXPECT_EQ(retInstr.operands[0].id, *entryBlock.instructions[0].result);
}

TEST(GVNCSE, GVN_LoadsRespectBasicAA_NoClobber)
{
    Module M;
    Function F;
    F.name = "gvn_loads";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    BasicBlock entry;
    entry.label = "entry";

    Instr alloca;
    alloca.result = id++;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    entry.instructions.push_back(std::move(alloca));

    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::Void);
    store.operands = {Value::temp(0), Value::constInt(7)};
    entry.instructions.push_back(std::move(store));

    Instr load1;
    load1.result = id++;
    load1.op = Opcode::Load;
    load1.type = Type(Type::Kind::I64);
    load1.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(load1));

    Instr load2;
    load2.result = id++;
    load2.op = Opcode::Load;
    load2.type = Type(Type::Kind::I64);
    load2.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(load2));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(id - 1)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    F.blocks.push_back(std::move(entry));
    F.valueNames.resize(id);
    M.functions.push_back(std::move(F));

    runGVN(M, M.functions.front());

    BasicBlock &B = M.functions.front().blocks.front();
    // alloca, store, load, ret
    ASSERT_EQ(B.instructions.size(), 4U);
    const Instr &retInstr = B.instructions.back();
    ASSERT_FALSE(retInstr.operands.empty());
    EXPECT_EQ(retInstr.operands[0].id, *B.instructions[2].result);
}

TEST(GVNCSE, GVN_LoadsClobberedByStore)
{
    Module M;
    Function F;
    F.name = "gvn_loads_clobber";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    BasicBlock entry;
    entry.label = "entry";

    Instr alloca;
    alloca.result = id++;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    entry.instructions.push_back(std::move(alloca));

    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::Void);
    store1.operands = {Value::temp(0), Value::constInt(1)};
    entry.instructions.push_back(std::move(store1));

    Instr load1;
    load1.result = id++;
    load1.op = Opcode::Load;
    load1.type = Type(Type::Kind::I64);
    load1.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(load1));

    Instr store2;
    store2.op = Opcode::Store;
    store2.type = Type(Type::Kind::Void);
    store2.operands = {Value::temp(0), Value::constInt(9)};
    entry.instructions.push_back(std::move(store2));

    Instr load2;
    load2.result = id++;
    load2.op = Opcode::Load;
    load2.type = Type(Type::Kind::I64);
    load2.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(load2));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands = {Value::temp(id - 1)};
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    F.blocks.push_back(std::move(entry));
    F.valueNames.resize(id);
    M.functions.push_back(std::move(F));

    runGVN(M, M.functions.front());

    BasicBlock &B = M.functions.front().blocks.front();
    // alloca, store1, load1, store2, load2, ret
    ASSERT_EQ(B.instructions.size(), 6U);
    const Instr &retInstr = B.instructions.back();
    ASSERT_FALSE(retInstr.operands.empty());
    EXPECT_EQ(retInstr.operands[0].id, *B.instructions[4].result);
}

#ifndef VIPER_HAS_GTEST
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
