//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Peephole fixes from the IL optimization review:
// - Unsigned comparison (UCmpLT/LE/GT/GE) folding in CBr simplification
// - Float comparison (FCmpEQ/NE/LT/LE/GT/GE) folding in CBr simplification
// - Reflexive comparison rules for UCmp and FCmp opcodes
//
//===----------------------------------------------------------------------===//

#include "il/transform/Peephole.hpp"

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

// Build a module with: cmp = op(a, b); cbr cmp, "true_bb", "false_bb"; ret in each
Module buildCmpBrModule(Opcode cmpOp, Value lhs, Value rhs, Type::Kind resultTypeKind = Type::Kind::I1)
{
    Module module;
    Function fn;
    fn.name = "cmp_br_test";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;

    BasicBlock entry;
    entry.label = "entry";

    Instr cmp;
    cmp.result = nextId++;
    cmp.op = cmpOp;
    cmp.type = Type(resultTypeKind);
    cmp.operands.push_back(lhs);
    cmp.operands.push_back(rhs);
    entry.instructions.push_back(std::move(cmp));

    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.type = Type(Type::Kind::Void);
    cbr.operands.push_back(Value::temp(0));
    cbr.labels = {"true_bb", "false_bb"};
    cbr.brArgs = {{}, {}};
    entry.instructions.push_back(std::move(cbr));
    entry.terminated = true;

    BasicBlock trueBB;
    trueBB.label = "true_bb";
    Instr retTrue;
    retTrue.op = Opcode::Ret;
    retTrue.type = Type(Type::Kind::Void);
    retTrue.operands.push_back(Value::constInt(1));
    trueBB.instructions.push_back(std::move(retTrue));
    trueBB.terminated = true;

    BasicBlock falseBB;
    falseBB.label = "false_bb";
    Instr retFalse;
    retFalse.op = Opcode::Ret;
    retFalse.type = Type(Type::Kind::Void);
    retFalse.operands.push_back(Value::constInt(0));
    falseBB.instructions.push_back(std::move(retFalse));
    falseBB.terminated = true;

    fn.blocks = {std::move(entry), std::move(trueBB), std::move(falseBB)};
    fn.valueNames.resize(nextId);
    fn.valueNames[0] = "cmp";
    module.functions.push_back(std::move(fn));
    return module;
}

// Check if peephole converted the CBr to an unconditional Br
bool cbrSimplifiedToTarget(const Module &module, const std::string &expectedTarget)
{
    const Function &fn = module.functions.front();
    const BasicBlock &entry = fn.blocks.front();
    const Instr &term = entry.instructions.back();
    if (term.op == Opcode::Br && !term.labels.empty())
        return term.labels[0] == expectedTarget;
    return false;
}

// Build a module with reflexive comparison: op(temp, temp) used in CBr
Module buildReflexiveCmpModule(Opcode cmpOp)
{
    Module module;
    Function fn;
    fn.name = "reflexive_test";
    fn.retType = Type(Type::Kind::I64);

    Param param;
    param.name = "x";
    param.type = Type(Type::Kind::I64);
    param.id = 0;
    fn.params.push_back(param);

    unsigned nextId = 1;

    BasicBlock entry;
    entry.label = "entry";

    Instr cmp;
    cmp.result = nextId++;
    cmp.op = cmpOp;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::temp(0)); // same temp
    cmp.operands.push_back(Value::temp(0)); // same temp
    entry.instructions.push_back(std::move(cmp));

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.operands.push_back(Value::temp(1));
    entry.instructions.push_back(std::move(ret));
    entry.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.valueNames.resize(nextId);
    fn.valueNames[1] = "cmp";
    module.functions.push_back(std::move(fn));
    return module;
}

// After peephole, check if the reflexive cmp was replaced with a constant
bool reflexiveCmpReplacedWith(const Module &module, long long expectedVal)
{
    const Function &fn = module.functions.front();
    const BasicBlock &entry = fn.blocks.front();
    // The ret should now use a constant instead of temp(1)
    const Instr &ret = entry.instructions.back();
    if (ret.op == Opcode::Ret && !ret.operands.empty())
    {
        const Value &v = ret.operands[0];
        if (v.kind == Value::Kind::ConstInt && v.i64 == expectedVal)
            return true;
    }
    return false;
}

} // namespace

// --- Unsigned comparison folding in CBr ---

TEST(Peephole, UCmpLT_ConstFoldInCBr)
{
    // 3 < 5 (unsigned) = true => should branch to true_bb
    Module m = buildCmpBrModule(Opcode::UCmpLT, Value::constInt(3), Value::constInt(5));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, UCmpLT_ConstFoldFalseInCBr)
{
    // 5 < 3 (unsigned) = false => should branch to false_bb
    Module m = buildCmpBrModule(Opcode::UCmpLT, Value::constInt(5), Value::constInt(3));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "false_bb"));
}

