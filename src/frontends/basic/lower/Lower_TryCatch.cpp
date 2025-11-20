//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Lower_TryCatch.cpp
// Purpose: Lower BASIC ON ERROR and RESUME constructs by manipulating the
//          lowerer's exception-handling stack.
// Key invariants: Handler metadata in the procedure context reflects the most
//                 recent ON ERROR directive; resume tokens are only materialised
//                 when the target handler remains live.
// Ownership/Lifetime: Routines borrow the @ref Lowerer state and update handler
//                     caches owned by @ref ProcedureContext.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements ON ERROR/RESUME lowering for BASIC error-handling constructs.
/// @details Helpers manipulate the Lowerer's exception-handling stack metadata,
/// ensuring runtime handlers are installed and resumed according to the source
/// semantics without leaking state between statements.

#include "frontends/basic/Lowerer.hpp"

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Lower an @c ON @c ERROR directive to push or clear runtime handlers.
///
/// @details Establishes the correct handler block when @p stmt targets a line
///          number and clears state when @c ON @c ERROR @c GOTO @c 0 is
///          encountered.  The helper ensures the procedure context records the
///          active handler index and line for use by subsequent statements.
/// @param stmt AST node describing the ON ERROR directive.
void Lowerer::lowerOnErrorGoto(const OnErrorGoto &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    curLoc = stmt.loc;

    // NOTE: No-op here; curIdx tracking belongs in lowerTryCatch.

    if (stmt.toZero)
    {
        clearActiveErrorHandler();
        return;
    }

    clearActiveErrorHandler();

    BasicBlock *handler = ensureErrorHandlerBlock(stmt.target);
    emitEhPush(handler);

    size_t idx = static_cast<size_t>(handler - &func->blocks[0]);
    ctx.errorHandlers().setActive(true);
    ctx.errorHandlers().setActiveIndex(idx);
    ctx.errorHandlers().setActiveLine(stmt.target);
}

/// @brief Lower a RESUME statement that unwinds to a stored handler.
///
/// @details Finds the appropriate handler block (either by explicit line or the
///          currently active handler), materialises a resume token, and appends
///          the matching opcode to the handler block.  The helper bails out if no
///          live handler exists or if the block already terminated.
/// @param stmt AST node describing the RESUME statement.
void Lowerer::lowerResume(const Resume &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    if (!func)
        return;

    std::optional<size_t> handlerIndex;

    auto &handlersByLine = ctx.errorHandlers().blocks();
    if (auto it = handlersByLine.find(stmt.target); it != handlersByLine.end())
    {
        handlerIndex = it->second;
    }
    else if (auto active = ctx.errorHandlers().activeIndex())
    {
        handlerIndex = *active;
    }

    if (!handlerIndex || *handlerIndex >= func->blocks.size())
        return;

    BasicBlock &handlerBlock = func->blocks[*handlerIndex];
    if (handlerBlock.terminated)
        return;

    if (handlerBlock.params.size() < 2)
        return;

    // Retrieve the resume token via the builder to avoid manipulating
    // function valueNames directly.
    Value resumeTok = builder->blockParam(handlerBlock, 1);

    Instr instr;
    instr.type = Type(Type::Kind::Void);
    instr.loc = curLoc;
    instr.operands.push_back(resumeTok);

    switch (stmt.mode)
    {
        case Resume::Mode::Same:
            instr.op = Opcode::ResumeSame;
            break;
        case Resume::Mode::Next:
            instr.op = Opcode::ResumeNext;
            break;
        case Resume::Mode::Label:
        {
            instr.op = Opcode::ResumeLabel;
            auto &lineBlocks = ctx.blockNames().lineBlocks();
            auto targetIt = lineBlocks.find(stmt.target);
            if (targetIt == lineBlocks.end())
                return;
            size_t targetIdx = targetIt->second;
            if (targetIdx >= func->blocks.size())
                return;
            instr.labels.push_back(func->blocks[targetIdx].label);
            break;
        }
    }

    handlerBlock.instructions.push_back(std::move(instr));
    handlerBlock.terminated = true;
}

