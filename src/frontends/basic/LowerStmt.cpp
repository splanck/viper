//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/LowerStmt.cpp
// Purpose: Implement the dispatch visitor and shared helpers that lower BASIC
//          statements into IL.
// Key invariants: Statement visitation preserves the active Lowerer context and
//                 terminates traversal when the current block has emitted a
//                 terminator; runtime error helpers always split failure/continue
//                 control flow deterministically.
// Ownership/Lifetime: Operates on a caller-owned Lowerer instance and borrows
//                     AST nodes and IL modules without extending their lifetimes.
// Links: docs/codemap.md, docs/il-guide.md#reference
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements shared statement lowering utilities for the BASIC frontend.
/// @details The translation pipeline uses a @ref LowererStmtVisitor to dispatch
///          AST nodes into @ref Lowerer methods while maintaining a consistent
///          lowering context.  Additional helpers in this translation unit
///          normalise runtime channels and construct error-handling control
///          structures.

#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <optional>

using namespace il::core;

namespace il::frontends::basic
{

class LowererStmtVisitor final : public StmtVisitor
{
  public:
    /// @brief Construct a visitor that delegates BASIC statements to a lowerer.
    ///
    /// @details The visitor stores a reference to the @ref Lowerer so each
    ///          overridden @c visit call can simply forward the AST node.  The
    ///          reference avoids copying or ownership complications while keeping
    ///          the visitor cheap to instantiate per statement.
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Handle label statements that were already resolved during block creation.
    ///
    /// @details Labels participate in control-flow analysis before lowering so
    ///          the visitor does not emit additional IL when encountering them.
    void visit(const LabelStmt &) override {}

    /// @brief Lower a `PRINT` statement via the lowerer's dedicated helper.
    ///
    /// @param stmt BASIC `PRINT` statement containing the expression list.
    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }

    /// @brief Lower a `PRINT CHR$` statement that emits a single character.
    ///
    /// @param stmt BASIC `PRINT CHR$` statement.
    void visit(const PrintChStmt &stmt) override { lowerer_.lowerPrintCh(stmt); }

    /// @brief Lower a call statement targeting a procedure or function.
    ///
    /// @param stmt BASIC call statement.
    void visit(const CallStmt &stmt) override { lowerer_.lowerCallStmt(stmt); }

    /// @brief Lower a `CLS` statement that clears the display.
    ///
    /// @param stmt BASIC `CLS` statement.
    void visit(const ClsStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower a `COLOR` statement configuring foreground/background colours.
    ///
    /// @param stmt BASIC `COLOR` statement.
    void visit(const ColorStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower a `LOCATE` statement that positions the text cursor.
    ///
    /// @param stmt BASIC `LOCATE` statement.
    void visit(const LocateStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower an assignment statement.
    ///
    /// @param stmt BASIC `LET` statement (explicit or implicit assignment).
    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }

    /// @brief Lower a `DIM` statement when it introduces arrays.
    ///
    /// @details Scalar DIM declarations do not require runtime work, so only
    ///          array forms trigger @ref Lowerer::lowerDim.
    ///
    /// @param stmt BASIC `DIM` statement.
    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }

    /// @brief Lower a `REDIM` statement that resizes dynamic arrays.
    ///
    /// @param stmt BASIC `REDIM` statement.
    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }

