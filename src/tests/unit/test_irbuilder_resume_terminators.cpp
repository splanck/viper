//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_irbuilder_resume_terminators.cpp
// Purpose: Ensure IRBuilder marks blocks terminated after emitting resume instructions. 
// Key invariants: Resume opcodes behave as terminators when emitted via IRBuilder helpers.
// Ownership/Lifetime: Test owns its module/function fixtures locally.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"

#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cassert>

int main()
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    auto &function = builder.startFunction("resume_test", Type(Type::Kind::Void), {});
    builder.addBlock(function, "resume_label_target");
    builder.addBlock(function, "resume_same");
    builder.addBlock(function, "resume_next");
    builder.addBlock(function, "resume_label");

    assert(function.blocks.size() == 4);
    BasicBlock &labelTarget = function.blocks[0];
    BasicBlock &sameBlock = function.blocks[1];
    BasicBlock &nextBlock = function.blocks[2];
    BasicBlock &labelBlock = function.blocks[3];

    auto makeToken = [&]()
    {
        unsigned id = builder.reserveTempId();
        return Value::temp(id);
    };

    auto verifyTerminated = [&](BasicBlock &block, auto emit)
    {
        builder.setInsertPoint(block);
        Value token = makeToken();
        emit(token);
        assert(block.terminated);
    };

    verifyTerminated(sameBlock, [&](Value token) { builder.emitResumeSame(token, {}); });

    verifyTerminated(nextBlock, [&](Value token) { builder.emitResumeNext(token, {}); });

    verifyTerminated(labelBlock,
                     [&](Value token)
                     {
                         builder.emitResumeLabel(token, labelTarget, {});
                         assert(!labelBlock.instructions.empty());
                         const Instr &instr = labelBlock.instructions.back();
                         assert(instr.labels.size() == 1);
                         assert(instr.labels[0] == labelTarget.label);
                     });

    return 0;
}
