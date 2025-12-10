//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/il/transform/test_SCCP.cpp
// Purpose: Validate SCCP lattice behaviour (constants, traps) and interaction
//          with SimplifyCFG on conditional/switch terminators.
// Links: docs/architecture.md, docs/il-reference.md
//
//===----------------------------------------------------------------------===//

#include "il/transform/SCCP.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/io/Serializer.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include "tests/unit/GTestStub.hpp"

#include <cassert>
#include <string>

using namespace il::core;

namespace
{

BasicBlock *findBlock(Function &function, const std::string &label)
{
    for (auto &block : function.blocks)
        if (block.label == label)
            return &block;
    return nullptr;
}

void runSimplifyCFG(Module &module, Function &function)
{
    auto verified = il::verify::Verifier::verify(module);
    if (!verified)
    {
        il::support::printDiag(verified.error(), std::cerr);
    }
    ASSERT_TRUE(verified);

    il::transform::SimplifyCFG simplify;
    simplify.setModule(&module);
    simplify.run(function, nullptr);
}

/// Build a two-way branch with a block-param join to test SCCP constant folding.
Module buildConstBranchModule()
{
    Module module;
    Function fn;
    fn.name = "sccp_phi_branch";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;

    BasicBlock entry;
    entry.label = "entry";
    Instr entryBr;
    entryBr.op = Opcode::CBr;
    entryBr.type = Type(Type::Kind::Void);
    entryBr.operands.push_back(Value::constBool(true));
    entryBr.labels.push_back("left");
    entryBr.labels.push_back("right");
    entryBr.brArgs.emplace_back();
    entryBr.brArgs.emplace_back();
    entry.instructions.push_back(std::move(entryBr));
    entry.terminated = true;

    BasicBlock left;
    left.label = "left";
    Instr leftBr;
    leftBr.op = Opcode::Br;
    leftBr.type = Type(Type::Kind::Void);
    leftBr.labels.push_back("join");
    leftBr.brArgs.emplace_back(std::vector<Value>{Value::constInt(4)});
    left.instructions.push_back(std::move(leftBr));
    left.terminated = true;

    BasicBlock right;
    right.label = "right";
    Instr rightBr;
    rightBr.op = Opcode::Br;
    rightBr.type = Type(Type::Kind::Void);
    rightBr.labels.push_back("join");
    rightBr.brArgs.emplace_back(std::vector<Value>{Value::constInt(8)});
    right.instructions.push_back(std::move(rightBr));
    right.terminated = true;

    BasicBlock join;
    join.label = "join";
    Param joinParam{"phi", Type(Type::Kind::I64), nextId++};
    join.params.push_back(joinParam);
    fn.valueNames.resize(nextId);
    fn.valueNames[joinParam.id] = joinParam.name;

    Instr cmp;
    cmp.result = nextId++;
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::temp(joinParam.id));
    cmp.operands.push_back(Value::constInt(4));
    fn.valueNames.resize(nextId);
    fn.valueNames[*cmp.result] = "is_four";

    Instr joinBr;
    joinBr.op = Opcode::CBr;
    joinBr.type = Type(Type::Kind::Void);
    joinBr.operands.push_back(Value::temp(*cmp.result));
    joinBr.labels.push_back("ret_true");
    joinBr.labels.push_back("ret_false");
    joinBr.brArgs.emplace_back(std::vector<Value>{Value::temp(joinParam.id)});
    joinBr.brArgs.emplace_back(std::vector<Value>{Value::temp(joinParam.id)});

    join.instructions.push_back(std::move(cmp));
    join.instructions.push_back(std::move(joinBr));
    join.terminated = true;

    BasicBlock retTrue;
    retTrue.label = "ret_true";
    Param retParamTrue{"value", Type(Type::Kind::I64), nextId++};
    retTrue.params.push_back(retParamTrue);
    fn.valueNames.resize(nextId);
    fn.valueNames[retParamTrue.id] = retParamTrue.name;
    Instr retInstrTrue;
    retInstrTrue.op = Opcode::Ret;
    retInstrTrue.type = Type(Type::Kind::Void);
    retInstrTrue.operands.push_back(Value::temp(retParamTrue.id));
    retTrue.instructions.push_back(std::move(retInstrTrue));
    retTrue.terminated = true;

