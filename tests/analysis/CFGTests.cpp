// File: tests/analysis/CFGTests.cpp
// Purpose: Verify CFG successor and predecessor utilities.
// Key invariants: Successor and predecessor sets reflect branch targets.
// Ownership/Lifetime: Builds a local module via IRBuilder.
// Links: docs/dev/analysis.md

#include "il/analysis/CFG.hpp"
#include "il/build/IRBuilder.hpp"
#include <algorithm>
#include <cassert>

int main()
{
    using namespace il::core;
    using viper::analysis::predecessors;
    using viper::analysis::successors;

    Module m;
    il::build::IRBuilder b(m);
    Function &fn = b.startFunction("f", Type(Type::Kind::Void), {});
    b.createBlock(fn, "entry");
    b.createBlock(fn, "t");
    b.createBlock(fn, "f");
    b.createBlock(fn, "join");
    Block &entry = fn.blocks[0];
    Block &t = fn.blocks[1];
    Block &f = fn.blocks[2];
    Block &join = fn.blocks[3];

    b.setInsertPoint(entry);
    b.cbr(Value::constInt(1), t, {}, f, {});

    b.setInsertPoint(t);
    b.br(join, {});

    b.setInsertPoint(f);
    b.br(join, {});

    b.setInsertPoint(join);
    b.emitRet(std::nullopt, {});

    viper::analysis::CFGContext ctx(m);

    auto sEntry = successors(ctx, entry);
    assert(sEntry.size() == 2);
    assert((sEntry[0] == &t && sEntry[1] == &f) || (sEntry[0] == &f && sEntry[1] == &t));

    auto sT = successors(ctx, t);
    assert(sT.size() == 1 && sT[0] == &join);
    auto sF = successors(ctx, f);
    assert(sF.size() == 1 && sF[0] == &join);
    auto sJoin = successors(ctx, join);
    assert(sJoin.empty());

    auto pJoin = predecessors(ctx, join);
    assert(pJoin.size() == 2);
    assert((pJoin[0] == &t && pJoin[1] == &f) || (pJoin[0] == &f && pJoin[1] == &t));

    return 0;
}
