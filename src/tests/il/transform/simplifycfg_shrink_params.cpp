//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_shrink_params.cpp
// Purpose: Ensure SimplifyCFG drops block params that are identical across predecessors. 
// Key invariants: Shared param is replaced by common value and removed from param list.
// Ownership/Lifetime: Constructs a local module and applies the pass in place.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>
#include <optional>
#include <string>

int main()
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction(
        "shrink_params", Type(Type::Kind::I64), {Param{"flag", Type(Type::Kind::I1), 0}});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "left");
    builder.createBlock(fn, "right");
    builder.createBlock(
        fn, "join", {Param{"a", Type(Type::Kind::I64), 0}, Param{"b", Type(Type::Kind::I64), 0}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &left = fn.blocks[1];
    BasicBlock &right = fn.blocks[2];
    BasicBlock &join = fn.blocks[3];

    builder.setInsertPoint(entry);
    const Value flag = Value::temp(fn.params[0].id);
    builder.cbr(flag, left, {}, right, {});

    builder.setInsertPoint(left);
    builder.br(join, {Value::constInt(99), Value::constInt(1)});

    builder.setInsertPoint(right);
    builder.br(join, {Value::constInt(99), Value::constInt(2)});

    builder.setInsertPoint(join);
    const unsigned sumId = builder.reserveTempId();
    Instr sum;
    sum.result = sumId;
    sum.op = Opcode::IAddOvf;
    sum.type = Type(Type::Kind::I64);
    sum.operands.push_back(builder.blockParam(join, 0));
    sum.operands.push_back(builder.blockParam(join, 1));
    join.instructions.push_back(sum);

    builder.emitRet(std::optional<Value>{Value::temp(sumId)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    assert(changed && "SimplifyCFG should remove redundant block parameters");
    assert(stats.paramsShrunk == 1 && "Expected a single parameter to be removed");

    const auto findBlock = [](const Function &function,
                              const std::string &label) -> const BasicBlock *
    {
        for (const auto &block : function.blocks)
        {
            if (block.label == label)
                return &block;
        }
        return nullptr;
    };

    const BasicBlock *joinBlock = findBlock(fn, "join");
    assert(joinBlock && "Join block must remain");
    assert(joinBlock->params.size() == 1 && "Join should retain only the varying parameter");

    assert(!joinBlock->instructions.empty());
    const Instr &sumInstr = joinBlock->instructions.front();
    assert(sumInstr.operands.size() == 2 && "Addition should have two operands");
    const Value &firstOperand = sumInstr.operands[0];
    assert(firstOperand.kind == Value::Kind::ConstInt);
    assert(firstOperand.i64 == 99 &&
           "Canonicalized parameter should be replaced with constant value");
    const Value &secondOperand = sumInstr.operands[1];
    assert(secondOperand.kind == Value::Kind::Temp);
    assert(secondOperand.id == joinBlock->params[0].id &&
           "Remaining operand should reference the surviving block parameter");

    const BasicBlock *entryBlock = findBlock(fn, "entry");
    assert(entryBlock && !entryBlock->instructions.empty());
    const Instr &entryTerm = entryBlock->instructions.back();
    assert(entryTerm.op == Opcode::CBr);
    assert(entryTerm.labels.size() == 2);
    assert(entryTerm.labels[0] == "join" && entryTerm.labels[1] == "join");
    assert(entryTerm.brArgs.size() == 2);
    assert(entryTerm.brArgs[0].size() == 1);
    assert(entryTerm.brArgs[1].size() == 1);
    const Value &trueArg = entryTerm.brArgs[0][0];
    const Value &falseArg = entryTerm.brArgs[1][0];
    assert(trueArg.kind == Value::Kind::ConstInt && trueArg.i64 == 1);
    assert(falseArg.kind == Value::Kind::ConstInt && falseArg.i64 == 2);

    return 0;
}
