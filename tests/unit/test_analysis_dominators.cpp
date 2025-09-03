// File: tests/unit/test_analysis_dominators.cpp
// Purpose: Unit tests for dominator tree construction.
// Key invariants: Uses synthetic CFGs covering diamonds and loops.
// Ownership/Lifetime: Tests manage temporary graphs.
// Links: docs/dev/analysis.md
#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/core/Type.hpp"
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

static Function makeLoop()
{
    Function f;
    f.name = "loop";
    f.retType = Type(Type::Kind::Void);
    BasicBlock entry{};
    entry.label = "entry";
    Instr br;
    br.op = Opcode::Br;
    br.labels = {"hdr"};
    entry.instructions.push_back(br);
    entry.terminated = true;
    BasicBlock hdr{};
    hdr.label = "hdr";
    Instr cbr;
    cbr.op = Opcode::CBr;
    cbr.labels = {"body", "exit"};
    hdr.instructions.push_back(cbr);
    hdr.terminated = true;
    BasicBlock body{};
    body.label = "body";
    Instr br2;
    br2.op = Opcode::Br;
    br2.labels = {"hdr"};
    body.instructions.push_back(br2);
    body.terminated = true;
    BasicBlock exit{};
    exit.label = "exit";
    Instr ret;
    ret.op = Opcode::Ret;
    exit.instructions.push_back(ret);
    exit.terminated = true;
    f.blocks = {entry, hdr, body, exit};
    return f;
}

int main()
{
    {
        Function f = makeDiamond();
        CFG cfg(f);
        DominatorTree dt(cfg);
        const auto &entry = f.blocks[0];
        const auto &thenB = f.blocks[1];
        const auto &elseB = f.blocks[2];
        const auto &merge = f.blocks[3];
        assert(dt.idom(entry) == nullptr);
        assert(dt.idom(thenB) == &entry);
        assert(dt.idom(elseB) == &entry);
        assert(dt.idom(merge) == &entry);
        assert(dt.dominates(entry, merge));
        assert(!dt.dominates(thenB, elseB));
    }
    {
        Function f = makeLoop();
        CFG cfg(f);
        DominatorTree dt(cfg);
        const auto &entry = f.blocks[0];
        const auto &hdr = f.blocks[1];
        const auto &body = f.blocks[2];
        const auto &exit = f.blocks[3];
        assert(dt.idom(entry) == nullptr);
        assert(dt.idom(hdr) == &entry);
        assert(dt.idom(body) == &hdr);
        assert(dt.idom(exit) == &hdr);
        assert(dt.dominates(hdr, body));
        assert(!dt.dominates(body, exit));
    }
    return 0;
}
