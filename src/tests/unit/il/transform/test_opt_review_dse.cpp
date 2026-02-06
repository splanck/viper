//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for DSE fixes from the IL optimization review:
// - Backward loop uses size_t (no integer overflow)
// - Dead stores within a basic block are correctly eliminated
// - Live stores (read between writes) are preserved
//
//===----------------------------------------------------------------------===//

#include "il/transform/DSE.hpp"

#include "il/analysis/BasicAA.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

il::transform::AnalysisRegistry makeDSERegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa",
        [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
    return registry;
}

Instr makeAlloca(unsigned id, Type::Kind typeKind = Type::Kind::Ptr)
{
    Instr instr;
    instr.result = id;
    instr.op = Opcode::Alloca;
    instr.type = Type(typeKind);
    instr.operands.push_back(Value::constInt(8));
    return instr;
}

Instr makeStore(Value ptr, Value val, Type::Kind typeKind = Type::Kind::I64)
{
    Instr instr;
    instr.op = Opcode::Store;
    instr.type = Type(typeKind);
    instr.operands = {ptr, val};
    return instr;
}

Instr makeLoad(unsigned resultId, Value ptr, Type::Kind typeKind = Type::Kind::I64)
{
    Instr instr;
    instr.result = resultId;
    instr.op = Opcode::Load;
    instr.type = Type(typeKind);
    instr.operands.push_back(ptr);
    return instr;
}

} // namespace

// Test that consecutive stores to the same alloca eliminate the first
TEST(DSE, EliminatesDeadStoreIntraBlock)
{
    Module module;
    Function fn;
    fn.name = "dse_test";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    unsigned allocaId = 0;
    entry.instructions.push_back(makeAlloca(allocaId));

    // Store 1 (dead — overwritten by store 2)
    entry.instructions.push_back(makeStore(Value::temp(allocaId), Value::constInt(10)));

    // Store 2 (live — no subsequent overwrite)
    entry.instructions.push_back(makeStore(Value::temp(allocaId), Value::constInt(20)));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager manager(module, registry);

    bool changed = il::transform::runDSE(module.functions.front(), manager);

    EXPECT_TRUE(changed);

    // Count remaining stores
    size_t storeCount = 0;
    for (const auto &I : module.functions.front().blocks.front().instructions)
        if (I.op == Opcode::Store)
            ++storeCount;

    // Only one store should remain (the live one storing 20)
    EXPECT_EQ(storeCount, 1U);
}

// Test that a store followed by a load then store does NOT eliminate the first store
TEST(DSE, PreservesStoreBeforeLoad)
{
    Module module;
    Function fn;
    fn.name = "dse_preserve";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    unsigned allocaId = 0;
    unsigned loadId = 1;
    entry.instructions.push_back(makeAlloca(allocaId));

    // Store 1 (live — read by subsequent load)
    entry.instructions.push_back(makeStore(Value::temp(allocaId), Value::constInt(10)));

    // Load from same address
    entry.instructions.push_back(makeLoad(loadId, Value::temp(allocaId)));

    // Store 2
    entry.instructions.push_back(makeStore(Value::temp(allocaId), Value::constInt(20)));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(2);
    fn.valueNames[0] = "alloca";
    fn.valueNames[1] = "loaded";
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager manager(module, registry);

    il::transform::runDSE(module.functions.front(), manager);

    // Both stores should remain because the load intervenes
    size_t storeCount = 0;
    for (const auto &I : module.functions.front().blocks.front().instructions)
        if (I.op == Opcode::Store)
            ++storeCount;

    EXPECT_EQ(storeCount, 2U);
}

// Test that stores to different allocas are not eliminated
TEST(DSE, PreservesStoresToDifferentAllocas)
{
    Module module;
    Function fn;
    fn.name = "dse_different";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    unsigned allocaA = 0;
    unsigned allocaB = 1;
    entry.instructions.push_back(makeAlloca(allocaA));
    entry.instructions.push_back(makeAlloca(allocaB));

    // Store to A
    entry.instructions.push_back(makeStore(Value::temp(allocaA), Value::constInt(10)));

    // Store to B (different address, does NOT kill store to A)
    entry.instructions.push_back(makeStore(Value::temp(allocaB), Value::constInt(20)));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager manager(module, registry);

    bool changed = il::transform::runDSE(module.functions.front(), manager);

    // No stores should be eliminated (different addresses)
    EXPECT_FALSE(changed);

    size_t storeCount = 0;
    for (const auto &I : module.functions.front().blocks.front().instructions)
        if (I.op == Opcode::Store)
            ++storeCount;

    EXPECT_EQ(storeCount, 2U);
}

// Test with empty function (no crash on edge case)
TEST(DSE, EmptyFunctionNoCrash)
{
    Module module;
    Function fn;
    fn.name = "empty";
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

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager manager(module, registry);

    bool changed = il::transform::runDSE(module.functions.front(), manager);
    EXPECT_FALSE(changed);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
