//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/analysis/AcyclicTests.cpp
// Purpose: Verify cycle detection and topological ordering for CFGs.
// Key invariants: topoOrder returns empty on cycles; order respects DAG edges.
// Ownership/Lifetime: Builds local modules via IRBuilder.
// Links: docs/dev/analysis.md
//
//===----------------------------------------------------------------------===//

#include "il/analysis/CFG.hpp"
#include "il/build/IRBuilder.hpp"
#include <cassert>

using namespace il::core;
using namespace viper::analysis;

int main()
{
    Module m;
    il::build::IRBuilder b(m);

    // Linear chain: A -> B -> C
    Function &chain = b.startFunction("chain", Type(Type::Kind::Void), {});
    b.createBlock(chain, "A");
    b.createBlock(chain, "B");
    b.createBlock(chain, "C");
    Block &A = chain.blocks[0];
    Block &B = chain.blocks[1];
    Block &C = chain.blocks[2];
    b.setInsertPoint(A);
    b.br(B, {});
    b.setInsertPoint(B);
    b.br(C, {});
    b.setInsertPoint(C);
    b.emitRet(std::nullopt, {});

    {
        CFGContext ctx(m);
        assert(isAcyclic(ctx, chain));
        auto chainOrder = topoOrder(ctx, chain);
        assert(chainOrder.size() == 3);
        assert(chainOrder[0] == &A && chainOrder[1] == &B && chainOrder[2] == &C);
    }

    // Diamond: entry -> {t, f} -> join
    Function &diamond = b.startFunction("diamond", Type(Type::Kind::Void), {});
    b.createBlock(diamond, "entry");
    b.createBlock(diamond, "t");
    b.createBlock(diamond, "f");
    b.createBlock(diamond, "join");
    Block &dEntry = diamond.blocks[0];
    Block &dT = diamond.blocks[1];
    Block &dF = diamond.blocks[2];
    Block &dJoin = diamond.blocks[3];
    b.setInsertPoint(dEntry);
    b.cbr(Value::constInt(1), dT, {}, dF, {});
    b.setInsertPoint(dT);
    b.br(dJoin, {});
    b.setInsertPoint(dF);
    b.br(dJoin, {});
    b.setInsertPoint(dJoin);
    b.emitRet(std::nullopt, {});

    {
        CFGContext ctx(m);
        assert(isAcyclic(ctx, diamond));
        auto diamondOrder = topoOrder(ctx, diamond);
        assert(diamondOrder.size() == 4);
        assert(diamondOrder.front() == &dEntry);
        assert(diamondOrder.back() == &dJoin);
    }

    // Loop: entry -> loop -> loop (cycle)
    Function &loopFn = b.startFunction("loop", Type(Type::Kind::Void), {});
    b.createBlock(loopFn, "entry");
    b.createBlock(loopFn, "loop");
    Block &lEntry = loopFn.blocks[0];
    Block &lLoop = loopFn.blocks[1];
    b.setInsertPoint(lEntry);
    b.br(lLoop, {});
    b.setInsertPoint(lLoop);
    b.br(lLoop, {});
    {
        CFGContext ctx(m);
        assert(!isAcyclic(ctx, loopFn));
        auto loopOrder = topoOrder(ctx, loopFn);
        assert(loopOrder.empty());
    }

    return 0;
}
