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
    if (auto it = handlersByLine.find(stmt.line); it != handlersByLine.end())
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

} // namespace il::frontends::basic
