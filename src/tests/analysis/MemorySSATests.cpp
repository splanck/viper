//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/analysis/MemorySSATests.cpp
// Purpose: Validate MemorySSA analysis and its dead-store precision improvement
//          over the conservative BFS-based cross-block DSE.
//
// Key invariants tested:
//   1. Dead stores to non-escaping allocas are detected even when calls appear
//      in successor blocks (calls are transparent for non-escaping allocas).
//   2. Live stores (with an intervening load) are NOT eliminated.
//   3. Simple cross-block dead stores (no calls) are also eliminated.
//   4. Stores to escaping allocas are conservatively preserved.
//
// The distinction from runCrossBlockDSE:
//   - runCrossBlockDSE calls blockReadsFrom() which returns true for any
//     ModRef call, blocking elimination even when the alloca is non-escaping.
//   - runMemorySSADSE uses MemorySSA which skips calls for non-escaping
//     allocas, giving a more precise dead-store answer.
//
// Ownership/Lifetime: Builds local modules via IRBuilder per test.
// Links: il/analysis/MemorySSA.hpp, il/transform/DSE.hpp
//
//===----------------------------------------------------------------------===//

#include "il/analysis/BasicAA.hpp"
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/analysis/MemorySSA.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "il/transform/AnalysisManager.hpp"
#include "il/transform/DSE.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"
#include "tests/TestHarness.hpp"

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
        ASSERT_TRUE(false && "Module verification failed");
    }
}

/// Build an AnalysisRegistry wired with BasicAA and MemorySSA.
il::transform::AnalysisRegistry makeRegistry()
{
    il::transform::AnalysisRegistry registry;
    registry.registerFunctionAnalysis<viper::analysis::BasicAA>(
        "basic-aa",
        [](Module &mod, Function &fnRef) { return viper::analysis::BasicAA(mod, fnRef); });
    registry.registerFunctionAnalysis<viper::analysis::MemorySSA>(
        "memory-ssa",
        [](Module &mod, Function &fnRef)
        {
            viper::analysis::BasicAA aa(mod, fnRef);
            return viper::analysis::computeMemorySSA(fnRef, aa);
        });
    return registry;
}

size_t countStores(const Function &fn)
{
    size_t count = 0;
    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            if (instr.op == Opcode::Store)
                ++count;
    return count;
}

// Helper: insert a Store instruction at the end of a block.
void addStore(BasicBlock &block, unsigned ptrId, int64_t val, Type::Kind kind = Type::Kind::I64)
{
    Instr store;
    store.op = Opcode::Store;
    store.type = Type(kind);
    store.operands.push_back(Value::temp(ptrId));
    store.operands.push_back(Value::constInt(val));
    block.instructions.push_back(std::move(store));
}

// Helper: insert a Load instruction at the end of a block.
void addLoad(BasicBlock &block, unsigned resultId, unsigned ptrId, Type::Kind kind = Type::Kind::I64)
{
    Instr load;
    load.result = resultId;
    load.op = Opcode::Load;
    load.type = Type(kind);
    load.operands.push_back(Value::temp(ptrId));
    block.instructions.push_back(std::move(load));
}

// Helper: insert an external call (not in module or runtime registry → ModRef).
void addCall(BasicBlock &block, std::string callee)
{
    Instr call;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::Void);
    call.callee = std::move(callee);
    block.instructions.push_back(std::move(call));
}

} // namespace

