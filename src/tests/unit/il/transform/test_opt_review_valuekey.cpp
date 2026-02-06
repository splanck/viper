//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for ValueKey fixes from the IL optimization review:
// - Commutative operand normalization produces consistent keys
// - makeValueKey filters unsafe opcodes correctly
// - isCommutativeCSE returns correct classifications
// - isSafeCSEOpcode returns correct classifications
//
//===----------------------------------------------------------------------===//

#include "il/transform/ValueKey.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

using namespace il::core;

namespace
{

Instr makeArith(Opcode op, Value lhs, Value rhs, unsigned resultId = 0)
{
    Instr instr;
    instr.result = resultId;
    instr.op = op;
    instr.type = Type(Type::Kind::I64);
    instr.operands = {lhs, rhs};
    return instr;
}

Instr makeFloatArith(Opcode op, Value lhs, Value rhs, unsigned resultId = 0)
{
    Instr instr;
    instr.result = resultId;
    instr.op = op;
    instr.type = Type(Type::Kind::F64);
    instr.operands = {lhs, rhs};
    return instr;
}

} // namespace

// Test that commutative operations produce the same key regardless of operand order
TEST(ValueKey, CommutativeAddNormalization)
{
    Instr a = makeArith(Opcode::Add, Value::temp(1), Value::temp(2));
    Instr b = makeArith(Opcode::Add, Value::temp(2), Value::temp(1));

    auto keyA = il::transform::makeValueKey(a);
    auto keyB = il::transform::makeValueKey(b);

    ASSERT_TRUE(keyA.has_value());
    ASSERT_TRUE(keyB.has_value());

    // Same expression, different operand order => same key
    EXPECT_TRUE(*keyA == *keyB);

    il::transform::ValueKeyHash hash;
    EXPECT_EQ(hash(*keyA), hash(*keyB));
}

// Test that commutative Mul produces same key
TEST(ValueKey, CommutativeMulNormalization)
{
    Instr a = makeArith(Opcode::Mul, Value::constInt(3), Value::temp(5));
    Instr b = makeArith(Opcode::Mul, Value::temp(5), Value::constInt(3));

    auto keyA = il::transform::makeValueKey(a);
    auto keyB = il::transform::makeValueKey(b);

    ASSERT_TRUE(keyA.has_value());
    ASSERT_TRUE(keyB.has_value());
    EXPECT_TRUE(*keyA == *keyB);
}

// Test that non-commutative Sub does NOT normalize
TEST(ValueKey, NonCommutativeSubNotNormalized)
{
    Instr a = makeArith(Opcode::Sub, Value::temp(1), Value::temp(2));
    Instr b = makeArith(Opcode::Sub, Value::temp(2), Value::temp(1));

    auto keyA = il::transform::makeValueKey(a);
    auto keyB = il::transform::makeValueKey(b);

    ASSERT_TRUE(keyA.has_value());
    ASSERT_TRUE(keyB.has_value());

    // Sub is not commutative, different operand order => different key
    EXPECT_FALSE(*keyA == *keyB);
}

// Test that float commutative ops normalize correctly
TEST(ValueKey, CommutativeFAddNormalization)
{
    Instr a = makeFloatArith(Opcode::FAdd, Value::constFloat(1.5), Value::temp(3));
    Instr b = makeFloatArith(Opcode::FAdd, Value::temp(3), Value::constFloat(1.5));

    auto keyA = il::transform::makeValueKey(a);
    auto keyB = il::transform::makeValueKey(b);

    ASSERT_TRUE(keyA.has_value());
    ASSERT_TRUE(keyB.has_value());
    EXPECT_TRUE(*keyA == *keyB);
}

// Test isCommutativeCSE classifications
TEST(ValueKey, CommutativeClassifications)
{
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::Add));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::Mul));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::And));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::Or));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::Xor));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::ICmpEq));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::ICmpNe));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::FAdd));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::FMul));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::FCmpEQ));
    EXPECT_TRUE(il::transform::isCommutativeCSE(Opcode::FCmpNE));

    EXPECT_FALSE(il::transform::isCommutativeCSE(Opcode::Sub));
    EXPECT_FALSE(il::transform::isCommutativeCSE(Opcode::FSub));
    EXPECT_FALSE(il::transform::isCommutativeCSE(Opcode::FDiv));
    EXPECT_FALSE(il::transform::isCommutativeCSE(Opcode::SCmpLT));
}

// Test isSafeCSEOpcode classifications
TEST(ValueKey, SafeCSEClassifications)
{
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::Add));
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::Sub));
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::Mul));
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::ICmpEq));
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::FCmpLT));
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::Zext1));
    EXPECT_TRUE(il::transform::isSafeCSEOpcode(Opcode::Trunc1));

    // Memory and side-effect ops are not safe
    EXPECT_FALSE(il::transform::isSafeCSEOpcode(Opcode::Load));
    EXPECT_FALSE(il::transform::isSafeCSEOpcode(Opcode::Store));
    EXPECT_FALSE(il::transform::isSafeCSEOpcode(Opcode::Call));
    EXPECT_FALSE(il::transform::isSafeCSEOpcode(Opcode::Alloca));
}

// Test that makeValueKey rejects unsafe opcodes
TEST(ValueKey, RejectsUnsafeOpcodes)
{
    // Load is not safe for CSE
    Instr load;
    load.result = 0;
    load.op = Opcode::Load;
    load.type = Type(Type::Kind::I64);
    load.operands.push_back(Value::temp(1));
    EXPECT_FALSE(il::transform::makeValueKey(load).has_value());

    // Call is not safe for CSE
    Instr call;
    call.result = 0;
    call.op = Opcode::Call;
    call.type = Type(Type::Kind::I64);
    call.callee = "some_fn";
    EXPECT_FALSE(il::transform::makeValueKey(call).has_value());
}

// Test that makeValueKey rejects instructions without results
TEST(ValueKey, RejectsNoResult)
{
    Instr store;
    store.op = Opcode::Store;
    store.type = Type(Type::Kind::Void);
    store.operands = {Value::temp(0), Value::temp(1)};
    EXPECT_FALSE(il::transform::makeValueKey(store).has_value());
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
