//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/analysis/GraphOrderTests.cpp
// Purpose: Verify post-order and reverse-post-order traversals.
// Key invariants: Entry is last in post-order and first in RPO; each block appears once.
// Ownership/Lifetime: Builds local modules via IRBuilder.
// Links: docs/dev/analysis.md
//
//===----------------------------------------------------------------------===//

#include "il/analysis/CFG.hpp"
#include "il/build/IRBuilder.hpp"
#include <algorithm>
#include <cassert>
#include <unordered_set>

using namespace il::core;
using namespace viper::analysis;

static void checkOrders(const CFGContext &ctx, Function &fn)
{
    auto po = postOrder(ctx, fn);
    auto rpo = reversePostOrder(ctx, fn);

    assert(po.size() == fn.blocks.size());
    assert(rpo.size() == fn.blocks.size());

    std::unordered_set<Block *> s1(po.begin(), po.end());
    std::unordered_set<Block *> s2(rpo.begin(), rpo.end());
    assert(s1.size() == po.size());
    assert(s2.size() == rpo.size());

    assert(po.back() == &fn.blocks.front());
    assert(rpo.front() == &fn.blocks.front());
}

int main()
{
    Module m;
    il::build::IRBuilder b(m);

    // Diamond: entry -> {t,f} -> join
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
        checkOrders(ctx, diamond);
    }

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
        checkOrders(ctx, chain);
    }

    return 0;
}
