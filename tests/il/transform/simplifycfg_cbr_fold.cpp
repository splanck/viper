// File: tests/il/transform/simplifycfg_cbr_fold.cpp
// Purpose: Verify SimplifyCFG folds unconditional conditional branches.
// Key invariants: SimplifyCFG folds constant-true cbrs and removes conditional branches.
// Ownership/Lifetime: Test owns in-memory module; no external resources.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/transform/SimplifyCFG.hpp"

#include <algorithm>
#include <cassert>

using namespace il;

namespace
{
core::Function makeFunction()
{
    core::Function fn;
    fn.name = "fold_cbr";
    fn.retType = core::Type(core::Type::Kind::Void);

    core::BasicBlock entry;
    entry.label = "entry";

    core::Instr cbr;
    cbr.op = core::Opcode::CBr;
    cbr.type = core::Type(core::Type::Kind::Void);
    cbr.operands.push_back(core::Value::constBool(true));
    cbr.labels.push_back("A");
    cbr.labels.push_back("B");
    cbr.brArgs.emplace_back();
    cbr.brArgs.emplace_back();
    entry.instructions.push_back(std::move(cbr));
    entry.terminated = true;

    core::BasicBlock a;
    a.label = "A";

    core::Instr retA;
    retA.op = core::Opcode::Ret;
    retA.type = core::Type(core::Type::Kind::Void);
    a.instructions.push_back(std::move(retA));
    a.terminated = true;

    core::BasicBlock b;
    b.label = "B";

    core::Instr retB;
    retB.op = core::Opcode::Ret;
    retB.type = core::Type(core::Type::Kind::Void);
    b.instructions.push_back(std::move(retB));
    b.terminated = true;

    fn.blocks.push_back(std::move(entry));
    fn.blocks.push_back(std::move(a));
    fn.blocks.push_back(std::move(b));

    return fn;
}

bool containsConditionalBranch(const core::Function &fn)
{
    for (const core::BasicBlock &block : fn.blocks)
    {
        auto it = std::find_if(block.instructions.begin(), block.instructions.end(), [](const core::Instr &instr) {
            return instr.op == core::Opcode::CBr;
        });
        if (it != block.instructions.end())
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    core::Module module;
    module.functions.push_back(makeFunction());

    core::Function &fn = module.functions.front();

    transform::SimplifyCFG pass;
    pass.setModule(&module);
    transform::SimplifyCFG::Stats stats{};
    bool changed = pass.run(fn, &stats);
    assert(changed);
    assert(stats.cbrToBr == 1);

    core::BasicBlock &entry = fn.blocks.front();
    assert(!entry.instructions.empty());
    const core::Instr &terminator = entry.instructions.back();
    if (terminator.op == core::Opcode::Br)
    {
        assert(terminator.labels.size() == 1);
        assert(terminator.labels.front() == "A");
    }
    else
    {
        assert(terminator.op == core::Opcode::Ret);
    }

    assert(!containsConditionalBranch(fn));

    return 0;
}
