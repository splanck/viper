// File: tests/unit/test_il_analysis.cpp
// Purpose: Verify CFG and dominator utilities on synthetic graphs.
// Key invariants: Dominator relationships are deterministic.
// Ownership/Lifetime: Test owns all IR objects locally.
// Links: docs/dev/analysis.md

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include <cassert>

using namespace il;

static core::Function makeDiamond()
{
    core::Function f;
    f.name = "diamond";
    f.retType = core::Type(core::Type::Kind::Void);

    core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;
    core::Instr brEntry;
    brEntry.op = core::Opcode::CBr;
    brEntry.type = core::Type(core::Type::Kind::Void);
    brEntry.operands = {core::Value::temp(0)};
    brEntry.labels = {"left", "right"};
    brEntry.brArgs = {{}, {}};
    entry.instructions.push_back(brEntry);
    f.blocks.push_back(entry);

    core::BasicBlock left;
    left.label = "left";
    left.terminated = true;
    core::Instr brLeft;
    brLeft.op = core::Opcode::Br;
    brLeft.type = core::Type(core::Type::Kind::Void);
    brLeft.labels = {"merge"};
    brLeft.brArgs = {{}};
    left.instructions.push_back(brLeft);
    f.blocks.push_back(left);

    core::BasicBlock right;
    right.label = "right";
    right.terminated = true;
    core::Instr brRight = brLeft;
    right.instructions.push_back(brRight);
    f.blocks.push_back(right);

    core::BasicBlock merge;
    merge.label = "merge";
    merge.terminated = true;
    core::Instr ret;
    ret.op = core::Opcode::Ret;
    ret.type = core::Type(core::Type::Kind::Void);
    merge.instructions.push_back(ret);
    f.blocks.push_back(merge);

    return f;
}

int main()
{
    core::Function diamond = makeDiamond();
    analysis::CFG cfg(diamond);
    assert(cfg.successors(diamond.blocks[0]).size() == 2);
    assert(cfg.predecessors(diamond.blocks[3]).size() == 2);
    assert(cfg.postOrder().back() == &diamond.blocks[0]);

    analysis::DominatorTree dom(cfg);
    assert(dom.dominates(diamond.blocks[0], diamond.blocks[1]));
    assert(!dom.dominates(diamond.blocks[1], diamond.blocks[2]));
    assert(dom.idom(diamond.blocks[3]) == &diamond.blocks[0]);
    return 0;
}
