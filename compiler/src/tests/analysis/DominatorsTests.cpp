//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/analysis/DominatorsTests.cpp
// Purpose: Validate dominator tree construction and queries.
// Key invariants: Immediate dominators and dominance checks reflect CFG structure.
// Ownership/Lifetime: Builds local modules via IRBuilder.
// Links: docs/dev/analysis.md
//
//===----------------------------------------------------------------------===//

#include "il/analysis/CFG.hpp"
#include "il/analysis/Dominators.hpp"
#include "il/build/IRBuilder.hpp"
#include <cassert>

int main()
{
    using namespace il::core;
    using namespace viper::analysis;

    Module m;
    il::build::IRBuilder b(m);

    // Diamond graph: entry -> {t, f} -> join
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
        DomTree dtDiamond = computeDominatorTree(ctx, diamond);
        assert(dtDiamond.immediateDominator(&dEntry) == nullptr);
        assert(dtDiamond.immediateDominator(&dT) == &dEntry);
        assert(dtDiamond.immediateDominator(&dF) == &dEntry);
        assert(dtDiamond.immediateDominator(&dJoin) == &dEntry);
        assert(dtDiamond.dominates(&dEntry, &dT));
        assert(dtDiamond.dominates(&dEntry, &dF));
        assert(dtDiamond.dominates(&dEntry, &dJoin));
        assert(!dtDiamond.dominates(&dT, &dF));
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
        DomTree dtChain = computeDominatorTree(ctx, chain);
        assert(dtChain.immediateDominator(&A) == nullptr);
        assert(dtChain.immediateDominator(&B) == &A);
        assert(dtChain.immediateDominator(&C) == &B);
        assert(dtChain.dominates(&A, &B));
        assert(dtChain.dominates(&A, &C));
    }

    return 0;
}
