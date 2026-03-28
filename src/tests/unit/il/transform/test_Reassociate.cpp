//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_Reassociate.cpp
// Purpose: Validate operand canonicalization for commutative+associative ops.
// Key invariants:
//   - Temporaries sort before constants.
//   - Non-commutative ops (Sub, SDiv) are untouched.
// Ownership/Lifetime: Transient modules.
// Links: il/transform/Reassociate.cpp
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
#include "il/transform/Reassociate.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace {

Instr makeBinary(Opcode op, unsigned result, Value lhs, Value rhs) {
    Instr i;
    i.op = op;
    i.result = result;
    i.operands.push_back(lhs);
    i.operands.push_back(rhs);
    return i;
}

} // namespace

TEST(Reassociate, SwapsConstBeforeTemp) {
    // 1 + %0  →  %0 + 1  (temp should come first)
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }
    entry.instructions.push_back(makeBinary(Opcode::Add, 1, Value::constInt(1), Value::temp(0)));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    mod.functions.push_back(std::move(fn));

    il::transform::reassociate(mod);

    const auto &add = mod.functions[0].blocks[0].instructions[0];
    EXPECT_EQ(add.operands[0].kind, Value::Kind::Temp);
    EXPECT_EQ(add.operands[1].kind, Value::Kind::ConstInt);
}

TEST(Reassociate, DoesNotSwapSubOperands) {
    // Sub is not commutative — operands must stay in order
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }
    entry.instructions.push_back(makeBinary(Opcode::Sub, 1, Value::constInt(10), Value::temp(0)));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    mod.functions.push_back(std::move(fn));

    il::transform::reassociate(mod);

    const auto &sub = mod.functions[0].blocks[0].instructions[0];
    // Should NOT be swapped — Sub is not commutative
    EXPECT_EQ(sub.operands[0].kind, Value::Kind::ConstInt);
    EXPECT_EQ(sub.operands[1].kind, Value::Kind::Temp);
}

TEST(Reassociate, LeavesAlreadyCanonical) {
    // %0 + 1 — already canonical, should not change
    Module mod;
    Function fn;
    fn.name = "test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";
    {
        Param p;
        p.name = "x";
        p.type = Type(Type::Kind::I64);
        p.id = 0;
        entry.params.push_back(std::move(p));
    }
    entry.instructions.push_back(makeBinary(Opcode::Add, 1, Value::temp(0), Value::constInt(1)));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    mod.functions.push_back(std::move(fn));

    il::transform::reassociate(mod);

    const auto &add = mod.functions[0].blocks[0].instructions[0];
    EXPECT_EQ(add.operands[0].kind, Value::Kind::Temp);
    EXPECT_EQ(add.operands[1].kind, Value::Kind::ConstInt);
}

TEST(Reassociate, CanonicalizesAllCommutativeOps) {
    // Test Mul, And, Or, Xor all get canonicalized
    Opcode ops[] = {Opcode::Mul, Opcode::And, Opcode::Or, Opcode::Xor};

    for (auto op : ops) {
        Module mod;
        Function fn;
        fn.name = "test";
        fn.retType = Type(Type::Kind::I64);

        BasicBlock entry;
        entry.label = "entry";
        {
            Param p;
            p.name = "x";
            p.type = Type(Type::Kind::I64);
            p.id = 0;
            entry.params.push_back(std::move(p));
        }
        // const first, temp second — should swap
        entry.instructions.push_back(makeBinary(op, 1, Value::constInt(42), Value::temp(0)));

        Instr ret;
        ret.op = Opcode::Ret;
        ret.operands.push_back(Value::temp(1));
        entry.instructions.push_back(std::move(ret));
        entry.terminated = true;

        fn.blocks.push_back(std::move(entry));
        mod.functions.push_back(std::move(fn));

        il::transform::reassociate(mod);

        const auto &instr = mod.functions[0].blocks[0].instructions[0];
        EXPECT_EQ(instr.operands[0].kind, Value::Kind::Temp);
        EXPECT_EQ(instr.operands[1].kind, Value::Kind::ConstInt);
    }
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