    BasicBlock retFalse;
    retFalse.label = "ret_false";
    Param retParamFalse{"fallback", Type(Type::Kind::I64), nextId++};
    retFalse.params.push_back(retParamFalse);
    fn.valueNames.resize(nextId);
    fn.valueNames[retParamFalse.id] = retParamFalse.name;
    Instr retInstrFalse;
    retInstrFalse.op = Opcode::Ret;
    retInstrFalse.type = Type(Type::Kind::Void);
    retInstrFalse.operands.push_back(Value::temp(retParamFalse.id));
    retFalse.instructions.push_back(std::move(retInstrFalse));
    retFalse.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(left));
    fn.blocks.push_back(std::move(right));
    fn.blocks.push_back(std::move(join));
    fn.blocks.push_back(std::move(retTrue));
    fn.blocks.push_back(std::move(retFalse));

    module.functions.push_back(std::move(fn));
    return module;
}

/// Build a module where the branch condition is a known trapping divide-by-zero.
Module buildTrappingConditionModule()
{
    Module module;
    Function fn;
    fn.name = "sccp_trap_guard";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;

    BasicBlock entry;
    entry.label = "entry";

    const unsigned divId = nextId++;
    Instr div;
    div.result = divId;
    div.op = Opcode::SDivChk0;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(8));
    div.operands.push_back(Value::constInt(0)); // known trap
    entry.instructions.push_back(std::move(div));

    const unsigned cmpId = nextId++;
    Instr cmp;
    cmp.result = cmpId;
    cmp.op = Opcode::ICmpEq;
    cmp.type = Type(Type::Kind::I1);
    cmp.operands.push_back(Value::temp(divId));
    cmp.operands.push_back(Value::constInt(0));
    entry.instructions.push_back(std::move(cmp));

    Instr br;
    br.op = Opcode::CBr;
    br.type = Type(Type::Kind::Void);
    br.operands.push_back(Value::temp(cmpId));
    br.labels.push_back("lhs");
    br.labels.push_back("rhs");
    br.brArgs.emplace_back();
    br.brArgs.emplace_back();
    entry.instructions.push_back(std::move(br));
    entry.terminated = true;

    BasicBlock lhs;
    lhs.label = "lhs";
    Instr retLhs;
    retLhs.op = Opcode::Ret;
    retLhs.type = Type(Type::Kind::Void);
    retLhs.operands.push_back(Value::constInt(1));
    lhs.instructions.push_back(std::move(retLhs));
    lhs.terminated = true;

    BasicBlock rhs;
    rhs.label = "rhs";
    Instr retRhs;
    retRhs.op = Opcode::Ret;
    retRhs.type = Type(Type::Kind::Void);
    retRhs.operands.push_back(Value::constInt(2));
    rhs.instructions.push_back(std::move(retRhs));
    rhs.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(lhs));
    fn.blocks.push_back(std::move(rhs));
    fn.valueNames.resize(nextId);
    fn.valueNames[divId] = "div";
    fn.valueNames[cmpId] = "cmp";
    module.functions.push_back(std::move(fn));
    return module;
}

