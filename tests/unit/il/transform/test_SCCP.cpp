// File: tests/unit/il/transform/test_SCCP.cpp
// Purpose: Exercise sparse conditional constant propagation on block-parameter SSA.
// Key invariants: Constants flowing through block parameters fold branches and prune unreachable
// blocks. Ownership/Lifetime: Constructs a transient module within the test. Links:
// docs/il-guide.md#reference

#include "il/transform/SCCP.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

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
} // namespace

int main()
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
    Function &function = module.functions.back();

    il::transform::sccp(module);

    il::transform::SimplifyCFG simplify;
    simplify.setModule(&module);
    simplify.run(function, nullptr);

    assert(findBlock(function, "ret_false") == nullptr && "Unreachable block should be removed");
    assert(findBlock(function, "right") == nullptr && "Dead branch block should be removed");

    bool foundConstRet = false;
    for (auto &block : function.blocks)
    {
        for (auto &instr : block.instructions)
        {
            if (instr.op != Opcode::Ret)
                continue;
            if (instr.operands.empty())
                continue;
            const Value &retVal = instr.operands[0];
            assert(retVal.kind == Value::Kind::ConstInt && "Return should be constant after SCCP");
            assert(retVal.i64 == 4 && "Return constant should match propagated value");
            foundConstRet = true;
        }
    }
    assert(foundConstRet && "Expected a constant return after SCCP");

    return 0;
}
