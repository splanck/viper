//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for SCCP float division handling:
// - FDiv propagates the IL's defined IEEE-754 infinity and NaN results
// - FDiv normal case folds correctly
// - FDiv 0.0/0.0 is NOT folded (NaN is non-finite)
//
// Note: SCCP deliberately refuses to fold FDiv when the result is non-finite
// (±inf or NaN) to align with ConstFold's conservative policy.  Folding
// non-finite constants can cascade through the lattice and produce surprising
// codegen.  The runtime handles IEEE 754 semantics at execution time.
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
#include <limits>

using namespace il::core;

namespace {

// Build a module with a single FDiv instruction followed by a Ret
Module buildFDivModule(double lhs, double rhs) {
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

TEST(SCCP, PlainIntegerAddWrapsWithoutSignedOverflowUB) {
    Module module;
    Function fn;
    fn.name = "wrap_add_test";
    fn.retType = Type(Type::Kind::I64);

    BasicBlock entry;
    entry.label = "entry";

    Instr add;
    add.result = 0;
    add.op = Opcode::Add;
    add.type = Type(Type::Kind::I64);
    add.operands.push_back(Value::constInt((std::numeric_limits<long long>::max)()));
    add.operands.push_back(Value::constInt(1));
    entry.instructions.push_back(std::move(add));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(0));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(1);
    module.functions.push_back(std::move(fn));

    il::transform::sccp(module);

    const Value &retVal = module.functions.front().blocks.front().instructions.back().operands[0];
    ASSERT_EQ(retVal.kind, Value::Kind::ConstInt);
    EXPECT_EQ(retVal.i64, (std::numeric_limits<long long>::min)());
}

// FDiv by zero propagates the IL's defined IEEE-754 infinity.
TEST(SCCP, FDivByZeroFoldsToPositiveInfinity) {
    Module module = buildFDivModule(1.0, 0.0);
    il::transform::sccp(module);

    const Value &value = module.functions.front().blocks.front().instructions.back().operands[0];
    ASSERT_EQ(value.kind, Value::Kind::ConstFloat);
    EXPECT_TRUE(std::isinf(value.f64));
    EXPECT_FALSE(std::signbit(value.f64));
}

TEST(SCCP, FDivNegByZeroFoldsToNegativeInfinity) {
    Module module = buildFDivModule(-1.0, 0.0);
    il::transform::sccp(module);

    const Value &value = module.functions.front().blocks.front().instructions.back().operands[0];
    ASSERT_EQ(value.kind, Value::Kind::ConstFloat);
    EXPECT_TRUE(std::isinf(value.f64));
    EXPECT_TRUE(std::signbit(value.f64));
}

// Normal FDiv folds correctly
TEST(SCCP, FDivNormalFoldsCorrectly) {
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

TEST(SCCP, FDivZeroByZeroFoldsToNaN) {
    Module module = buildFDivModule(0.0, 0.0);
    il::transform::sccp(module);

    const Value &value = module.functions.front().blocks.front().instructions.back().operands[0];
    ASSERT_EQ(value.kind, Value::Kind::ConstFloat);
    EXPECT_TRUE(std::isnan(value.f64));
}

/// @brief Main.
int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
