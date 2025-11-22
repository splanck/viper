//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_merge_single_pred.cpp
// Purpose: Validate SimplifyCFG merges single-predecessor blocks into their parent. 
// Key invariants: Instructions from the merged block relocate to the predecessor and the block is
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <vector>

int main()
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction(
        "merge_single_pred", Type(Type::Kind::I64), {Param{"x", Type(Type::Kind::I64), 0}});

    builder.createBlock(fn, "entry");
    const std::vector<Param> midParams{Param{"v", Type(Type::Kind::I64), 0}};
    builder.createBlock(fn, "mid", midParams);
    const std::vector<Param> exitParams{Param{"result", Type(Type::Kind::I64), 0}};
    builder.createBlock(fn, "exit", exitParams);

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &mid = fn.blocks[1];
    BasicBlock &exit = fn.blocks[2];

    assert(mid.params.size() == 1 && "Mid block must expose its parameter");
    assert(exit.params.size() == 1 && "Exit block must expose its parameter");

    builder.setInsertPoint(entry);
    const Value paramX = Value::temp(fn.params[0].id);
    builder.br(mid, {paramX});

    builder.setInsertPoint(mid);
    const unsigned addId = builder.reserveTempId();
    Instr addInstr;
    addInstr.result = addId;
    addInstr.op = Opcode::IAddOvf;
    addInstr.type = Type(Type::Kind::I64);
    addInstr.operands.push_back(builder.blockParam(mid, 0));
    addInstr.operands.push_back(Value::constInt(5));
    mid.instructions.push_back(addInstr);

    const unsigned mulId = builder.reserveTempId();
    Instr mulInstr;
    mulInstr.result = mulId;
    mulInstr.op = Opcode::IMulOvf;
    mulInstr.type = Type(Type::Kind::I64);
    mulInstr.operands.push_back(Value::temp(addId));
    mulInstr.operands.push_back(Value::constInt(2));
    mid.instructions.push_back(mulInstr);

    builder.br(exit, {Value::temp(mulId)});

    builder.setInsertPoint(exit);
    builder.emitRet(std::optional<Value>{builder.blockParam(exit, 0)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    assert(changed && "SimplifyCFG should merge the single-predecessor block");
    assert(stats.blocksMerged >= 1 && "Expected at least one block merge to occur");

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

    const BasicBlock *entryBlock = findBlock(fn, "entry");
    assert(entryBlock && "Entry block must remain after merging");
    const BasicBlock *exitBlock = findBlock(fn, "exit");
    const BasicBlock *midBlock = findBlock(fn, "mid");
    assert(!midBlock && "Mid block should be removed after merging");

    assert(entryBlock->instructions.size() >= 3);
    const Instr &firstInstr = entryBlock->instructions[0];
    assert(firstInstr.op == Opcode::IAddOvf && "First instruction should be the hoisted addition");
    assert(firstInstr.result.has_value());
    assert(firstInstr.operands.size() == 2);
    const Value &firstLhs = firstInstr.operands[0];
    assert(firstLhs.kind == Value::Kind::Temp);
    assert(firstLhs.id == fn.params[0].id && "Addition must use the incoming function parameter");
    const Value &firstRhs = firstInstr.operands[1];
    assert(firstRhs.kind == Value::Kind::ConstInt && firstRhs.i64 == 5);

    const Instr &secondInstr = entryBlock->instructions[1];
    assert(secondInstr.op == Opcode::IMulOvf &&
           "Second instruction should be the hoisted multiply");
    assert(secondInstr.result.has_value());
    assert(secondInstr.operands.size() == 2);
    const Value &mulLhs = secondInstr.operands[0];
    assert(mulLhs.kind == Value::Kind::Temp);
    assert(mulLhs.id == *firstInstr.result && "Multiply must consume the addition result");
    const Value &mulRhs = secondInstr.operands[1];
    assert(mulRhs.kind == Value::Kind::ConstInt && mulRhs.i64 == 2);

    const Instr &entryTerm = entryBlock->instructions.back();
    if (exitBlock)
    {
        assert(entryTerm.op == Opcode::Br && "Entry must branch directly to exit");
        assert(entryTerm.labels.size() == 1 && entryTerm.labels.front() == exitBlock->label);
        assert(entryTerm.brArgs.size() == 1);
        assert(entryTerm.brArgs.front().size() == 1);
        const Value &branchArg = entryTerm.brArgs.front().front();
        assert(branchArg.kind == Value::Kind::Temp);
        assert(branchArg.id == *secondInstr.result &&
               "Branch argument should forward the multiply result");
        assert(exitBlock->params.size() == 1 && "Exit block must retain its parameter");
    }
    else
    {
        assert(entryTerm.op == Opcode::Ret && "Entry should return directly when exit is merged");
        assert(entryTerm.operands.size() == 1);
        const Value &retValue = entryTerm.operands.front();
        assert(retValue.kind == Value::Kind::Temp);
        assert(retValue.id == *secondInstr.result && "Return value should use the multiply result");
    }

    return 0;
}
