//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests that DSE correctly preserves stores with side effects or later reads.
// Covers: escaping allocas via call args, escaping via stored address,
// interleaved loads, dead non-escaping stores, cross-block reads, and
// multiple allocas with mixed liveness.
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
        "basic-aa", [](Module &mod, Function &fn) { return viper::analysis::BasicAA(mod, fn); });
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

Instr makeRet(Value val)
{
    Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::I64);
    instr.operands.push_back(val);
    return instr;
}

Instr makeRetVoid()
{
    Instr instr;
    instr.op = Opcode::Ret;
    instr.type = Type(Type::Kind::Void);
    return instr;
}

Instr makeBr(const std::string &target)
{
    Instr instr;
    instr.op = Opcode::Br;
    instr.type = Type(Type::Kind::Void);
    instr.labels = {target};
    instr.brArgs = {{}};
    return instr;
}

size_t countStores(const Function &fn)
{
    size_t count = 0;
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == Opcode::Store)
                ++count;
    return count;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. StoreReadByCall
//    alloca %0 -> store(%0, 42) -> call @rt_print_i64(%0) -> ret 0
//    The alloca escapes via the call argument, so the store must be preserved.
// ---------------------------------------------------------------------------
TEST(DSESideEffects, StoreReadByCall)
{
    Module module;

    Extern ext;
    ext.name = "rt_print_i64";
    ext.retType = Type(Type::Kind::Void);
    ext.params = {Type(Type::Kind::I64)};
    module.externs.push_back(ext);

    Function fn;
    fn.name = "store_read_by_call";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    entry.instructions.push_back(makeAlloca(0));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(42)));

    Instr call;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::Void);
    call.callee = "rt_print_i64";
    call.operands = {Value::temp(0)};
    entry.instructions.push_back(std::move(call));

    entry.instructions.push_back(makeRet(Value::constInt(0)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);
    il::transform::runDSE(module.functions.front(), am);

    // Store must survive: alloca escapes via call argument
    EXPECT_EQ(countStores(module.functions.front()), 1U);
}

// ---------------------------------------------------------------------------
// 2. StoreToEscapedAlloca
//    alloca A (%0), alloca B (%1) -> store(B, A) -> store(A, 42) -> ret 0
//    A's address escapes because it is stored into B. Store to A is preserved.
// ---------------------------------------------------------------------------
TEST(DSESideEffects, StoreToEscapedAlloca)
{
    Module module;
    Function fn;
    fn.name = "store_escaped_alloca";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    entry.instructions.push_back(makeAlloca(0)); // A
    entry.instructions.push_back(makeAlloca(1)); // B

    // Store A's address into B — A escapes
    entry.instructions.push_back(makeStore(Value::temp(1), Value::temp(0), Type::Kind::Ptr));

    // Store a value into A
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(42)));

    entry.instructions.push_back(makeRet(Value::constInt(0)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);
    il::transform::runDSE(module.functions.front(), am);

    // Both stores must survive: store to B escapes A, store to A is to escaped alloca
    EXPECT_EQ(countStores(module.functions.front()), 2U);
}

// ---------------------------------------------------------------------------
// 3. StoreThenLoadThenStore
//    alloca %0 -> store(%0, 10) -> load %1 from %0 -> store(%0, 20) -> ret %1
//    First store PRESERVED because the load reads it before the overwrite.
// ---------------------------------------------------------------------------
TEST(DSESideEffects, StoreThenLoadThenStore)
{
    Module module;
    Function fn;
    fn.name = "store_load_store";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    entry.instructions.push_back(makeAlloca(0));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(10)));
    entry.instructions.push_back(makeLoad(1, Value::temp(0)));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(20)));
    entry.instructions.push_back(makeRet(Value::temp(1)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);
    il::transform::runDSE(module.functions.front(), am);

    // Both stores survive: load intervenes between them
    EXPECT_EQ(countStores(module.functions.front()), 2U);
}

// ---------------------------------------------------------------------------
// 4. DeadStoreNonEscaping
//    entry: alloca %0, store(%0, 10), br exit
//    exit:  ret 0
//    Store ELIMINATED by cross-block DSE: value is never read on any path
//    and the alloca does not escape. Uses two blocks so that the cross-block
//    dataflow has a successor to visit (single-block functions have no
//    successors, making `visited.size() > 0` false in the cross-block pass).
// ---------------------------------------------------------------------------
TEST(DSESideEffects, DeadStoreNonEscaping)
{
    Module module;
    Function fn;
    fn.name = "dead_store_non_escaping";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(makeAlloca(0));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(10)));
    entry.instructions.push_back(makeBr("exit"));
    entry.terminated = true;

    BasicBlock exit;
    exit.label = "exit";
    exit.instructions.push_back(makeRet(Value::constInt(0)));
    exit.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(exit));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);

    // Run both intra-block and cross-block DSE to cover the dead store
    il::transform::runDSE(module.functions.front(), am);
    il::transform::runCrossBlockDSE(module.functions.front(), am);

    // Store should be eliminated: never read on any path, alloca doesn't escape
    EXPECT_EQ(countStores(module.functions.front()), 0U);
}