    /// @brief Lower a `RANDOMIZE` statement.
    ///
    /// @param stmt BASIC `RANDOMIZE` statement with seed information.
    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }

    /// @brief Lower an `IF ... THEN ...` conditional statement.
    ///
    /// @param stmt BASIC conditional statement.
    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }

    /// @brief Lower a `SELECT CASE` statement.
    ///
    /// @param stmt BASIC select/case statement tree.
    void visit(const SelectCaseStmt &stmt) override { lowerer_.lowerSelectCase(stmt); }

    /// @brief Lower a `WHILE` loop.
    ///
    /// @param stmt BASIC `WHILE` loop statement.
    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }

    /// @brief Lower a `DO` loop.
    ///
    /// @param stmt BASIC `DO` loop statement.
    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }

    /// @brief Lower a `FOR` loop header.
    ///
    /// @param stmt BASIC `FOR` loop statement.
    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }

    /// @brief Lower a `NEXT` statement that finalises a FOR loop iteration.
    ///
    /// @param stmt BASIC `NEXT` statement.
    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }

    /// @brief Lower an `EXIT` statement used to break out of loops.
    ///
    /// @param stmt BASIC `EXIT` statement (FOR/DO/WHILE variants).
    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }

    /// @brief Lower a `GOTO` statement that performs an unconditional jump.
    ///
    /// @param stmt BASIC `GOTO` statement.
    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }

    /// @brief Lower a `GOSUB` statement that calls a subroutine by label.
    ///
    /// @param stmt BASIC `GOSUB` statement.
    void visit(const GosubStmt &stmt) override { lowerer_.lowerGosub(stmt); }

    /// @brief Lower an `OPEN` statement for file channels.
    ///
    /// @param stmt BASIC `OPEN` statement.
    void visit(const OpenStmt &stmt) override { lowerer_.lowerOpen(stmt); }

    /// @brief Lower a `CLOSE` statement closing an open channel.
    ///
    /// @param stmt BASIC `CLOSE` statement.
    void visit(const CloseStmt &stmt) override { lowerer_.lowerClose(stmt); }

    /// @brief Lower a `SEEK` statement that repositions a file handle.
    ///
    /// @param stmt BASIC `SEEK` statement.
    void visit(const SeekStmt &stmt) override { lowerer_.lowerSeek(stmt); }

    /// @brief Lower an `ON ERROR GOTO` statement configuring error handlers.
    ///
    /// @param stmt BASIC `ON ERROR GOTO` statement.
    void visit(const OnErrorGoto &stmt) override { lowerer_.lowerOnErrorGoto(stmt); }

    /// @brief Lower a `RESUME` statement that resumes execution after an error.
    ///
    /// @param stmt BASIC `RESUME` statement.
    void visit(const Resume &stmt) override { lowerer_.lowerResume(stmt); }

    /// @brief Lower an `END` statement that terminates the program or procedure.
    ///
    /// @param stmt BASIC `END` statement.
    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }

    /// @brief Lower an `INPUT` statement for numeric/text input.
    ///
    /// @param stmt BASIC `INPUT` statement.
    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }

    /// @brief Lower an `INPUT$` statement that reads a character.
    ///
    /// @param stmt BASIC `INPUT$` statement.
    void visit(const InputChStmt &stmt) override { lowerer_.lowerInputCh(stmt); }

    /// @brief Lower a `LINE INPUT` statement for buffered text input.
    ///
    /// @param stmt BASIC `LINE INPUT` statement.
    void visit(const LineInputChStmt &stmt) override { lowerer_.lowerLineInputCh(stmt); }

    /// @brief Lower a `RETURN` statement, including gosub returns.
    ///
    /// @param stmt BASIC `RETURN` statement.
    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }

    /// @brief Ignore function declarations encountered in statement lists.
    ///
    /// @details Declarations are handled in dedicated lowering passes.
    void visit(const FunctionDecl &) override {}

    /// @brief Ignore subroutine declarations encountered in statement lists.
    ///
    /// @details Subroutines are lowered elsewhere once symbol collection completes.
    void visit(const SubDecl &) override {}

    /// @brief Recursively lower nested statement lists.
    ///
    /// @param stmt Statement list container.
    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }

    /// @brief Lower a `DELETE` statement for file or entity deletion.
    ///
    /// @param stmt BASIC `DELETE` statement.
    void visit(const DeleteStmt &stmt) override { lowerer_.lowerDelete(stmt); }

    /// @brief Ignore constructor declarations delegated to class lowering.
    void visit(const ConstructorDecl &) override {}

    /// @brief Ignore destructor declarations delegated to class lowering.
    void visit(const DestructorDecl &) override {}

    /// @brief Ignore method declarations delegated to class lowering.
    void visit(const MethodDecl &) override {}

    /// @brief Ignore class declarations delegated to type lowering.
    void visit(const ClassDecl &) override {}

    /// @brief Ignore type declarations handled by the semantic analyser.
    void visit(const TypeDecl &) override {}

  private:
    /// @brief Lowerer used to emit IL for each visited statement.
    Lowerer &lowerer_;
};

/// @brief Lower a single AST statement using the visitor dispatcher.
///
/// @details Records the statement's source location for downstream diagnostics,
///          creates a @ref LowererStmtVisitor to perform double-dispatch, and
///          invokes @c accept on the AST node so it can call the appropriate
///          @ref Lowerer method.
///
/// @param stmt AST statement scheduled for lowering.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    stmt.accept(visitor);
}