/// Build a switch with explicit branch arguments to ensure SCCP rewrites switch
/// terminators conservatively and preserves argument forwarding.
Module buildConstantSwitchModule()
{
    Module module;
    Function fn;
    fn.name = "sccp_switch";
    fn.retType = Type(Type::Kind::I64);

    unsigned nextId = 0;

    BasicBlock entry;
    entry.label = "entry";

    Instr sw;
    sw.op = Opcode::SwitchI32;
    sw.type = Type(Type::Kind::Void);
    sw.operands.push_back(Value::constInt(3));
    sw.labels.push_back("default");
    sw.brArgs.push_back({Value::constInt(7)});
    sw.operands.push_back(Value::constInt(3));
    sw.labels.push_back("hit");
    sw.brArgs.push_back({Value::constInt(42)});
    entry.instructions.push_back(std::move(sw));
    entry.terminated = true;

    BasicBlock def;
    def.label = "default";
    Param defParam{"v", Type(Type::Kind::I64), nextId++};
    def.params.push_back(defParam);
    Instr retDef;
    retDef.op = Opcode::Ret;
    retDef.type = Type(Type::Kind::Void);
    retDef.operands.push_back(Value::temp(defParam.id));
    def.instructions.push_back(std::move(retDef));
    def.terminated = true;

    BasicBlock hit;
    hit.label = "hit";
    Param hitParam{"v", Type(Type::Kind::I64), nextId++};
    hit.params.push_back(hitParam);
    Instr retHit;
    retHit.op = Opcode::Ret;
    retHit.type = Type(Type::Kind::Void);
    retHit.operands.push_back(Value::temp(hitParam.id));
    hit.instructions.push_back(std::move(retHit));
    hit.terminated = true;

    module.functions.push_back(std::move(fn));
    Function &F = module.functions.back();
    F.blocks.push_back(std::move(entry));
    F.blocks.push_back(std::move(def));
    F.blocks.push_back(std::move(hit));
    F.valueNames.resize(nextId);
    F.valueNames[0] = "def_v";
    F.valueNames[1] = "hit_v";
    return module;
}

} // namespace

TEST(SCCP, FoldsConstantBranchAndPhi)
{
    Module module = buildConstBranchModule();
    Function &function = module.functions.front();

    il::transform::sccp(module);
    runSimplifyCFG(module, function);

    ASSERT_EQ(findBlock(function, "right"), nullptr);
    ASSERT_EQ(findBlock(function, "ret_false"), nullptr);

    bool foundConstRet = false;
    for (auto &block : function.blocks)
    {
        for (auto &instr : block.instructions)
        {
            if (instr.op != Opcode::Ret || instr.operands.empty())
                continue;
            const Value &retVal = instr.operands[0];
            ASSERT_EQ(retVal.kind, Value::Kind::ConstInt);
            EXPECT_EQ(retVal.i64, 4);
            foundConstRet = true;
        }
    }
    ASSERT_TRUE(foundConstRet);
}

TEST(SCCP, DoesNotFoldTrappingDivision)
{
    Module module = buildTrappingConditionModule();
    Function &function = module.functions.front();

    il::transform::sccp(module);
    runSimplifyCFG(module, function);

    BasicBlock *entry = findBlock(function, "entry");
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->instructions.size() >= 2U);

    Instr &div = entry->instructions[0];
    EXPECT_EQ(div.op, Opcode::SDivChk0);
    ASSERT_EQ(div.operands.size(), 2U);
    EXPECT_EQ(div.operands[1].kind, Value::Kind::ConstInt);
    EXPECT_EQ(div.operands[1].i64, 0);

    Instr &term = entry->instructions.back();
    EXPECT_EQ(term.op, Opcode::CBr);
    ASSERT_FALSE(term.operands.empty());
    EXPECT_EQ(term.operands[0].kind, Value::Kind::Temp);
}

TEST(SCCP, RewritesSwitchOnConstant)
{
    Module module = buildConstantSwitchModule();
    Function &function = module.functions.front();

    il::transform::sccp(module);
    runSimplifyCFG(module, function);

    BasicBlock *entry = findBlock(function, "entry");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->instructions.size(), 1U);
    Instr &ret = entry->instructions.back();
    if (ret.op != Opcode::Ret)
    {
        std::cerr << il::io::Serializer::toString(module, il::io::Serializer::Mode::Pretty);
    }
    ASSERT_EQ(ret.op, Opcode::Ret);
    ASSERT_FALSE(ret.operands.empty());
    EXPECT_EQ(ret.operands[0].kind, Value::Kind::ConstInt);
    EXPECT_EQ(ret.operands[0].i64, 42);

    // Default should be unreachable after SimplifyCFG.
    EXPECT_EQ(findBlock(function, "default"), nullptr);
    EXPECT_EQ(findBlock(function, "hit"), nullptr);
}

#ifndef VIPER_HAS_GTEST
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
