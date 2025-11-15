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
#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <cassert>
#include <functional>

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

    // Note: clearContinuations() removed - gosub continuations must persist
    // across all sequences in a procedure since RETURN needs visibility of
    // ALL gosub sites, not just those in the current sequence.

    // Helper to recursively find GOSUB statements, including those nested in StmtList
    std::function<void(const Stmt *, size_t)> scanForGosub;
    scanForGosub = [&](const Stmt *stmt, size_t nextIdx)
    {
        if (const auto *gosubStmt = as<const GosubStmt>(*stmt))
        {
            ctx.gosub().registerContinuation(gosubStmt, nextIdx);
        }
        else if (const auto *stmtList = as<const StmtList>(*stmt))
        {
            // Recursively scan statements in the list
            for (const auto &childStmt : stmtList->stmts)
                scanForGosub(childStmt.get(), nextIdx);
        }
    };

    bool hasGosub = false;
    for (size_t i = 0; i < stmts.size(); ++i)
    {
        size_t contIdx = ctx.exitIndex();
        if (i + 1 < stmts.size())
        {
            int nextLine = lowerer.virtualLine(*stmts[i + 1]);
            contIdx = lineBlocks[nextLine];
        }

        // Check if this statement (or any nested statement) is a GOSUB
        if (as<const GosubStmt>(*stmts[i]) || as<const StmtList>(*stmts[i]))
        {
            scanForGosub(stmts[i], contIdx);
            hasGosub = true;
        }
    }

    if (hasGosub)
        lowerer.ensureGosubStack();
    int firstLine = lowerer.virtualLine(*stmts.front());
    lowerer.emitBr(&func->blocks[lineBlocks[firstLine]]);

    for (size_t i = 0; i < stmts.size(); ++i)
    {
        const Stmt &stmt = *stmts[i];
        int vLine = lowerer.virtualLine(stmt);

        // Check if this line is an error handler target
        auto &handlerBlocks = ctx.errorHandlers().blocks();
        auto handlerIt = handlerBlocks.find(vLine);
        il::core::BasicBlock *lineBlock = nullptr;
        if (handlerIt != handlerBlocks.end())
        {
            // This line is a handler target - use the handler block
            ctx.setCurrent(&func->blocks[handlerIt->second]);
            // Keep track of the line block so we can add a trap to it
            if (lineBlocks.find(vLine) != lineBlocks.end())
                lineBlock = &func->blocks[lineBlocks[vLine]];
        }
        else
        {
            // Normal line block
            ctx.setCurrent(&func->blocks[lineBlocks[vLine]]);
        }

        lowerer.lowerStmt(stmt);

        // If this was a handler target with a line block, add a trap to the line block
        if (lineBlock && lineBlock->instructions.empty() && !lineBlock->terminated)
        {
            auto *savedCurrent = ctx.current();
            ctx.setCurrent(lineBlock);
            lowerer.emitTrap();
            ctx.setCurrent(savedCurrent);
        }
        auto *current = ctx.current();
        if (current && current->terminated)
        {
            if (stopOnTerminated)
                break;
            continue;
        }

        // Skip automatic branching for handler blocks - control flow is via RESUME
        bool isHandlerBlock = (handlerIt != handlerBlocks.end());
        if (!isHandlerBlock)
        {
            auto *next = (i + 1 < stmts.size())
                             ? &func->blocks[lineBlocks[lowerer.virtualLine(*stmts[i + 1])]]
                             : &func->blocks[ctx.exitIndex()];
            if (beforeBranch)
                beforeBranch(stmt);
            lowerer.emitBr(next);
        }
        else
        {
            // Handler block: if no terminator was emitted, add default terminator
            auto *handlerBlock = ctx.current();
            if (handlerBlock && !handlerBlock->terminated)
            {
                // Check if this is the last statement or if the next statement is on a different
                // line
                bool isLastInHandler =
                    (i + 1 >= stmts.size()) || (lowerer.virtualLine(*stmts[i + 1]) != vLine);

                if (isLastInHandler)
                {
                    // Return from program (emitRet will pop handler if needed)
                    lowerer.emitRet(il::core::Value::constInt(0));
                }
            }
        }
    }
}

void Lowerer::lowerStatementSequence(const std::vector<const Stmt *> &stmts,
                                     bool stopOnTerminated,
                                     const std::function<void(const Stmt &)> &beforeBranch)
{
    statementLowering->lowerSequence(stmts, stopOnTerminated, beforeBranch);
}

} // namespace il::frontends::basic