// ---------------------------------------------------------------------------
// 5. TwoStoresSameAddr
//    alloca %0 -> store(%0, 10) -> store(%0, 20) -> load %1 from %0 -> ret %1
//    First store ELIMINATED (overwritten before read), second preserved.
// ---------------------------------------------------------------------------
TEST(DSESideEffects, TwoStoresSameAddr)
{
    Module module;
    Function fn;
    fn.name = "two_stores_same_addr";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    entry.instructions.push_back(makeAlloca(0));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(10)));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(20)));
    entry.instructions.push_back(makeLoad(1, Value::temp(0)));
    entry.instructions.push_back(makeRet(Value::temp(1)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);

    bool changed = il::transform::runDSE(module.functions.front(), am);

    EXPECT_TRUE(changed);
    // Only one store remains (the second one storing 20)
    EXPECT_EQ(countStores(module.functions.front()), 1U);
}

// ---------------------------------------------------------------------------
// 6. CrossBlockRead
//    entry: alloca %0, store(%0, 10), br succ
//    succ:  load %1 from %0, ret %1
//    Store PRESERVED because the load in the successor reads it.
//    (Intra-block DSE won't see the cross-block load, so the store survives.)
// ---------------------------------------------------------------------------
TEST(DSESideEffects, CrossBlockRead)
{
    Module module;
    Function fn;
    fn.name = "cross_block_read";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(makeAlloca(0));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(10)));
    entry.instructions.push_back(makeBr("succ"));
    entry.terminated = true;

    BasicBlock succ;
    succ.label = "succ";
    succ.instructions.push_back(makeLoad(1, Value::temp(0)));
    succ.instructions.push_back(makeRet(Value::temp(1)));
    succ.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(succ));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);

    // Run both intra-block and cross-block DSE
    il::transform::runDSE(module.functions.front(), am);
    il::transform::runCrossBlockDSE(module.functions.front(), am);

    // Store must survive: successor block reads it
    EXPECT_EQ(countStores(module.functions.front()), 1U);
}

// ---------------------------------------------------------------------------
// 7. StoreLoadDifferentAllocas
//    entry: alloca A (%0), alloca B (%1), store(A, 10), store(B, 20), br read
//    read:  load %2 from A, ret %2
//    Store to A PRESERVED (loaded in successor).
//    Store to B ELIMINATED by cross-block DSE (never read, doesn't escape).
//    Uses two blocks so the cross-block dataflow has a successor to visit.
// ---------------------------------------------------------------------------
TEST(DSESideEffects, StoreLoadDifferentAllocas)
{
    Module module;
    Function fn;
    fn.name = "store_load_diff_allocas";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    entry.instructions.push_back(makeAlloca(0));                                  // A
    entry.instructions.push_back(makeAlloca(1));                                  // B
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(10))); // store to A
    entry.instructions.push_back(makeStore(Value::temp(1), Value::constInt(20))); // store to B
    entry.instructions.push_back(makeBr("read"));
    entry.terminated = true;

    BasicBlock read;
    read.label = "read";
    read.instructions.push_back(makeLoad(2, Value::temp(0))); // load from A
    read.instructions.push_back(makeRet(Value::temp(2)));
    read.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(read));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);

    // Run both passes to eliminate the dead store to B
    il::transform::runDSE(module.functions.front(), am);
    il::transform::runCrossBlockDSE(module.functions.front(), am);

    // Store to A preserved (loaded in successor), store to B eliminated (never read)
    EXPECT_EQ(countStores(module.functions.front()), 1U);

    // Verify the surviving store is to alloca A (%0)
    for (const auto &bb : module.functions.front().blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == Opcode::Store)
            {
                EXPECT_EQ(instr.operands[0].kind, Value::Kind::Temp);
                EXPECT_EQ(instr.operands[0].id, 0U); // alloca A
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 8. MultipleStoresWithInterleavedLoad
//    alloca %0 -> store(%0, 10) -> load %1 -> store(%0, 20) -> load %2 -> ret %2
//    Both stores PRESERVED because each is read by a subsequent load.
// ---------------------------------------------------------------------------
TEST(DSESideEffects, MultipleStoresWithInterleavedLoad)
{
    Module module;
    Function fn;
    fn.name = "multi_store_interleaved_load";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    entry.instructions.push_back(makeAlloca(0));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(10)));
    entry.instructions.push_back(makeLoad(1, Value::temp(0)));
    entry.instructions.push_back(makeStore(Value::temp(0), Value::constInt(20)));
    entry.instructions.push_back(makeLoad(2, Value::temp(0)));
    entry.instructions.push_back(makeRet(Value::temp(2)));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    module.functions.push_back(std::move(fn));

    auto registry = makeDSERegistry();
    il::transform::AnalysisManager am(module, registry);
    il::transform::runDSE(module.functions.front(), am);

    // Both stores survive: each is read by a following load
    EXPECT_EQ(countStores(module.functions.front()), 2U);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