// -------------------------------------------------------------------------
// Test 1: Dead store with call barrier in successor block.
//
// This is the KEY precision improvement of MemorySSA over the old BFS:
//
//   entry:
//     %ptr = alloca 8
//     store %ptr, 1        ← dead store (overwritten before any read)
//     br has_call
//   has_call:
//     call "external"()    ← old BFS: read barrier (conservative ModRef)
//                             MemorySSA: transparent (non-escaping alloca)
//     br exit
//   exit:
//     store %ptr, 2        ← kills first store
//     ret
//
// The old runCrossBlockDSE would NOT eliminate `store %ptr, 1` because
// blockReadsFrom() returns true for the ModRef call.
// runMemorySSADSE correctly eliminates it.
// -------------------------------------------------------------------------
TEST(MemorySSA, EliminatesDeadStoreWithCallBarrier)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("call_barrier", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "has_call");
    builder.createBlock(fn, "exit");

    BasicBlock &entry    = fn.blocks[0];
    BasicBlock &hasCall  = fn.blocks[1];
    BasicBlock &exitBlk  = fn.blocks[2];

    // entry: alloca + dead store + branch
    unsigned ptrId = builder.reserveTempId();
    {
        Instr alloca;
        alloca.result = ptrId;
        alloca.op = Opcode::Alloca;
        alloca.type = Type(Type::Kind::Ptr);
        alloca.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(alloca));
    }
    addStore(entry, ptrId, 1); // dead — overwritten before any read

    builder.setInsertPoint(entry);
    builder.br(hasCall, {});

    // has_call: external call + branch
    // The call is ModRef but the alloca doesn't escape → call cannot read %ptr.
    addCall(hasCall, "external_runtime_fn");

    builder.setInsertPoint(hasCall);
    builder.br(exitBlk, {});

    // exit: overwriting store + ret
    addStore(exitBlk, ptrId, 2); // kills the first store
    builder.setInsertPoint(exitBlk);
    builder.emitRet(std::nullopt, {});

    // Declare the external function so the verifier accepts the call.
    {
        Extern ext;
        ext.name = "external_runtime_fn";
        ext.retType = Type(Type::Kind::Void);
        module.externs.push_back(std::move(ext));
    }

    verifyOrDie(module);

    size_t storesBefore = countStores(fn);
    ASSERT_EQ(storesBefore, 2U);

    auto registry = makeRegistry();
    il::transform::AnalysisManager am(module, registry);

    bool changed = il::transform::runMemorySSADSE(fn, am);

    verifyOrDie(module);

    EXPECT_TRUE(changed);
    size_t storesAfter = countStores(fn);
    // First store (dead) should be eliminated; second (live) preserved.
    EXPECT_EQ(storesAfter, 1U);
}

// -------------------------------------------------------------------------
// Test 2: Live store preserved when a load intervenes.
//
//   entry:
//     %ptr = alloca 8
//     store %ptr, 42       ← live (read by load below)
//     br read_it
//   read_it:
//     %v = load %ptr       ← reads the first store
//     store %ptr, 100      ← live (last store before ret)
//     ret
//
// Neither store should be eliminated.
// -------------------------------------------------------------------------
TEST(MemorySSA, PreservesLiveStoreWithInterveningLoad)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("live_store", Type(Type::Kind::I64), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "read_it");

    BasicBlock &entry   = fn.blocks[0];
    BasicBlock &readIt  = fn.blocks[1];

    unsigned ptrId = builder.reserveTempId();
    {
        Instr alloca;
        alloca.result = ptrId;
        alloca.op = Opcode::Alloca;
        alloca.type = Type(Type::Kind::Ptr);
        alloca.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(alloca));
    }
    addStore(entry, ptrId, 42); // live — read in read_it

    builder.setInsertPoint(entry);
    builder.br(readIt, {});

    unsigned valId = builder.reserveTempId();
    addLoad(readIt, valId, ptrId);          // reads first store
    addStore(readIt, ptrId, 100);           // second store
    builder.setInsertPoint(readIt);
    builder.emitRet(Value::temp(valId), {});

    verifyOrDie(module);

    size_t storesBefore = countStores(fn);
    ASSERT_EQ(storesBefore, 2U);

    auto registry = makeRegistry();
    il::transform::AnalysisManager am(module, registry);

    bool changed = il::transform::runMemorySSADSE(fn, am);

    verifyOrDie(module);

    // Neither store should have been eliminated.
    EXPECT_FALSE(changed);
    EXPECT_EQ(countStores(fn), 2U);
}

// -------------------------------------------------------------------------
// Test 3: Simple cross-block dead store (no calls — baseline correctness).
//
//   entry:
//     %ptr = alloca 8
//     store %ptr, 1        ← dead — overwritten before read
//     br exit
//   exit:
//     store %ptr, 2        ← kills first store
//     ret
// -------------------------------------------------------------------------
TEST(MemorySSA, EliminatesSimpleCrossBlockDeadStore)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("simple_crossblock", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "exit");

    BasicBlock &entry   = fn.blocks[0];
    BasicBlock &exitBlk = fn.blocks[1];

    unsigned ptrId = builder.reserveTempId();
    {
        Instr alloca;
        alloca.result = ptrId;
        alloca.op = Opcode::Alloca;
        alloca.type = Type(Type::Kind::Ptr);
        alloca.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(alloca));
    }
    addStore(entry, ptrId, 1); // dead

    builder.setInsertPoint(entry);
    builder.br(exitBlk, {});

    addStore(exitBlk, ptrId, 2); // kills first store
    builder.setInsertPoint(exitBlk);
    builder.emitRet(std::nullopt, {});

    verifyOrDie(module);

    ASSERT_EQ(countStores(fn), 2U);

    auto registry = makeRegistry();
    il::transform::AnalysisManager am(module, registry);

    bool changed = il::transform::runMemorySSADSE(fn, am);

    verifyOrDie(module);
    EXPECT_TRUE(changed);
    EXPECT_EQ(countStores(fn), 1U);
}