/// @brief Lower a TRY/CATCH statement using the runtime EH model.
///
/// Interaction model with legacy ON ERROR/RESUME:
/// - TRY installs a fresh handler using only `eh.push`/`eh.pop`, without mutating
///   the Lowerer's ErrorHandlerState (active/line/index). This ensures a preexisting
///   ON ERROR GOTO handler remains beneath the TRY handler on the runtime stack and
///   is automatically restored when TRY exits (single `eh.pop`).
/// - CATCH may include a RESUME statement. It is permitted but typically unnecessary,
///   because the canonical endpoint of the handler uses `resume.label %tok, ^after_try`.
///
/// Emission sequence:
/// - Emit `eh.push ^handler` before the try-body.
/// - Lower try-body; on normal fallthrough emit `eh.pop` and branch to `^after_try`.
/// - In the handler block (with `eh.entry` and params `%err`, `%tok`):
///     * Optionally initialise the catch variable with ERR() (i64) if resolvable.
///     * Lower the catch-body.
///     * Terminate with `resume.label %tok, ^after_try`.
void Lowerer::lowerTryCatch(const TryCatchStmt &stmt)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *current = ctx.current();
    if (!func || !current)
        return;

    curLoc = stmt.loc;

    // Capture the index of the current block before creating any new blocks,
    // since appending to the function's block list may reallocate and
    // invalidate raw pointers stored in the context.
    const std::size_t curIdx = ctx.currentIndex();

    // Create the post-try continuation block with a deterministic label.
    BlockNamer *blockNamer = ctx.blockNames().namer();
    const size_t afterIdx = func->blocks.size();
    std::string afterLbl =
        blockNamer ? blockNamer->generic("after_try") : mangler.block("after_try");
    builder->addBlock(*func, afterLbl);

    // Restore pointers that might be invalidated by block creation.
    func = ctx.function();
    BasicBlock *afterTry = &func->blocks[afterIdx];

    // Determine a stable handler key. Prefer the first statement inside TRY so
    // the handler is associated with that line; fall back to the TRY node.
    int handlerKey = virtualLine(stmt);
    if (!stmt.tryBody.empty())
    {
        for (const auto &sp : stmt.tryBody)
        {
            if (sp)
            {
                handlerKey = virtualLine(*sp);
                break;
            }
        }
    }

    // Pre-create handler block keyed by handlerKey so we can capture its label
    // before creating additional blocks that may reallocate the block vector.
    BasicBlock *preHandler = ensureErrorHandlerBlock(handlerKey);
    std::string preHandlerLabel = preHandler ? preHandler->label : std::string{};

    // Emit eh.push in a dedicated try-entry block to avoid attributing inner TRY
    // coverage to the parent line block. This also creates a clean structural
    // region for post-dominator checks.
    func = ctx.function();
    std::string tryEntryLbl =
        blockNamer ? blockNamer->generic("try_entry") : mangler.block("try_entry");
    builder->addBlock(*func, tryEntryLbl);
    BasicBlock *tryEntry = &func->blocks.back();
    // Branch from the original current block to the try-entry block.
    ctx.setCurrentByIndex(curIdx);
    emitBr(tryEntry);
    // Start TRY region in the new block.
    ctx.setCurrent(tryEntry);
    // Compute handler label deterministically and emit eh.push by label to avoid
    // dangling block pointers across vector reallocations.
    std::string handlerLabel = preHandlerLabel;
    {
        Instr in;
        in.op = Opcode::EhPush;
        in.type = Type(Type::Kind::Void);
        in.labels.push_back(handlerLabel);
        in.loc = curLoc;
        BasicBlock *block = ctx.current();
        if (block)
            block->instructions.push_back(std::move(in));
    }
    for (const auto &st : stmt.tryBody)
    {
        if (!st)
            continue;
        lowerStmt(*st);
        BasicBlock *cur = ctx.current();
        if (!cur || cur->terminated)
            break;
    }

    // On the normal path, pop the handler and branch to the continuation.
    if (ctx.current() && !ctx.current()->terminated)
    {
        // Lowering the try-body may have appended new blocks and reallocated
        // the function's block vector. Refresh the after_try pointer before use.
        func = ctx.function();
        afterTry = &func->blocks[afterIdx];
        emitEhPop();
        emitBr(afterTry);
    }

    // Switch insertion to the handler to lower the catch body.
    func = ctx.function();
    BasicBlock *handlerBlock = ensureErrorHandlerBlock(handlerKey);
    ctx.setCurrent(handlerBlock);

    // Lower the catch body statements.
    for (const auto &st : stmt.catchBody)
    {
        if (!st)
            continue;
        lowerStmt(*st);
        BasicBlock *cur = ctx.current();
        if (!cur || cur->terminated)
            break;
    }

    // Terminate handler with resume.label to after_try if not already terminated.
    handlerBlock = ctx.current();
    if (handlerBlock && !handlerBlock->terminated)
    {
        if (handlerBlock->params.size() >= 2)
        {
            // Refresh both handler and after_try pointers before emitting the terminator,
            // in case catch-body lowering appended blocks and caused reallocation.
            func = ctx.function();
            handlerBlock = ensureErrorHandlerBlock(handlerKey);
            afterTry = &func->blocks[afterIdx];
            builder->setInsertPoint(*handlerBlock);
            Value resumeTok2 = Value::temp(handlerBlock->params[1].id);
            builder->emitResumeLabel(resumeTok2, *afterTry, stmt.loc);
            handlerBlock->terminated = true;
        }
    }

    // Continue lowering at the after_try block.
    func = ctx.function();
    afterTry = &func->blocks[afterIdx];
    ctx.setCurrent(afterTry);
}

} // namespace il::frontends::basic
