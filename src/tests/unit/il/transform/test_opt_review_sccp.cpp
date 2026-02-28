//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for SCCP float division handling:
// - FDiv by zero is NOT folded (non-finite results are unsafe to propagate)
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

// Helper: check whether the FDiv instruction is still present (not folded)
bool hasFDivInstr(const BasicBlock &bb)
{
    for (const auto &instr : bb.instructions)
        if (instr.op == Opcode::FDiv)
            return true;
    return false;
}

} // namespace

// FDiv by zero must NOT be folded — non-finite results are unsafe to propagate
TEST(SCCP, FDivByZeroNotFolded)
{
    Module module = buildFDivModule(1.0, 0.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    // FDiv instruction should remain — producing +inf is not safe to fold
    EXPECT_TRUE(hasFDivInstr(entry));
}

// FDiv -1.0/0.0 must NOT be folded — would produce -inf
TEST(SCCP, FDivNegByZeroNotFolded)
{
    Module module = buildFDivModule(-1.0, 0.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    EXPECT_TRUE(hasFDivInstr(entry));
}

// Normal FDiv folds correctly
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

// FDiv 0.0/0.0 must NOT be folded — would produce NaN
TEST(SCCP, FDivZeroByZeroNotFolded)
{
    Module module = buildFDivModule(0.0, 0.0);
    il::transform::sccp(module);

    Function &fn = module.functions.front();
    BasicBlock &entry = fn.blocks.front();

    EXPECT_TRUE(hasFDivInstr(entry));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
