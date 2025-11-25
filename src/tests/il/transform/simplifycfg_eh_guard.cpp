//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/transform/simplifycfg_eh_guard.cpp
// Purpose: Ensure SimplifyCFG preserves EH-sensitive blocks.
// Key invariants: Blocks containing EH structural ops or resume terminators remain intact.
// Ownership/Lifetime: Builds a local module and runs SimplifyCFG in place.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/SimplifyCFG.hpp"
#include "il/verify/Verifier.hpp"

#include <cassert>

int main()
{
    using namespace il::core;

    Module module;
    Function function;
    function.name = "eh_guard";
    function.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr push;
    push.op = Opcode::EhPush;
    push.type = Type(Type::Kind::Void);
    push.labels.push_back("handler");
    entry.instructions.push_back(push);

    Instr trap;
    trap.op = Opcode::Trap;
    trap.type = Type(Type::Kind::Void);
    entry.instructions.push_back(trap);
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";
    handler.params.push_back({"err", Type(Type::Kind::Error), 0});
    handler.params.push_back({"tok", Type(Type::Kind::ResumeTok), 1});

    Instr entryMarker;
    entryMarker.op = Opcode::EhEntry;
    entryMarker.type = Type(Type::Kind::Void);
    handler.instructions.push_back(entryMarker);

    Instr pop;
    pop.op = Opcode::EhPop;
    pop.type = Type(Type::Kind::Void);
    handler.instructions.push_back(pop);

    Instr resume;
    resume.op = Opcode::ResumeNext;
    resume.type = Type(Type::Kind::Void);
    resume.operands.push_back(Value::temp(handler.params[1].id));
    handler.instructions.push_back(resume);
    handler.terminated = true;

    function.blocks.push_back(entry);
    function.blocks.push_back(handler);
    module.functions.push_back(function);

    auto verifyResult = il::verify::Verifier::verify(module);
    assert(verifyResult && "Module must verify before running SimplifyCFG");

    il::transform::SimplifyCFG pass;
    pass.setModule(&module);
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(module.functions.front(), &stats);
    assert(!changed && "SimplifyCFG should not rewrite EH-sensitive blocks");

    const Function &resultFn = module.functions.front();
    assert(resultFn.blocks.size() == 2 && "EH handler block must be preserved");

    const BasicBlock &resultEntry = resultFn.blocks[0];
    assert(!resultEntry.instructions.empty());
    const Instr &resultPush = resultEntry.instructions.front();
    assert(resultPush.op == Opcode::EhPush);
    assert(resultPush.labels.size() == 1);
    assert(resultPush.labels.front() == "handler" &&
           "EH push must continue to reference the handler label");

    bool foundHandler = false;
    for (const auto &block : resultFn.blocks)
    {
        if (block.label != "handler")
            continue;

        foundHandler = true;
        assert(block.instructions.size() == 3);
        assert(block.instructions[0].op == Opcode::EhEntry);
        assert(block.instructions[1].op == Opcode::EhPop);
        assert(block.instructions[2].op == Opcode::ResumeNext);
        assert(block.instructions[2].operands.size() == 1);
        const Value &resumeTok = block.instructions[2].operands.front();
        assert(resumeTok.kind == Value::Kind::Temp);
        assert(resumeTok.id == handler.params[1].id);
    }

    assert(foundHandler && "Handler block must remain present after SimplifyCFG");

    assert(stats.cbrToBr == 0);
    assert(stats.emptyBlocksRemoved == 0);
    assert(stats.predsMerged == 0);
    assert(stats.paramsShrunk == 0);
    assert(stats.blocksMerged == 0);
    assert(stats.unreachableRemoved == 0);

    return 0;
}
