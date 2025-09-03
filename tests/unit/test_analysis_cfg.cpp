// File: tests/unit/test_analysis_cfg.cpp
// Purpose: Unit tests for CFG utilities.
// Key invariants: Constructs synthetic graphs to validate analysis.
// Ownership/Lifetime: Tests own all temporary graphs.
// Links: docs/dev/analysis.md
#include "il/analysis/CFG.hpp"
#include "il/core/Type.hpp"
#include "il/utils/Utils.hpp"
#include <cassert>

using namespace il::core;
using namespace il::analysis;

static Function makeDiamond()
{
    Function f;
    f.name = "f";
    f.retType = Type(Type::Kind::Void);

    BasicBlock entry{};
    entry.label = "entry";
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.labels = {"then", "else"};
    entry.instructions.push_back(cbr);
    entry.terminated = true;

    BasicBlock thenB{};
    thenB.label = "then";
    Instr br1;
    br1.op = Opcode::Br;
    br1.labels = {"merge"};
    thenB.instructions.push_back(br1);
    thenB.terminated = true;

    BasicBlock elseB{};
    elseB.label = "else";
    Instr br2;
    br2.op = Opcode::Br;
    br2.labels = {"merge"};
    elseB.instructions.push_back(br2);
    elseB.terminated = true;

    BasicBlock merge{};
    merge.label = "merge";
    Instr ret;
    ret.op = Opcode::Ret;
    merge.instructions.push_back(ret);
    merge.terminated = true;

    f.blocks = {entry, thenB, elseB, merge};
    return f;
}

int main()
{
    Function f = makeDiamond();
    CFG cfg(f);
    const auto &entry = f.blocks[0];
    const auto &thenB = f.blocks[1];
    const auto &elseB = f.blocks[2];
    const auto &merge = f.blocks[3];

    assert(cfg.succs(entry).size() == 2);
    assert(cfg.preds(thenB).size() == 1 && cfg.preds(thenB)[0] == &entry);
    assert(cfg.postOrder(merge) == 0);
    assert(cfg.postOrder(entry) == 3);
    assert(il::util::inBlock(thenB.instructions.front(), thenB));
    assert(!il::util::inBlock(thenB.instructions.front(), elseB));
    return 0;
}
