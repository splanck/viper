//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_cbr_fold.cpp
// Purpose: Exercise SimplifyCFG folding of constant conditional branches.
// Key invariants: Conditional branch with constant true becomes unconditional.
// Ownership/Lifetime: Constructs a local module and runs a pass by value.
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

    Function &fn = builder.startFunction("fold", Type(Type::Kind::Void), {});
    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "A");
    builder.createBlock(fn, "B");

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &blockA = fn.blocks[1];
    BasicBlock &blockB = fn.blocks[2];

    builder.setInsertPoint(entry);
    builder.cbr(Value::constBool(true), blockA, {}, blockB, {});

    builder.setInsertPoint(blockA);
    builder.emitRet(std::nullopt, {});

    builder.setInsertPoint(blockB);
    builder.emitRet(std::nullopt, {});

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module should verify before SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    assert(changed && "SimplifyCFG should fold the trivial conditional branch");
    assert(stats.cbrToBr == 1 && "Expected exactly one conditional branch fold");

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
    assert(entryBlock);
    assert(!entryBlock->instructions.empty());
    const Instr &terminator = entryBlock->instructions.back();
    // SimplifyCFG may immediately merge the entry block into A after folding the
    // branch, so accept either an explicit branch to ^A or a direct return.
    const bool isFoldedBranch = terminator.op == Opcode::Br && !terminator.labels.empty() &&
                                terminator.labels.front() == "A";
    const bool isMergedRet = terminator.op == Opcode::Ret;
    assert((isFoldedBranch || isMergedRet) && "Entry should branch to A or merge into its return");

    for (const auto &block : fn.blocks)
        for (const auto &instr : block.instructions)
            assert(instr.op != Opcode::CBr);

    return 0;
}
