//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Lower_If.cpp
// Purpose: Lower BASIC IF/ELSEIF/ELSE constructs into IL control flow by
//          allocating test, body, and merge blocks.
// Key invariants: Generated block sequences preserve source order and ensure
//                 exactly one terminator per block; phi operands are prepared by
//                 the caller once @ref CtrlState is returned.
// Ownership/Lifetime: Helpers borrow the @ref Lowerer state and manipulate IL
//                     blocks owned by the active procedure context.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements IF/ELSEIF/ELSE lowering helpers for the BASIC front end.
/// @details These routines construct deterministic block layouts for chained
/// conditionals, evaluate branch conditions, and materialise merge blocks while
/// preserving the active lowering context. They operate on Lowerer directly so
/// diagnostics and terminators remain consistent across translation units.

#include "frontends/basic/Lowerer.hpp"

#include <cassert>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Allocate the block layout required for an IF/ELSEIF/ELSE chain.
///
/// @details Reserves pairs of test/then blocks for each condition plus shared
///          else and exit blocks.  The helper preserves the caller's current
///          block and returns indices so subsequent lowering stages can patch in
///          phi arguments.
/// @param conds Number of condition arms (including the initial IF).
/// @return Structure containing indices for all generated blocks.
Lowerer::IfBlocks Lowerer::emitIfBlocks(size_t conds)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    assert(func && ctx.current());
    BlockNamer *blockNamer = ctx.blockNames().namer();
    size_t curIdx = static_cast<size_t>(ctx.current() - &func->blocks[0]);
    size_t start = func->blocks.size();
    unsigned firstId = 0;
    for (size_t i = 0; i < conds; ++i)
    {
        unsigned id = blockNamer ? blockNamer->nextIf() : static_cast<unsigned>(i);
        if (i == 0)
            firstId = id;
        std::string testLbl = blockNamer ? blockNamer->generic("if_test")
                                         : mangler.block("if_test_" + std::to_string(i));
        std::string thenLbl = blockNamer ? blockNamer->ifThen(id)
                                         : mangler.block("if_then_" + std::to_string(i));
        builder->addBlock(*func, testLbl);
        builder->addBlock(*func, thenLbl);
    }
    std::string elseLbl = blockNamer ? blockNamer->ifElse(firstId) : mangler.block("if_else");
    std::string endLbl = blockNamer ? blockNamer->ifEnd(firstId) : mangler.block("if_exit");
    builder->addBlock(*func, elseLbl);
    builder->addBlock(*func, endLbl);
    ctx.setCurrent(&func->blocks[curIdx]);
    std::vector<size_t> testIdx(conds);
    std::vector<size_t> thenIdx(conds);
    for (size_t i = 0; i < conds; ++i)
    {
        testIdx[i] = start + 2 * i;
        thenIdx[i] = start + 2 * i + 1;
    }
    size_t elseIdx = start + 2 * conds;
    size_t exitIdx = start + 2 * conds + 1;
    return {std::move(testIdx), std::move(thenIdx), elseIdx, exitIdx};
}

/// @brief Emit the conditional branch for a single IF/ELSEIF test.
///
/// @details Switches the active block to @p testBlk, lowers the boolean
///          expression, and emits a conditional branch that jumps to
///          @p thenBlk on success or @p falseBlk otherwise.  The caller is
///          responsible for ensuring @p testBlk already exists in the function.
/// @param cond Expression controlling the branch.
/// @param testBlk Block that should contain the branch instruction.
/// @param thenBlk Destination block when the condition evaluates to true.
/// @param falseBlk Destination block when the condition evaluates to false.
/// @param loc Source location used for diagnostics within nested lowering.
void Lowerer::lowerIfCondition(const Expr &cond,
                               BasicBlock *testBlk,
                               BasicBlock *thenBlk,
                               BasicBlock *falseBlk,
                               il::support::SourceLoc loc)
{
    context().setCurrent(testBlk);
    lowerCondBranch(cond, thenBlk, falseBlk, loc);
}

