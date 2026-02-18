//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for the extended EarlyCSE pass — specifically the dominator-tree-scoped
// CSE that replaces the old per-block-only approach.
//
// Test cases:
//   1. Cross-block CSE: an Add in the entry block eliminates the same Add
//      (with commuted operands) in the only successor.
//   2. Non-dominated sibling branches: the same expression in two sibling
//      branches must NOT be eliminated by the pass (neither dominates the
//      other), so both instructions must remain.
//
//===----------------------------------------------------------------------===//

#include "il/transform/EarlyCSE.hpp"

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

/// Build:
///   fn cross_block(a: i64, b: i64) -> i64:
///     entry:
///       t2 = add a, b
///       br next
///     next:
///       t3 = add b, a    ; commuted duplicate — dominated by entry
///       ret t3
///
/// After EarlyCSE, t3 must be eliminated and ret must use t2.
Module buildCrossBlockCSE()
{
    Module M;
    Function F;
    F.name = "cross_block";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;
    Param a{"a", Type(Type::Kind::I64), id++}; // t0
    Param b{"b", Type(Type::Kind::I64), id++}; // t1
    F.params = {a, b};

    // entry: t2 = add a, b  →  br next
    BasicBlock entry;
    entry.label = "entry";
    {
        Instr add1;
        add1.result = id++; // t2
        add1.op = Opcode::Add;
        add1.type = Type(Type::Kind::I64);
        add1.operands = {Value::temp(a.id), Value::temp(b.id)};
        entry.instructions.push_back(std::move(add1));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("next");
        br.brArgs.push_back({});
        entry.instructions.push_back(std::move(br));
        entry.terminated = true;
    }

    // next: t3 = add b, a   (commuted)  →  ret t3
    BasicBlock next;
    next.label = "next";
    {
        Instr add2;
        add2.result = id++; // t3
        add2.op = Opcode::Add;
        add2.type = Type(Type::Kind::I64);
        add2.operands = {Value::temp(b.id), Value::temp(a.id)}; // commuted
        next.instructions.push_back(std::move(add2));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands = {Value::temp(id - 1)}; // t3
        next.instructions.push_back(std::move(ret));
        next.terminated = true;
    }

    F.blocks.push_back(std::move(entry));
    F.blocks.push_back(std::move(next));
    F.valueNames.resize(id);
    F.valueNames[a.id] = "a";
    F.valueNames[b.id] = "b";
    M.functions.push_back(std::move(F));
    return M;
}

/// Count instructions with a given opcode across all blocks of a function.
unsigned countOpcode(const Function &fn, Opcode op)
{
    unsigned n = 0;
    for (const auto &B : fn.blocks)
        for (const auto &I : B.instructions)
            if (I.op == op)
                ++n;
    return n;
}

