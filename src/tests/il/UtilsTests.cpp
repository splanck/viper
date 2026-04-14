//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/UtilsTests.cpp
// Purpose: Verify IL utility helpers for block membership, terminators, and
//          safe temp-use replacement helpers.
// Key invariants: Helpers correctly identify instruction containment and
//                 terminators; UseDefInfo remains safe across instruction erasure.
// Ownership/Lifetime: Constructs local IL blocks and instructions.
// Links: docs/dev/analysis.md
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/utils/UseDefInfo.hpp"
#include "il/utils/Utils.hpp"
#include "tests/TestHarness.hpp"

TEST(IL, UtilsTests) {
    using namespace viper::il;
    using il::core::BasicBlock;
    using il::core::Instr;
    using il::core::Opcode;

    // Block with a single non-terminator.
    BasicBlock b;
    b.label = "b";
    b.instructions.emplace_back();
    Instr &add = b.instructions.back();
    add.op = Opcode::IAddOvf;
    ASSERT_TRUE(belongsToBlock(add, b));
    Instr other;
    other.op = Opcode::IAddOvf;
    ASSERT_FALSE(belongsToBlock(other, b));
    ASSERT_FALSE(isTerminator(add));
    ASSERT_EQ(terminator(b), nullptr);

    auto checkTerm = [](Opcode op) {
        BasicBlock blk;
        blk.label = "t";
        blk.instructions.emplace_back();
        blk.instructions.back().op = Opcode::IAddOvf;
        blk.instructions.emplace_back();
        Instr &term = blk.instructions.back();
        term.op = op;
        blk.terminated = true;
        ASSERT_TRUE(isTerminator(term));
        ASSERT_EQ(terminator(blk), &term);
    };

    checkTerm(Opcode::Br);
    checkTerm(Opcode::CBr);
    checkTerm(Opcode::Ret);
    checkTerm(Opcode::Trap);
    checkTerm(Opcode::TrapFromErr);
    checkTerm(Opcode::ResumeSame);
    checkTerm(Opcode::ResumeNext);
    checkTerm(Opcode::ResumeLabel);

    // UseDefInfo must remain safe when callers erase instructions after a
    // replacement. The old raw-pointer implementation would leave behind
    // invalid use-site pointers into the block's instruction vector.
    il::core::Function fn;
    fn.name = "use_def_mutation";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    {
        il::core::Instr add0;
        add0.result = 0;
        add0.op = Opcode::Add;
        add0.type = il::core::Type(il::core::Type::Kind::I64);
        add0.operands = {il::core::Value::constInt(1), il::core::Value::constInt(2)};
        entry.instructions.push_back(std::move(add0));

        il::core::Instr add1;
        add1.result = 1;
        add1.op = Opcode::Add;
        add1.type = il::core::Type(il::core::Type::Kind::I64);
        add1.operands = {il::core::Value::constInt(1), il::core::Value::constInt(2)};
        entry.instructions.push_back(std::move(add1));

        il::core::Instr sub;
        sub.result = 2;
        sub.op = Opcode::Sub;
        sub.type = il::core::Type(il::core::Type::Kind::I64);
        sub.operands = {il::core::Value::temp(1), il::core::Value::constInt(1)};
        entry.instructions.push_back(std::move(sub));

        il::core::Instr add2;
        add2.result = 3;
        add2.op = Opcode::Add;
        add2.type = il::core::Type(il::core::Type::Kind::I64);
        add2.operands = {il::core::Value::constInt(1), il::core::Value::constInt(2)};
        entry.instructions.push_back(std::move(add2));

        il::core::Instr ret;
        ret.op = Opcode::Ret;
        ret.type = il::core::Type(il::core::Type::Kind::Void);
        ret.operands = {il::core::Value::temp(3)};
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;
    }
    fn.blocks.push_back(std::move(entry));

    viper::il::UseDefInfo useInfo(fn);
    ASSERT_EQ(useInfo.useCount(1), 1u);
    ASSERT_EQ(useInfo.useCount(3), 1u);

    EXPECT_EQ(useInfo.replaceAllUses(1, il::core::Value::temp(0)), 1u);
    fn.blocks.front().instructions.erase(fn.blocks.front().instructions.begin() + 1);

    EXPECT_EQ(useInfo.replaceAllUses(3, il::core::Value::temp(0)), 1u);
    const auto &instructions = fn.blocks.front().instructions;
    ASSERT_EQ(instructions.size(), 4u);
    ASSERT_EQ(instructions[1].op, Opcode::Sub);
    ASSERT_EQ(instructions[2].op, Opcode::Add);
    ASSERT_EQ(instructions[3].op, Opcode::Ret);
    ASSERT_EQ(instructions[1].operands[0].kind, il::core::Value::Kind::Temp);
    EXPECT_EQ(instructions[1].operands[0].id, 0u);
    ASSERT_EQ(instructions[3].operands[0].kind, il::core::Value::Kind::Temp);
    EXPECT_EQ(instructions[3].operands[0].id, 0u);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
