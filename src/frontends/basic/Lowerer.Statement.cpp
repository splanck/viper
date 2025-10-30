//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the sequencing routines used by the BASIC lowerer when translating a
// numbered statement list into IL basic blocks.  The helpers here manage the
// gosub continuation table and ensure fallthrough edges are emitted correctly.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Statement lowering utilities for the BASIC front end.
/// @details The @ref StatementLowering helper coordinates between numbered BASIC
///          lines and the IL block graph, wiring up branches, gosub continuations,
///          and fallthrough logic while reusing the owning @ref Lowerer state.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <cassert>

namespace il::frontends::basic
{

/// @brief Construct a lowering helper bound to the owning @ref Lowerer.
/// @details Stores a reference to the parent lowerer so helper routines can
///          access shared state such as the current lowering context, gosub
///          stacks, and basic-block tables.
/// @param lowerer Parent lowerer responsible for emitting IL.
StatementLowering::StatementLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Lower a sequential list of BASIC statements into IL blocks.
/// @details Establishes gosub continuation state, emits an initial branch from
///          the caller into the first numbered block, and then iterates over the
///          statements, lowering each in turn.  After visiting a statement the
///          helper either stops (when @p stopOnTerminated is true and a
///          terminator was emitted) or stitches a branch to the next block while
///          allowing @p beforeBranch to inject custom behaviour.
/// @param stmts Statement pointers in execution order.
/// @param stopOnTerminated When true the loop exits once a terminator is seen.
/// @param beforeBranch Optional hook invoked immediately before emitting a
///        fallthrough branch.
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
        auto *next = (i + 1 < stmts.size())
                         ? &func->blocks[lineBlocks[lowerer.virtualLine(*stmts[i + 1])]]
                         : &func->blocks[ctx.exitIndex()];
        if (beforeBranch)
            beforeBranch(stmt);
        lowerer.emitBr(next);
    }
}

void Lowerer::lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                     bool stopOnTerminated,
                                     const std::function<void(const Stmt &)> &beforeBranch)
{
    statementLowering->lowerSequence(stmts, stopOnTerminated, beforeBranch);
}

} // namespace il::frontends::basic