TEST(Peephole, UCmpLE_ConstFoldInCBr)
{
    // 5 <= 5 (unsigned) = true
    Module m = buildCmpBrModule(Opcode::UCmpLE, Value::constInt(5), Value::constInt(5));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, UCmpGT_ConstFoldInCBr)
{
    // 10 > 3 (unsigned) = true
    Module m = buildCmpBrModule(Opcode::UCmpGT, Value::constInt(10), Value::constInt(3));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, UCmpGE_ConstFoldInCBr)
{
    // 3 >= 5 (unsigned) = false
    Module m = buildCmpBrModule(Opcode::UCmpGE, Value::constInt(3), Value::constInt(5));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "false_bb"));
}

// --- Float comparison folding in CBr ---

TEST(Peephole, FCmpEQ_ConstFoldInCBr)
{
    // 3.0 == 3.0 = true
    Module m = buildCmpBrModule(Opcode::FCmpEQ, Value::constFloat(3.0), Value::constFloat(3.0));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, FCmpNE_ConstFoldInCBr)
{
    // 3.0 != 5.0 = true
    Module m = buildCmpBrModule(Opcode::FCmpNE, Value::constFloat(3.0), Value::constFloat(5.0));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, FCmpLT_ConstFoldInCBr)
{
    // 2.5 < 3.5 = true
    Module m = buildCmpBrModule(Opcode::FCmpLT, Value::constFloat(2.5), Value::constFloat(3.5));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, FCmpLT_ConstFoldFalseInCBr)
{
    // 5.0 < 3.0 = false
    Module m = buildCmpBrModule(Opcode::FCmpLT, Value::constFloat(5.0), Value::constFloat(3.0));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "false_bb"));
}

TEST(Peephole, FCmpLE_ConstFoldInCBr)
{
    // 3.0 <= 3.0 = true
    Module m = buildCmpBrModule(Opcode::FCmpLE, Value::constFloat(3.0), Value::constFloat(3.0));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, FCmpGT_ConstFoldInCBr)
{
    // 10.0 > 3.0 = true
    Module m = buildCmpBrModule(Opcode::FCmpGT, Value::constFloat(10.0), Value::constFloat(3.0));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "true_bb"));
}

TEST(Peephole, FCmpGE_ConstFoldInCBr)
{
    // 3.0 >= 5.0 = false
    Module m = buildCmpBrModule(Opcode::FCmpGE, Value::constFloat(3.0), Value::constFloat(5.0));
    il::transform::peephole(m);
    EXPECT_TRUE(cbrSimplifiedToTarget(m, "false_bb"));
}

// --- Reflexive unsigned comparison rules ---

TEST(Peephole, UCmpLT_ReflexiveFoldsToFalse)
{
    Module m = buildReflexiveCmpModule(Opcode::UCmpLT);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 0));
}

TEST(Peephole, UCmpLE_ReflexiveFoldsToTrue)
{
    Module m = buildReflexiveCmpModule(Opcode::UCmpLE);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 1));
}

TEST(Peephole, UCmpGT_ReflexiveFoldsToFalse)
{
    Module m = buildReflexiveCmpModule(Opcode::UCmpGT);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 0));
}

TEST(Peephole, UCmpGE_ReflexiveFoldsToTrue)
{
    Module m = buildReflexiveCmpModule(Opcode::UCmpGE);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 1));
}

// --- Reflexive float comparison rules ---

TEST(Peephole, FCmpEQ_ReflexiveFoldsToTrue)
{
    Module m = buildReflexiveCmpModule(Opcode::FCmpEQ);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 1));
}

TEST(Peephole, FCmpNE_ReflexiveFoldsToFalse)
{
    Module m = buildReflexiveCmpModule(Opcode::FCmpNE);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 0));
}

TEST(Peephole, FCmpLT_ReflexiveFoldsToFalse)
{
    Module m = buildReflexiveCmpModule(Opcode::FCmpLT);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 0));
}

TEST(Peephole, FCmpLE_ReflexiveFoldsToTrue)
{
    Module m = buildReflexiveCmpModule(Opcode::FCmpLE);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 1));
}

TEST(Peephole, FCmpGT_ReflexiveFoldsToFalse)
{
    Module m = buildReflexiveCmpModule(Opcode::FCmpGT);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 0));
}

TEST(Peephole, FCmpGE_ReflexiveFoldsToTrue)
{
    Module m = buildReflexiveCmpModule(Opcode::FCmpGE);
    il::transform::peephole(m);
    EXPECT_TRUE(reflexiveCmpReplacedWith(m, 1));
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
