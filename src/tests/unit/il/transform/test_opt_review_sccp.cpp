//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for SCCP fixes from the IL optimization review:
// - FDiv by zero folds to IEEE 754 infinity (not left as unknown)
// - FDiv normal case folds correctly
//
//===----------------------------------------------------------------------===//

#include "il/transform/SCCP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

#include <cmath>

using namespace il::core;

namespace
{

// Build a module with a single FDiv instruction followed by a Ret
Module buildFDivModule(double lhs, double rhs)
{
    Module module;
    Function fn;
    fn.name = "fdiv_test";
    fn.retType = Type(Type::Kind::F64);

    unsigned nextId = 0;

    BasicBlock entry;
    entry.label = "entry";

    Instr fdiv;
    fdiv.result = nextId++;
    fdiv.op = Opcode::FDiv;
    fdiv.type = Type(Type::Kind::F64);
    fdiv.operands.push_back(Value::constFloat(lhs));
    fdiv.operands.push_back(Value::constFloat(rhs));
    entry.instructions.push_back(std::move(fdiv));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(0));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "result";
    module.functions.push_back(std::move(fn));
    return module;
}

} // namespace

// Test that FDiv by zero folds to infinity per IEEE 754
TEST(SCCP, FDivByZeroFoldsToInfinity)
{
    Module module = buildFDivModule(1.0, 0.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    // After SCCP, the ret operand should be a constant float (infinity)
    Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_FALSE(ret.operands.empty());

    const Value &retVal = ret.operands[0];
    EXPECT_EQ(retVal.kind, Value::Kind::ConstFloat);
    EXPECT_TRUE(std::isinf(retVal.f64));
    EXPECT_TRUE(retVal.f64 > 0.0); // positive infinity
}

// Test that FDiv -1.0 / 0.0 folds to negative infinity
TEST(SCCP, FDivNegByZeroFoldsToNegInfinity)
{
    Module module = buildFDivModule(-1.0, 0.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_FALSE(ret.operands.empty());

    const Value &retVal = ret.operands[0];
    EXPECT_EQ(retVal.kind, Value::Kind::ConstFloat);
    EXPECT_TRUE(std::isinf(retVal.f64));
    EXPECT_TRUE(retVal.f64 < 0.0); // negative infinity
}

// Test normal FDiv folds correctly
TEST(SCCP, FDivNormalFoldsCorrectly)
{
    Module module = buildFDivModule(10.0, 2.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_FALSE(ret.operands.empty());

    const Value &retVal = ret.operands[0];
    EXPECT_EQ(retVal.kind, Value::Kind::ConstFloat);
    EXPECT_EQ(retVal.f64, 5.0);
}

// Test that FDiv 0.0/0.0 produces NaN
TEST(SCCP, FDivZeroByZeroFoldsToNaN)
{
    Module module = buildFDivModule(0.0, 0.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    Instr &ret = entry.instructions.back();
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_FALSE(ret.operands.empty());

    const Value &retVal = ret.operands[0];
    EXPECT_EQ(retVal.kind, Value::Kind::ConstFloat);
    EXPECT_TRUE(std::isnan(retVal.f64));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