/// Build:
///   fn siblings(a: i64, b: i64) -> i64:
///     entry:
///       cbr 1, then, els
///     then:
///       t2 = add a, b
///       br merge, t2
///     els:
///       t3 = add a, b    ; same expression but in a sibling — not dominated
///       br merge, t3
///     merge(x: i64):
///       ret x
///
/// EarlyCSE must NOT remove t3 because "then" does not dominate "els".
Module buildSiblingBranchCSE()
{
    Module M;
    Function F;
    F.name = "siblings";
    F.retType = Type(Type::Kind::I64);

    unsigned id = 0;
    Param a{"a", Type(Type::Kind::I64), id++}; // t0
    Param b{"b", Type(Type::Kind::I64), id++}; // t1
    F.params = {a, b};

    // entry: cbr 1, then, els
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

    // then: t2 = add a, b  → br merge, t2
    BasicBlock then_;
    then_.label = "then";
    {
        Instr add;
        add.result = id++; // t2
        add.op = Opcode::Add;
        add.type = Type(Type::Kind::I64);
        add.operands = {Value::temp(a.id), Value::temp(b.id)};
        then_.instructions.push_back(std::move(add));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("merge");
        br.brArgs.push_back({Value::temp(id - 1)});
        then_.instructions.push_back(std::move(br));
        then_.terminated = true;
    }

    // els: t3 = add a, b  → br merge, t3
    BasicBlock els_;
    els_.label = "els";
    {
        Instr add;
        add.result = id++; // t3
        add.op = Opcode::Add;
        add.type = Type(Type::Kind::I64);
        add.operands = {Value::temp(a.id), Value::temp(b.id)};
        els_.instructions.push_back(std::move(add));

        Instr br;
        br.op = Opcode::Br;
        br.type = Type(Type::Kind::Void);
        br.labels.push_back("merge");
        br.brArgs.push_back({Value::temp(id - 1)});
        els_.instructions.push_back(std::move(br));
        els_.terminated = true;
    }

    // merge(x: i64): ret x
    BasicBlock merge;
    merge.label = "merge";
    {
        Param x{"x", Type(Type::Kind::I64), id++}; // t4
        merge.params.push_back(x);

        Instr ret;
        ret.op = Opcode::Ret;
        ret.type = Type(Type::Kind::Void);
        ret.operands.push_back(Value::temp(x.id));
        merge.instructions.push_back(std::move(ret));
        merge.terminated = true;
    }

    F.blocks.push_back(std::move(entry));
    F.blocks.push_back(std::move(then_));
    F.blocks.push_back(std::move(els_));
    F.blocks.push_back(std::move(merge));
    F.valueNames.resize(id);
    F.valueNames[a.id] = "a";
    F.valueNames[b.id] = "b";
    M.functions.push_back(std::move(F));
    return M;
}

} // namespace

// An Add in the entry block dominates its successor — the commuted duplicate
// in the successor must be eliminated by the dominator-tree CSE.
TEST(EarlyCSEDomTree, CrossBlockCSEEliminatesDuplicateInDominatedBlock)
{
    Module M = buildCrossBlockCSE();
    ASSERT_EQ(M.functions.size(), 1u);
    Function &fn = M.functions.front();

    unsigned addsBefore = countOpcode(fn, Opcode::Add);
    ASSERT_EQ(addsBefore, 2u); // entry add + next add

    bool changed = il::transform::runEarlyCSE(M, fn);
    EXPECT_TRUE(changed);

    // Duplicate add in "next" must be gone.
    unsigned addsAfter = countOpcode(fn, Opcode::Add);
    EXPECT_EQ(addsAfter, 1u);

    // The surviving ret must reference the entry-block add result.
    const BasicBlock &entryBlock = fn.blocks[0];
    const BasicBlock &nextBlock = fn.blocks[1];
    ASSERT_FALSE(entryBlock.instructions.empty());
    unsigned entryAddId = *entryBlock.instructions[0].result;

    const Instr &retInstr = nextBlock.instructions.back();
    ASSERT_FALSE(retInstr.operands.empty());
    EXPECT_EQ(retInstr.operands[0].kind, Value::Kind::Temp);
    EXPECT_EQ(retInstr.operands[0].id, entryAddId);
}

// Sibling branches (then / els) do not dominate each other. The same Add in
// both siblings must NOT be eliminated by EarlyCSE.
TEST(EarlyCSEDomTree, SiblingBranchExpressionsAreNotEliminated)
{
    Module M = buildSiblingBranchCSE();
    ASSERT_EQ(M.functions.size(), 1u);
    Function &fn = M.functions.front();

    unsigned addsBefore = countOpcode(fn, Opcode::Add);
    ASSERT_EQ(addsBefore, 2u); // one in then, one in els

    il::transform::runEarlyCSE(M, fn);

    // Both adds must survive — neither branch dominates the other.
    unsigned addsAfter = countOpcode(fn, Opcode::Add);
    EXPECT_EQ(addsAfter, 2u);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