// -------------------------------------------------------------------------
// Test 4: Stores to an ESCAPING alloca must NOT be eliminated.
//
//   entry:
//     %ptr = alloca 8
//     store %ptr, 1        ← possibly live (ptr might escape through call)
//     call "sink"(%ptr)    ← ptr escapes here
//     ret
//
// Since %ptr escapes, the store might be observed by the call's callee.
// MemorySSA must conservatively preserve it.
// -------------------------------------------------------------------------
TEST(MemorySSA, PreservesStoreToEscapingAlloca)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("escaping", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];

    unsigned ptrId = builder.reserveTempId();
    {
        Instr alloca;
        alloca.result = ptrId;
        alloca.op = Opcode::Alloca;
        alloca.type = Type(Type::Kind::Ptr);
        alloca.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(alloca));
    }
    addStore(entry, ptrId, 99); // potentially live — ptr escapes below

    // Declare "sink" as an external function that takes a Ptr argument.
    {
        Extern ext;
        ext.name = "sink";
        ext.retType = Type(Type::Kind::Void);
        ext.params.push_back(Type(Type::Kind::Ptr));
        module.externs.push_back(std::move(ext));
    }

    // Call with %ptr as an argument — this causes the alloca to "escape".
    {
        Instr call;
        call.op = Opcode::Call;
        call.type = Type(Type::Kind::Void);
        call.callee = "sink";
        call.operands.push_back(Value::temp(ptrId)); // %ptr passed to call → escapes
        entry.instructions.push_back(std::move(call));
    }

    builder.setInsertPoint(entry);
    builder.emitRet(std::nullopt, {});

    verifyOrDie(module);

    ASSERT_EQ(countStores(fn), 1U);

    auto registry = makeRegistry();
    il::transform::AnalysisManager am(module, registry);

    bool changed = il::transform::runMemorySSADSE(fn, am);

    verifyOrDie(module);
    // Store must be preserved — alloca escapes through the call.
    EXPECT_FALSE(changed);
    EXPECT_EQ(countStores(fn), 1U);
}

// -------------------------------------------------------------------------
// Test 5: MemorySSA accessFor() query — verify def-use nodes are built.
//
// Directly inspects the MemorySSA result to check that Store instructions
// produce MemoryDef nodes and Load instructions produce MemoryUse nodes.
// -------------------------------------------------------------------------
TEST(MemorySSA, AssignsDefAndUseNodes)
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("def_use", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    BasicBlock &entry = fn.blocks[0];

    unsigned ptrId = builder.reserveTempId();
    unsigned valId = builder.reserveTempId();
    {
        Instr alloca;
        alloca.result = ptrId;
        alloca.op = Opcode::Alloca;
        alloca.type = Type(Type::Kind::Ptr);
        alloca.operands.push_back(Value::constInt(8));
        entry.instructions.push_back(std::move(alloca));
    }

    addStore(entry, ptrId, 7);         // index 1 → MemoryDef
    addLoad(entry, valId, ptrId);      // index 2 → MemoryUse
    addStore(entry, ptrId, 8);         // index 3 → MemoryDef

    builder.setInsertPoint(entry);
    builder.emitRet(std::nullopt, {});

    verifyOrDie(module);

    viper::analysis::BasicAA aa(module, fn);
    viper::analysis::MemorySSA mssa = viper::analysis::computeMemorySSA(fn, aa);

    // instrIdx=1 is the first Store → should be a Def
    const viper::analysis::MemoryAccess *def1 = mssa.accessFor(&entry, 1);
    ASSERT_TRUE(def1 != nullptr);
    EXPECT_EQ(def1->kind, viper::analysis::MemAccessKind::Def);

    // instrIdx=2 is the Load → should be a Use
    const viper::analysis::MemoryAccess *use = mssa.accessFor(&entry, 2);
    ASSERT_TRUE(use != nullptr);
    EXPECT_EQ(use->kind, viper::analysis::MemAccessKind::Use);

    // instrIdx=3 is the second Store → MemoryDef
    const viper::analysis::MemoryAccess *def2 = mssa.accessFor(&entry, 3);
    ASSERT_TRUE(def2 != nullptr);
    EXPECT_EQ(def2->kind, viper::analysis::MemAccessKind::Def);

    // First store (def1) should NOT be dead because the load reads it.
    EXPECT_FALSE(mssa.isDeadStore(&entry, 1));
    // Second store (def2) IS dead — no load reads it and function exits.
    // (Intra-block check: no subsequent reads in entry, no successors → dead-on-exit)
    // NOTE: MemorySSA marks this dead since no load reaches it before exit.
    // Whether it is actually eliminated is a function of the dead-exit heuristic.
    // The important thing is def1 is NOT dead (has a Use).
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
