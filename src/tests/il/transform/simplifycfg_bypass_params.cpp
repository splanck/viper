//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_bypass_params.cpp
// Purpose: Verify SimplifyCFG forwards branch arguments when bypassing blocks with params. 
// Key invariants: Forwarding block removal must preserve branch arguments and remove the block.
// Ownership/Lifetime: Constructs a local module and runs the pass by value.
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

    Function &fn = builder.startFunction("bypass", Type(Type::Kind::I64), {});
    BasicBlock &entry = builder.createBlock(fn, "entry");
    BasicBlock &mid = builder.createBlock(fn, "mid", {Param{"p", Type(Type::Kind::I64), 0}});
    BasicBlock &exit = builder.createBlock(fn, "exit", {Param{"result", Type(Type::Kind::I64), 0}});

    builder.setInsertPoint(entry);
    builder.br(mid, {Value::constInt(7)});

    builder.setInsertPoint(mid);
    builder.br(exit, {builder.blockParam(mid, 0)});

    builder.setInsertPoint(exit);
    builder.emitRet(std::optional<Value>{builder.blockParam(exit, 0)}, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    assert(changed && "SimplifyCFG should remove the forwarding block");
    assert(stats.predsMerged == 1 && "Expected a single predecessor redirection");
    assert(stats.emptyBlocksRemoved == 1 && "Expected the forwarding block to be removed");

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
    assert(entryBlock && "Entry block must remain");
    const BasicBlock *exitBlock = findBlock(fn, "exit");
    assert(exitBlock && "Exit block must remain");
    const BasicBlock *midBlock = findBlock(fn, "mid");
    assert(!midBlock && "Forwarding block should be removed");

    assert(!entryBlock->instructions.empty());
    const Instr &entryTerm = entryBlock->instructions.back();
    assert(entryTerm.op == Opcode::Br && "Entry must branch directly to exit");
    assert(entryTerm.labels.size() == 1 && entryTerm.labels.front() == exitBlock->label);
    assert(entryTerm.brArgs.size() == 1 && entryTerm.brArgs.front().size() == 1);
    const Value &bypassedArg = entryTerm.brArgs.front().front();
    assert(bypassedArg.kind == Value::Kind::ConstInt);
    assert(bypassedArg.i64 == 7 && "Branch argument should be forwarded");

    assert(exitBlock->params.size() == 1 && "Exit block must retain its parameter");

    return 0;
}
