// File: tests/unit/test_analysis_cfg_dominators.cpp
// Purpose: Exercise CFG and dominator analyses on small graphs.
// Key invariants: Dominator relationships follow graph structure.
// Ownership/Lifetime: Builds temporary modules via IRBuilder.
// Links: docs/dev/analysis.md

#include "analysis/CFG.hpp"
#include "analysis/Dominators.hpp"
#include "il/Utils.hpp"
#include "il/build/IRBuilder.hpp"
#include <cassert>
#include <optional>

using namespace il::core;

void testDiamond()
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("diamond", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry");
    b.createBlock(fn, "t");
    b.createBlock(fn, "f");
    b.createBlock(fn, "join");
    BasicBlock &entry = fn.blocks[0];
    BasicBlock &t = fn.blocks[1];
    BasicBlock &f = fn.blocks[2];
    BasicBlock &join = fn.blocks[3];

    b.setInsertPoint(entry);
    b.cbr(Value::constInt(1), t, {}, f, {});

    b.setInsertPoint(t);
    b.br(join);

    b.setInsertPoint(f);
    b.br(join);

    b.setInsertPoint(join);
    b.emitRet(std::nullopt, {});

    il::analysis::CFG cfg(fn);
    il::analysis::DominatorTree dom(cfg);

    assert(dom.dominates(entry, t));
    assert(dom.dominates(entry, f));
    assert(!dom.dominates(t, f));
    assert(dom.idom(t) == &entry);
    assert(dom.idom(f) == &entry);
    assert(dom.idom(join) == &entry);

    const Instr &br = entry.instructions.back();
    assert(il::util::isInBlock(entry, br));
    assert(il::util::findBlock(fn, br) == &entry);
}

void testLoop()
{
    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("loop", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry");
    b.createBlock(fn, "loop");
    b.createBlock(fn, "exit");
    BasicBlock &entry = fn.blocks[0];
    BasicBlock &loop = fn.blocks[1];
    BasicBlock &exit = fn.blocks[2];

    b.setInsertPoint(entry);
    b.br(loop);

    b.setInsertPoint(loop);
    b.cbr(Value::constInt(0), loop, {}, exit, {});

    b.setInsertPoint(exit);
    b.emitRet(std::nullopt, {});

    il::analysis::CFG cfg(fn);
    il::analysis::DominatorTree dom(cfg);

    assert(dom.dominates(entry, loop));
    assert(dom.dominates(entry, exit));
    assert(dom.idom(loop) == &entry);
    assert(dom.idom(exit) == &loop);
}

int main()
{
    testDiamond();
    testLoop();
    return 0;
}
