//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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

    unsigned tokId = handlerBlock.params[1].id;
    // Ensure the parameter name is in the function's valueNames so it serializes correctly
    if (func->valueNames.size() <= tokId)
        func->valueNames.resize(tokId + 1);
    if (func->valueNames[tokId].empty())
        func->valueNames[tokId] = handlerBlock.params[1].name;

    Value resumeTok = Value::temp(tokId);

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

    // Create the post-try continuation block with a deterministic label.
    BlockNamer *blockNamer = ctx.blockNames().namer();
    const size_t afterIdx = func->blocks.size();
    std::string afterLbl = blockNamer ? blockNamer->generic("after_try") : mangler.block("after_try");
    builder->addBlock(*func, afterLbl);

    // Prepare handler block keyed by the TRY virtual line number; ensure it has (err, tok) params and eh.entry.
    // Using virtualLine() avoids collisions when source does not include explicit line numbers.
    BasicBlock *handler = ensureErrorHandlerBlock(virtualLine(stmt));

    // Restore pointers that might be invalidated by block creation.
    func = ctx.function();
    BasicBlock *afterTry = &func->blocks[afterIdx];

    // Push handler and lower the try body in the current block sequence.
    // Important: Do NOT touch ctx.errorHandlers().setActive* here. TRY/CATCH
    // composes as a pure push/pop atop any ON ERROR handler.
    emitEhPush(handler);
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
    // This restores any outer ON ERROR handler that existed prior to TRY.
    if (ctx.current() && !ctx.current()->terminated)
    {
        emitEhPop();
        emitBr(afterTry);
    }

    // Switch insertion to the handler to lower the catch body.
    func = ctx.function();
    handler = ensureErrorHandlerBlock(virtualLine(stmt));
    BasicBlock *handlerBlock = handler;
    ctx.setCurrent(handlerBlock);

    // If a catch variable was provided, attempt to initialise it from ERR().
    if (stmt.catchVar && !stmt.catchVar->empty())
    {
        const std::string &name = *stmt.catchVar;
        if (auto storage = resolveVariableStorage(name, stmt.loc))
        {
            // Extract i32 error code from handler %err and widen/sign-extend to i64.
            if (handlerBlock->params.size() >= 1)
            {
                unsigned errId = handlerBlock->params[0].id;
                // Ensure parameter names appear for deterministic printing.
                if (func->valueNames.size() <= errId)
                    func->valueNames.resize(errId + 1);
                if (func->valueNames[errId].empty())
                    func->valueNames[errId] = handlerBlock->params[0].name;

                Value errParam = Value::temp(errId);
                Value code32 = emitUnary(Opcode::ErrGetCode, Type(Type::Kind::I32), errParam);
                // Widen to i64 via a store/load pair and arithmetic shift to sign-extend 32->64.
                Value scratch = emitAlloca(sizeof(std::int64_t));
                emitStore(Type(Type::Kind::I64), scratch, Value::constInt(0));
                emitStore(Type(Type::Kind::I32), scratch, code32);
                Value code64 = emitLoad(Type(Type::Kind::I64), scratch);
                Value shl = emitBinary(Opcode::Shl, Type(Type::Kind::I64), code64, Value::constInt(32));
                code64 = emitBinary(Opcode::AShr, Type(Type::Kind::I64), shl, Value::constInt(32));

                emitStore(Type(Type::Kind::I64), storage->pointer, code64);
            }
        }
    }

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
            unsigned tokId = handlerBlock->params[1].id;
            if (func->valueNames.size() <= tokId)
                func->valueNames.resize(tokId + 1);
            if (func->valueNames[tokId].empty())
                func->valueNames[tokId] = handlerBlock->params[1].name;
            Value resumeTok = Value::temp(tokId);
            builder->emitResumeLabel(resumeTok, *afterTry, stmt.loc);
            handlerBlock->terminated = true;
        }
    }

    // Continue lowering at the after_try block.
    func = ctx.function();
    afterTry = &func->blocks[afterIdx];
    ctx.setCurrent(afterTry);
}

} // namespace il::frontends::basic