/// @brief Lower the body of an IF/ELSEIF/ELSE branch and ensure control-flow continuity.
///
/// @details Sets the active block to @p thenBlk, lowers @p stmt when present,
///          and emits a branch to @p exitBlk when the body finishes without a
///          terminator.  The return value indicates whether control falls through
///          to the exit block, guiding the caller's phi construction.
/// @param stmt Statement tree executed when the branch is taken (may be null).
/// @param thenBlk Block receiving the branch body.
/// @param exitBlk Merge block for the entire IF construct.
/// @param loc Source location applied to any implicit branch.
/// @return @c true if the lowered branch reaches @p exitBlk.
bool Lowerer::lowerIfBranch(const Stmt *stmt,
                            BasicBlock *thenBlk,
                            BasicBlock *exitBlk,
                            il::support::SourceLoc loc)
{
    context().setCurrent(thenBlk);
    if (stmt)
        lowerStmt(*stmt);
    BasicBlock *current = context().current();
    if (current && !current->terminated)
    {
        curLoc = loc;
        emitBr(exitBlk);
        return true;
    }
    return false;
}

/// @brief Lower a full IF statement including chained ELSEIF and ELSE blocks.
///
/// @details Allocates the necessary block structure, evaluates each condition,
///          and lowers all branch bodies while tracking whether control reaches
///          the final merge block.  Returns a @ref CtrlState capturing the block
///          that should remain current once lowering finishes.
/// @param stmt AST node representing the entire IF statement.
/// @return Control-flow state describing the resulting CFG configuration.
Lowerer::CtrlState Lowerer::emitIf(const IfStmt &stmt)
{
    CtrlState state{};
    auto &ctx = context();
    auto *func = ctx.function();
    auto *current = ctx.current();
    if (!func || !current)
        return state;

    const size_t conds = 1 + stmt.elseifs.size();
    IfBlocks blocks = emitIfBlocks(conds);

    std::vector<const Expr *> condExprs;
    std::vector<const Stmt *> thenStmts;
    condExprs.reserve(conds);
    thenStmts.reserve(conds);
    condExprs.push_back(stmt.cond.get());
    thenStmts.push_back(stmt.then_branch.get());
    for (const auto &e : stmt.elseifs)
    {
        condExprs.push_back(e.cond.get());
        thenStmts.push_back(e.then_branch.get());
    }

    func = ctx.function();
    curLoc = stmt.loc;
    emitBr(&func->blocks[blocks.tests[0]]);

    bool fallthrough = false;
    for (size_t i = 0; i < conds; ++i)
    {
        func = ctx.function();
        auto *testBlk = &func->blocks[blocks.tests[i]];
        auto *thenBlk = &func->blocks[blocks.thens[i]];
        auto *falseBlk = (i + 1 < conds) ? &func->blocks[blocks.tests[i + 1]]
                                         : &func->blocks[blocks.elseIdx];
        lowerIfCondition(*condExprs[i], testBlk, thenBlk, falseBlk, stmt.loc);

        func = ctx.function();
        thenBlk = &func->blocks[blocks.thens[i]];
        auto *exitBlk = &func->blocks[blocks.exitIdx];
        fallthrough = lowerIfBranch(thenStmts[i], thenBlk, exitBlk, stmt.loc) || fallthrough;
    }

    func = ctx.function();
    auto *elseBlk = &func->blocks[blocks.elseIdx];
    auto *exitBlk = &func->blocks[blocks.exitIdx];
    fallthrough = lowerIfBranch(stmt.else_branch.get(), elseBlk, exitBlk, stmt.loc) || fallthrough;

    if (!fallthrough)
    {
        func->blocks.pop_back();
        func = ctx.function();
        ctx.setCurrent(&func->blocks[blocks.elseIdx]);
        state.cur = ctx.current();
        state.after = nullptr;
        state.fallthrough = false;
        return state;
    }

    ctx.setCurrent(&func->blocks[blocks.exitIdx]);
    state.cur = ctx.current();
    state.after = state.cur;
    state.fallthrough = true;
    return state;
}

/// @brief Public entry point for lowering an IF statement.
///
/// @details Invokes @ref emitIf to build the CFG and then updates the lowering
///          context to the block reported in the returned @ref CtrlState.
/// @param stmt AST node representing the IF construct.
void Lowerer::lowerIf(const IfStmt &stmt)
{
    CtrlState state = emitIf(stmt);
    if (state.cur)
        context().setCurrent(state.cur);
}

} // namespace il::frontends::basic
