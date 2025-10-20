// File: tests/il/transform/simplifycfg_missing_br_args.cpp
// Purpose: Regression for SimplifyCFG when predecessors omit branch arguments.
// Key invariants: Pass should tolerate missing argument vectors without crashing.
// Ownership/Lifetime: Constructs IR locally and runs SimplifyCFG in place.
// Links: docs/il-guide.md#reference

#include "il/build/IRBuilder.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include <cassert>
#include <optional>

int main()
{
    using namespace il::core;

    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction("missing_args",
                                         Type(Type::Kind::I64),
                                         {});

    builder.createBlock(fn, "entry");
    builder.createBlock(fn, "target", {Param{"x", Type(Type::Kind::I64), 0}});

    BasicBlock &entry = fn.blocks[0];
    BasicBlock &target = fn.blocks[1];

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(7)});

    Instr &entryTerm = entry.instructions.back();
    assert(entryTerm.op == Opcode::Br);
    entryTerm.brArgs.clear();

    builder.setInsertPoint(target);
    builder.emitRet(std::optional<Value>{builder.blockParam(target, 0)}, {});

    il::transform::SimplifyCFG pass;
    // Intentionally skip providing the module to bypass debug verification: the
    // constructed IR is deliberately inconsistent to exercise recovery paths.
    il::transform::SimplifyCFG::Stats stats{};
    const bool changed = pass.run(fn, &stats);
    (void)changed;

    assert(entryTerm.labels.size() == 1);
    assert(entryTerm.brArgs.size() == entryTerm.labels.size());
    assert(entryTerm.brArgs[0].empty() &&
           "Missing argument entry should be materialised as an empty vector");

    assert(target.params.size() == 1 && "Target block parameters must remain intact");

    return 0;
}
