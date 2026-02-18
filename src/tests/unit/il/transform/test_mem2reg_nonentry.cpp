//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the extended Mem2Reg pass — specifically the removal of the
// entry-block-only restriction.  An alloca in a non-entry block is now
// promotable when its defining block dominates all blocks containing uses.
//
// Test cases:
//   1. Single-block alloca in non-entry block — always promotable.
//   2. Multi-block alloca where defining block dominates all uses — promotable.
//   3. Multi-block alloca where defining block does NOT dominate a use — not promoted.
//
//===----------------------------------------------------------------------===//

#include "il/transform/Mem2Reg.hpp"

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

unsigned countOpcodeInFunction(const Function &fn, Opcode op)
{
    unsigned count = 0;
    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            if (instr.op == op)
                ++count;
    return count;
}

/// Build a function with an alloca in a non-entry block (single-block use).
///
///   fn single_block_nonentry() -> i64:
///     entry:
///       br middle
///     middle:
///       t0 = alloca 8         ; alloca in non-entry block
///       store i64 42, t0
///       t1 = load i64, t0    ; use in same block -> singleBlock=true
///       ret t1
Module buildSingleBlockNonEntryAlloca()
{
    Module module;
    Function fn;
    fn.name = "single_block_nonentry";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    // entry block
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("middle");
        br.brArgs.push_back({});
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    // middle block — alloca + store + load + ret, all in same block
    BasicBlock middle;
    middle.label = "middle";
    {
        Instr alloca_;
        alloca_.result = id++;
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands.push_back(Value::constInt(8));
        middle.instructions.push_back(std::move(alloca_)); // t0

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands.push_back(Value::temp(0)); // ptr t0
        store.operands.push_back(Value::constInt(42));
        middle.instructions.push_back(std::move(store));

        Instr load;
        load.result = id++;
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands.push_back(Value::temp(0));        // ptr t0
        middle.instructions.push_back(std::move(load)); // t1

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(1));
        middle.instructions.push_back(std::move(ret));
        middle.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(middle));
    fn.valueNames.resize(id);
    fn.valueNames[0] = "ptr";
    fn.valueNames[1] = "val";
    module.functions.push_back(std::move(fn));
    return module;
}

/// Build a function where an if-branch alloca dominates a successor's use.
///
///   fn dominating_nonentry() -> i64:
///     entry:
///       cbr 1, then, else
///     then:
///       t0 = alloca 8
///       store i64 7, t0
///       t1 = load i64, t0
///       br merge, t1
///     else:
///       br merge, i64 0
///     merge(x: i64):
///       ret x
///
/// The alloca is in "then" which dominates only the then→merge path — but all
/// uses of t0 are in "then", so singleBlock=true → promoted.
Module buildDominatingNonEntryAlloca()
{
    Module module;
    Function fn;
    fn.name = "dominating_nonentry";
    fn.retType = Type(Type::Kind::I64);

    unsigned id = 0;

    // entry: unconditionally enter "then" for simplicity (cbr cond=1)
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.type = Type(Type::Kind::Void);
        cbr.operands.push_back(Value::constInt(1));
        cbr.labels.push_back("then");
        cbr.labels.push_back("els");
        cbr.brArgs.push_back({});
        cbr.brArgs.push_back({});
        entry.instructions.push_back(std::move(cbr));
        entry.terminated = true;
    }

    // then: alloca + store + load → br merge
    BasicBlock then_;
    then_.label = "then";
    {
        Instr alloca_;
        alloca_.result = id++; // t0
        alloca_.op = Opcode::Alloca;
        alloca_.type = Type(Type::Kind::Ptr);
        alloca_.operands.push_back(Value::constInt(8));
        then_.instructions.push_back(std::move(alloca_));

        Instr store;
        store.op = Opcode::Store;
        store.type = Type(Type::Kind::I64);
        store.operands.push_back(Value::temp(0));
        store.operands.push_back(Value::constInt(7));
        then_.instructions.push_back(std::move(store));

        Instr load;
        load.result = id++; // t1
        load.op = Opcode::Load;
        load.type = Type(Type::Kind::I64);
        load.operands.push_back(Value::temp(0));
        then_.instructions.push_back(std::move(load));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("merge");
        br.brArgs.push_back({Value::temp(1)});
        then_.instructions.push_back(std::move(br));
        then_.terminated = true;
    }

    // else: br merge with constant 0
    BasicBlock else_;
    else_.label = "els";
    {
        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("merge");
        br.brArgs.push_back({Value::constInt(0)});
        else_.instructions.push_back(std::move(br));
        else_.terminated = true;
    }

    // merge(x: i64): ret x
    BasicBlock merge;
    merge.label = "merge";
    {
        Param p;
        p.id = id++; // t2
        p.type = Type(Type::Kind::I64);
        p.name = "x";
        merge.params.push_back(std::move(p));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(2));
        merge.instructions.push_back(std::move(ret));
        merge.terminated = true;
    }

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(then_));
    fn.blocks.push_back(std::move(else_));
    fn.blocks.push_back(std::move(merge));
    fn.valueNames.resize(id);
    fn.valueNames[0] = "ptr";
    fn.valueNames[1] = "val";
    fn.valueNames[2] = "x";
    module.functions.push_back(std::move(fn));
    return module;
}

} // namespace

// A single-block alloca in a non-entry block must be promoted.
// Previously this would be silently skipped (entry-block-only restriction).
TEST(Mem2RegNonEntry, SingleBlockNonEntryAllocaIsPromoted)
{
    Module module = buildSingleBlockNonEntryAlloca();
    ASSERT_FALSE(module.functions.empty());

    // Before: 1 alloca, 1 store, 1 load
    unsigned allocasBefore = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    ASSERT_EQ(allocasBefore, 1u);

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    // After: alloca and load must be removed (promoted to SSA)
    unsigned allocasAfter = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    unsigned loadsAfter = countOpcodeInFunction(module.functions[0], Opcode::Load);
    EXPECT_EQ(allocasAfter, 0u);
    EXPECT_EQ(loadsAfter, 0u);
}

// A non-entry-block alloca where all uses are in the same block (singleBlock=true)
// and the module has a conditional branch — must be promoted.
TEST(Mem2RegNonEntry, DominatingNonEntryAllocaIsPromoted)
{
    Module module = buildDominatingNonEntryAlloca();
    ASSERT_FALSE(module.functions.empty());

    unsigned allocasBefore = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    ASSERT_EQ(allocasBefore, 1u);

    viper::passes::Mem2RegStats stats{};
    viper::passes::mem2reg(module, &stats);

    // Alloca and its load must be gone after promotion
    unsigned allocasAfter = countOpcodeInFunction(module.functions[0], Opcode::Alloca);
    unsigned loadsAfter = countOpcodeInFunction(module.functions[0], Opcode::Load);
    EXPECT_EQ(allocasAfter, 0u);
    EXPECT_EQ(loadsAfter, 0u);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
