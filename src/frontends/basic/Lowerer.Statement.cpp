// File: src/frontends/basic/Lowerer.Statement.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements statement-sequencing helpers that connect numbered BASIC
//          lines to IL basic blocks and manage gosub continuations.
// Key invariants: Statement lowering respects per-procedure block numbering and
//                 terminator semantics.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <cassert>

namespace il::frontends::basic
{

StatementLowering::StatementLowering(Lowerer &lowerer) : lowerer(lowerer) {}

void StatementLowering::lowerSequence(const std::vector<const Stmt *> &stmts,
                                      bool stopOnTerminated,
                                      const std::function<void(const Stmt &)> &beforeBranch)
{
    if (stmts.empty())
        return;

    lowerer.curLoc = {};
    auto &ctx = lowerer.context();
    auto *func = ctx.function();
    assert(func && "lowerSequence requires an active function");
    auto &lineBlocks = ctx.blockNames().lineBlocks();

    ctx.gosub().clearContinuations();
    bool hasGosub = false;
    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const auto *gosubStmt = dynamic_cast<const GosubStmt *>(stmts[i]);
        if (!gosubStmt)
            continue;

        hasGosub = true;
        size_t contIdx = ctx.exitIndex();
        if (i + 1 < stmts.size())
        {
            int nextLine = lowerer.virtualLine(*stmts[i + 1]);
            contIdx = lineBlocks[nextLine];
        }
        ctx.gosub().registerContinuation(gosubStmt, contIdx);
    }

    if (hasGosub)
        lowerer.ensureGosubStack();
    int firstLine = lowerer.virtualLine(*stmts.front());
    lowerer.emitBr(&func->blocks[lineBlocks[firstLine]]);

    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const Stmt &stmt = *stmts[i];
        int vLine = lowerer.virtualLine(stmt);
        ctx.setCurrent(&func->blocks[lineBlocks[vLine]]);
        lowerer.lowerStmt(stmt);
        auto *current = ctx.current();
        if (current && current->terminated)
        {
            if (stopOnTerminated)
                break;
            continue;
        }
        auto *next =
            (i + 1 < stmts.size())
                ? &func->blocks[lineBlocks[lowerer.virtualLine(*stmts[i + 1])]]
                : &func->blocks[ctx.exitIndex()];
        if (beforeBranch)
            beforeBranch(stmt);
        lowerer.emitBr(next);
    }
}

} // namespace il::frontends::basic