/// @brief Lower all statements contained in a BASIC statement list.
///
/// @details Iterates over the child pointers, skipping null entries produced by
///          earlier transformations, and stops once the active block has been
///          terminated to avoid generating unreachable code.  Each surviving
///          child is forwarded to @ref lowerStmt.
///
/// @param stmt Statement list node that owns the sequence of statements.
void Lowerer::lowerStmtList(const StmtList &stmt)
{
    for (const auto &child : stmt.stmts)
    {
        if (!child)
            continue;
        BasicBlock *current = context().current();
        if (current && current->terminated)
            break;
        lowerStmt(*child);
    }
}

/// @brief Lower a call statement whose body is a procedure invocation.
///
/// @details Some statements may lose their call expression during semantic
///          analysis (for example, when an intrinsic is folded).  The helper
///          guards against @c nullptr before delegating to @ref lowerExpr, which
///          emits the actual call.
///
/// @param stmt BASIC call statement that should be lowered.
void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (!stmt.call)
        return;
    curLoc = stmt.loc;
    lowerExpr(*stmt.call);
}

/// @brief Lower a return statement, including gosub variants.
///
/// @details Distinguishes gosub returns from procedure returns and routes them
///          through @ref lowerGosubReturn when necessary.  When lowering a
///          standard return the helper evaluates the optional value expression,
///          emits a typed return when present, or falls back to a void return.
///
/// @param stmt BASIC `RETURN` statement under lowering.
void Lowerer::lowerReturn(const ReturnStmt &stmt)
{
    if (stmt.isGosubReturn)
    {
        lowerGosubReturn(stmt);
        return;
    }

    if (stmt.value)
    {
        RVal v = lowerExpr(*stmt.value);
        emitRet(v.value);
    }
    else
    {
        emitRetVoid();
    }
}

/// @brief Convert a runtime channel expression to the canonical 32-bit type.
///
/// @details Runtime helpers expect channels as 32-bit integers.  If the input
///          already satisfies that requirement the value is returned unchanged;
///          otherwise the routine widens the value to 64 bits to reuse range
///          checks before narrowing with a checked cast to i32.
///
/// @param channel Evaluated expression representing the channel.
/// @param loc Source location used for diagnostic attribution.
/// @return Normalised channel value with i32 type.
Lowerer::RVal Lowerer::normalizeChannelToI32(RVal channel, il::support::SourceLoc loc)
{
    if (channel.type.kind == Type::Kind::I32)
        return channel;

    channel = ensureI64(std::move(channel), loc);
    curLoc = loc;
    channel.value = emitUnary(Opcode::CastSiNarrowChk, Type(Type::Kind::I32), channel.value);
    channel.type = Type(Type::Kind::I32);
    return channel;
}

/// @brief Split control flow based on a runtime error indicator.
///
/// @details Emits dedicated failure and continuation blocks, branches on whether
///          @p err is non-zero, and invokes the supplied callback inside the
///          failure block so the caller can emit diagnostics or traps.  Labels
///          are derived deterministically from @p labelStem so repeated calls
///          remain stable across runs.
///
/// @param err Value returned from a runtime helper where non-zero denotes error.
/// @param loc Source location of the originating runtime call.
/// @param labelStem Stem used to generate deterministic block labels.
/// @param onFailure Callback invoked inside the failure block.
void Lowerer::emitRuntimeErrCheck(Value err,
                                  il::support::SourceLoc loc,
                                  std::string_view labelStem,
                                  const std::function<void(Value)> &onFailure)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (!func || !original)
        return;

    size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string stem(labelStem);
    std::string failLbl = blockNamer ? blockNamer->generic(stem + "_fail")
                                     : mangler.block(stem + "_fail");
    std::string contLbl = blockNamer ? blockNamer->generic(stem + "_cont")
                                     : mangler.block(stem + "_cont");

    size_t failIdx = func->blocks.size();
    builder->addBlock(*func, failLbl);
    size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    BasicBlock *failBlk = &func->blocks[failIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];

    ctx.setCurrent(&func->blocks[curIdx]);
    curLoc = loc;
    Value isFail = emitBinary(Opcode::ICmpNe, ilBoolTy(), err, Value::constInt(0));
    emitCBr(isFail, failBlk, contBlk);

    ctx.setCurrent(failBlk);
    curLoc = loc;
    onFailure(err);

    ctx.setCurrent(contBlk);
}

} // namespace il::frontends::basic
