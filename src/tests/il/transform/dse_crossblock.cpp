//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/dse_crossblock.cpp
// Purpose: Tests for dead store elimination.
// Key invariants: Stores to non-escaping allocas that are overwritten
//                 before being read should be eliminated.
// Ownership/Lifetime: Builds transient modules per test invocation.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/core/Module.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/DSE.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <iostream>

using namespace il::core;

namespace
{

static void verifyOrDie(const Module &module)
{
    auto verifyResult = il::verify::Verifier::verify(module);
    if (!verifyResult)
    {
        il::support::printDiag(verifyResult.error(), std::cerr);
        assert(false && "Module verification failed");
    }
}

void setupAnalysisRegistry(il::transform::AnalysisRegistry &registry)
{
    registry.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg", [](Module &mod, Function &fnRef) { return il::transform::buildCFG(mod, fnRef); });
    registry.registerFunctionAnalysis<viper::analysis::DomTree>(
        "dominators",
        [](Module &mod, Function &fnRef)
        {
            viper::analysis::CFGContext ctx(mod);
            return viper::analysis::computeDominatorTree(ctx, fnRef);
        });
    registry.registerFunctionAnalysis<il::transform::LivenessInfo>(
        "liveness",
        [](Module &mod, Function &fnRef) { return il::transform::computeLiveness(mod, fnRef); });
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa",
        [](Module &mod, Function &fnRef) { return viper::analysis::BasicAA(mod, fnRef); });
}

size_t countStores(const Function &fn)
{
    size_t count = 0;
    for (const auto &block : fn.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.op == Opcode::Store)
                ++count;
        }
    }
    return count;
}

/// @brief Test that intra-block dead stores are eliminated.
/// store ptr, 1; store ptr, 2 -> store ptr, 2
void testIntraBlockDSE()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_intra", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %ptr = alloca 8
    unsigned ptrId = builder.reserveTempId();
    Instr alloca;
    alloca.result = ptrId;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(8)); // size
    entry.instructions.push_back(std::move(alloca));

    // store %ptr, 1 (dead store)
    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands.push_back(Value::temp(ptrId));
    store1.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(std::move(store1));

    // store %ptr, 2 (overwrites)
    Instr store2;
    store2.op = Opcode::Store;
    store2.type = Type(Type::Kind::I64);
    store2.operands.push_back(Value::temp(ptrId));
    store2.operands.push_back(Value::constInt(2));
    entry.instructions.push_back(std::move(store2));

    builder.emitRet(std::nullopt, {});

    verifyOrDie(module);

    size_t storesBefore = countStores(fn);
    assert(storesBefore == 2);

    il::transform::AnalysisRegistry registry;
    setupAnalysisRegistry(registry);
    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::runDSE(fn, analysisManager);

    verifyOrDie(module);

    size_t storesAfter = countStores(fn);
    // DSE should remove the first store
    assert(storesAfter == 1 && "First dead store should be eliminated");
}

/// @brief Test that stores read before being overwritten are NOT eliminated.
void testStoreReadBeforeOverwrite()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_no_dse", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %ptr = alloca 8
    unsigned ptrId = builder.reserveTempId();
    Instr alloca;
    alloca.result = ptrId;
    alloca.op = Opcode::Alloca;
    alloca.type = Type(Type::Kind::Ptr);
    alloca.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(alloca));

    // store %ptr, 42
    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands.push_back(Value::temp(ptrId));
    store1.operands.push_back(Value::constInt(42));
    entry.instructions.push_back(std::move(store1));

    // %val = load %ptr (reads the store)
    unsigned valId = builder.reserveTempId();
    Instr load;
    load.result = valId;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(ptrId));
    entry.instructions.push_back(std::move(load));

    // store %ptr, 100 (overwrites but after read)
    Instr store2;
    store2.op = Opcode::Store;
    store2.type = Type(Type::Kind::I64);
    store2.operands.push_back(Value::temp(ptrId));
    store2.operands.push_back(Value::constInt(100));
    entry.instructions.push_back(std::move(store2));

    builder.emitRet(Value::temp(valId), {});

    verifyOrDie(module);

    size_t storesBefore = countStores(fn);
    assert(storesBefore == 2);

    il::transform::AnalysisRegistry registry;
    setupAnalysisRegistry(registry);
    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::runDSE(fn, analysisManager);

    verifyOrDie(module);

    size_t storesAfter = countStores(fn);
    // First store should NOT be eliminated (it's read before overwrite)
    // Second store should NOT be eliminated (it's the last store)
    assert(storesAfter == 2 && "No stores should be eliminated when read occurs");
}

/// @brief Test that stores to different locations are not confused.
void testDifferentLocations()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("test_diff_loc", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];
    builder.setInsertPoint(entry);

    // %ptr1 = alloca 8
    unsigned ptr1Id = builder.reserveTempId();
    Instr alloca1;
    alloca1.result = ptr1Id;
    alloca1.op = Opcode::Alloca;
    alloca1.type = Type(Type::Kind::Ptr);
    alloca1.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(alloca1));

    // %ptr2 = alloca 8
    unsigned ptr2Id = builder.reserveTempId();
    Instr alloca2;
    alloca2.result = ptr2Id;
    alloca2.op = Opcode::Alloca;
    alloca2.type = Type(Type::Kind::Ptr);
    alloca2.operands.push_back(Value::constInt(8));
    entry.instructions.push_back(std::move(alloca2));

    // store %ptr1, 1
    Instr store1;
    store1.op = Opcode::Store;
    store1.type = Type(Type::Kind::I64);
    store1.operands.push_back(Value::temp(ptr1Id));
    store1.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(std::move(store1));

    // store %ptr2, 2 (different location, should not kill store to ptr1)
    Instr store2;
    store2.op = Opcode::Store;
    store2.type = Type(Type::Kind::I64);
    store2.operands.push_back(Value::temp(ptr2Id));
    store2.operands.push_back(Value::constInt(2));
    entry.instructions.push_back(std::move(store2));

    builder.emitRet(std::nullopt, {});

    verifyOrDie(module);

    size_t storesBefore = countStores(fn);
    assert(storesBefore == 2);

    il::transform::AnalysisRegistry registry;
    setupAnalysisRegistry(registry);
    il::transform::AnalysisManager analysisManager(module, registry);

    il::transform::runDSE(fn, analysisManager);

    verifyOrDie(module);

    size_t storesAfter = countStores(fn);
    // Both stores should remain (different locations)
    assert(storesAfter == 2 && "Stores to different locations should not be eliminated");
}

} // namespace

int main()
{
    testIntraBlockDSE();
    testStoreReadBeforeOverwrite();
    testDifferentLocations();
    return 0;
}
