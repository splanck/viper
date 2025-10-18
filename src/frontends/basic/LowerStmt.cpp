//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the core statement visitor and shared lowering utilities used across
// BASIC statement lowering modules.  The implementation wires BASIC AST nodes
// to the lowering pipeline so control-flow helpers can be implemented in
// dedicated translation units without duplicating the visitor scaffolding.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the central BASIC statement lowering entry points.
/// @details This translation unit houses the `LowererStmtVisitor` along with
///          the handful of shared `Lowerer` helpers that manipulate procedure
///          state.  The visitor performs dynamic dispatch on AST statement
///          kinds and forwards each node to the specialised lowering routines
///          maintained in sibling files.

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
    /// @brief Bind the visitor to a lowering context.
    ///
    /// @details The visitor stores a reference to the ambient `Lowerer` so it
    ///          can translate AST statements into IL without needing to thread
    ///          the lowering state through every `visit` override manually.
    ///          The constructor performs no additional work beyond capturing the
    ///          reference because all heavy lifting occurs in the overrides.
    explicit LowererStmtVisitor(Lowerer &lowerer) noexcept : lowerer_(lowerer) {}

    /// @brief Ignore label declarations because they do not emit IL directly.
    ///
    /// @details Labels are resolved during control-flow lowering; visiting the
    ///          node here would be redundant, so the override intentionally
    ///          performs no work.
    void visit(const LabelStmt &) override {}

    /// @brief Lower a BASIC `PRINT` statement by delegating to the `Lowerer`.
    ///
    /// @details The lowering logic is implemented in `Lowerer::lowerPrint`; the
    ///          visitor simply forwards the AST node to keep dispatch centralised.
    void visit(const PrintStmt &stmt) override { lowerer_.lowerPrint(stmt); }

    /// @brief Lower the `PRINT #` channel statement.
    ///
    /// @details Delegates to `Lowerer::lowerPrintCh`, which handles evaluating
    ///          the target channel and formatting semantics.
    void visit(const PrintChStmt &stmt) override { lowerer_.lowerPrintCh(stmt); }

    /// @brief Lower a procedure call statement.
    ///
    /// @details Forwards to `Lowerer::lowerCallStmt`, allowing the lowering
    ///          core to manage argument evaluation and call emission.
    void visit(const CallStmt &stmt) override { lowerer_.lowerCallStmt(stmt); }

    /// @brief Lower screen clearing statements such as `CLS`.
    ///
    /// @details Reuses the general-purpose `Lowerer::visit` helper that owns
    ///          the runtime bindings for these statements.
    void visit(const ClsStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower colour-setting statements.
    ///
    /// @details The actual runtime conversion is handled by `Lowerer::visit`, so
    ///          the visitor simply passes along the AST node.
    void visit(const ColorStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower cursor positioning statements like `LOCATE`.
    ///
    /// @details Dispatches to the generic visit helper that converts arguments
    ///          into runtime calls.
    void visit(const LocateStmt &stmt) override { lowerer_.visit(stmt); }

    /// @brief Lower assignment statements.
    ///
    /// @details Delegates to `Lowerer::lowerLet`, which performs expression
    ///          evaluation and destination resolution.
    void visit(const LetStmt &stmt) override { lowerer_.lowerLet(stmt); }

    /// @brief Lower array dimension declarations.
    ///
    /// @details Only `DIM` statements that declare an array require lowering.
    ///          Scalars are handled entirely by the semantic layer, so the
    ///          visitor filters them out before calling `Lowerer::lowerDim`.
    void visit(const DimStmt &stmt) override
    {
        if (stmt.isArray)
            lowerer_.lowerDim(stmt);
    }

    /// @brief Lower `REDIM` statements to resize existing arrays.
    ///
    /// @details Simply forwards to `Lowerer::lowerReDim`, which contains the
    ///          runtime glue for issuing the resize call.
    void visit(const ReDimStmt &stmt) override { lowerer_.lowerReDim(stmt); }

    /// @brief Lower `RANDOMIZE` statements.
    ///
    /// @details Delegates to the lowering core that emits the runtime seed call
    ///          and error handling.
    void visit(const RandomizeStmt &stmt) override { lowerer_.lowerRandomize(stmt); }

    /// @brief Lower conditional statements.
    ///
    /// @details Uses `Lowerer::lowerIf` to emit the branching control flow.
    void visit(const IfStmt &stmt) override { lowerer_.lowerIf(stmt); }

    /// @brief Lower `SELECT CASE` statements.
    ///
    /// @details Delegates to the specialised lowering routine that materialises
    ///          jump tables and case comparisons.
    void visit(const SelectCaseStmt &stmt) override { lowerer_.lowerSelectCase(stmt); }

    /// @brief Lower `WHILE` loops.
    ///
    /// @details Forwards to `Lowerer::lowerWhile`, which constructs the loop
    ///          control-flow skeleton and emits the condition evaluation.
    void visit(const WhileStmt &stmt) override { lowerer_.lowerWhile(stmt); }

    /// @brief Lower `DO` loops with their optional tests.
    ///
    /// @details Delegates to `Lowerer::lowerDo`, centralising the pre/post-test
    ///          handling inside the lowering core.
    void visit(const DoStmt &stmt) override { lowerer_.lowerDo(stmt); }

    /// @brief Lower `FOR` loops.
    ///
    /// @details The lowering logic emits induction variable initialisation and
    ///          the loop body; the visitor merely forwards the AST node.
    void visit(const ForStmt &stmt) override { lowerer_.lowerFor(stmt); }

    /// @brief Lower `NEXT` loop terminators.
    ///
    /// @details Uses `Lowerer::lowerNext` to update induction variables and
    ///          branch back to the loop header.
    void visit(const NextStmt &stmt) override { lowerer_.lowerNext(stmt); }

    /// @brief Lower loop `EXIT` statements.
    ///
    /// @details Relies on `Lowerer::lowerExit` to thread the appropriate control
    ///          flow edges and cleanup.
    void visit(const ExitStmt &stmt) override { lowerer_.lowerExit(stmt); }

    /// @brief Lower unconditional `GOTO` jumps.
    ///
    /// @details Delegates to `Lowerer::lowerGoto`, ensuring label resolution and
    ///          branch emission remain consistent with other control-flow logic.
    void visit(const GotoStmt &stmt) override { lowerer_.lowerGoto(stmt); }

    /// @brief Lower `GOSUB` invocations that push a return address.
    ///
    /// @details `Lowerer::lowerGosub` performs the stack management; this
    ///          override only forwards the AST node.
    void visit(const GosubStmt &stmt) override { lowerer_.lowerGosub(stmt); }

    /// @brief Lower file `OPEN` statements.
    ///
    /// @details Delegates to the runtime-aware lowering helper.
    void visit(const OpenStmt &stmt) override { lowerer_.lowerOpen(stmt); }

    /// @brief Lower `CLOSE` statements.
    ///
    /// @details Forwards to the file-runtime lowering logic.
    void visit(const CloseStmt &stmt) override { lowerer_.lowerClose(stmt); }

    /// @brief Lower `SEEK` statements.
    ///
    /// @details Relies on `Lowerer::lowerSeek` to emit runtime positioning
    ///          operations for file handles.
    void visit(const SeekStmt &stmt) override { lowerer_.lowerSeek(stmt); }

    /// @brief Lower `ON ERROR GOTO` statements.
    ///
    /// @details Calls `Lowerer::lowerOnErrorGoto`, which sets up resume targets
    ///          within the procedure context.
    void visit(const OnErrorGoto &stmt) override { lowerer_.lowerOnErrorGoto(stmt); }

    /// @brief Lower `RESUME` statements.
    ///
    /// @details Delegates to `Lowerer::lowerResume` to emit appropriate trap
    ///          handling control flow.
    void visit(const Resume &stmt) override { lowerer_.lowerResume(stmt); }

    /// @brief Lower `END` statements.
    ///
    /// @details Uses `Lowerer::lowerEnd` to emit procedure or program
    ///          termination semantics.
    void visit(const EndStmt &stmt) override { lowerer_.lowerEnd(stmt); }

    /// @brief Lower `INPUT` statements for buffered input.
    ///
    /// @details Forwards to `Lowerer::lowerInput`, which expands the runtime
    ///          interaction and assignment logic.
    void visit(const InputStmt &stmt) override { lowerer_.lowerInput(stmt); }

    /// @brief Lower `INPUT #` channel statements.
    ///
    /// @details Uses `Lowerer::lowerInputCh` to connect the AST node with the
    ///          runtime channel API.
    void visit(const InputChStmt &stmt) override { lowerer_.lowerInputCh(stmt); }

    /// @brief Lower `LINE INPUT #` channel statements.
    ///
    /// @details Delegates to `Lowerer::lowerLineInputCh`, which issues the
    ///          appropriate runtime call and handles result assignment.
    void visit(const LineInputChStmt &stmt) override { lowerer_.lowerLineInputCh(stmt); }

    /// @brief Lower `RETURN` statements.
    ///
    /// @details Directly forwards to `Lowerer::lowerReturn` so the lowering core
    ///          can decide between regular returns and `GOSUB` unwinds.
    void visit(const ReturnStmt &stmt) override { lowerer_.lowerReturn(stmt); }

    /// @brief Ignore nested procedure declarations encountered in statement
    ///        position.
    ///
    /// @details Procedure definitions are handled during top-level lowering, so
    ///          statement visits must not emit additional IL for them.
    void visit(const FunctionDecl &) override {}

    /// @brief Ignore nested subroutine declarations.
    ///
    /// @details As with `FunctionDecl`, subroutine nodes are processed by the
    ///          dedicated declaration lowering pipeline.
    void visit(const SubDecl &) override {}

    /// @brief Lower nested statement lists (e.g., compound bodies).
    ///
    /// @details Delegates back into `Lowerer::lowerStmtList` to reuse the same
    ///          traversal logic used for top-level sequences.
    void visit(const StmtList &stmt) override { lowerer_.lowerStmtList(stmt); }

  private:
    Lowerer &lowerer_;
};

/// @brief Lower a single BASIC statement through dynamic dispatch.
///
/// @details Records the source location for diagnostic fidelity, instantiates
///          a `LowererStmtVisitor`, and asks the AST node to accept it.  The
///          visitor forwards the call to the appropriate lowering helper.
void Lowerer::lowerStmt(const Stmt &stmt)
{
    curLoc = stmt.loc;
    LowererStmtVisitor visitor(*this);
    stmt.accept(visitor);
}

/// @brief Lower a list of statements until control flow terminates the block.
///
/// @details Iterates the nested statements, skipping null placeholders, and
///          stops when the active basic block has a terminator.  Each statement
///          is lowered via `lowerStmt`, ensuring consistent location tracking.
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

/// @brief Lower an explicit call statement (CALL or CALL SUB).
///
/// @details Ensures a call expression is present before forwarding to
///          `lowerExpr` for evaluation.  The emitted expression materialises the
///          IL call and discards any produced value, mirroring BASIC semantics.
void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (!stmt.call)
        return;
    curLoc = stmt.loc;
    lowerExpr(*stmt.call);
}

/// @brief Lower a return statement, handling both GOSUB and function returns.
///
/// @details Distinguishes GOSUB unwinds from regular procedure returns.  For
///          standard returns it evaluates the optional value, emits a return of
///          the resulting IL value when present, or emits a void return
///          otherwise.
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

/// @brief Convert a channel expression to a 32-bit integer handle.
///
/// @details If the value is already 32-bit, the function returns it unchanged.
///          Otherwise the expression is coerced to 64-bit, narrowed with the
///          runtime-checked `CastSiNarrowChk` opcode, and the resulting handle
///          annotated as `i32`.  Diagnostic locations are updated to the call
///          site so narrowing failures report correctly.
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

/// @brief Emit a conditional runtime error check around an operation.
///
/// @details Constructs fail/continue blocks that branch based on @p err being
///          non-zero.  The helper temporarily inserts new blocks, sets the
///          current block to the failure case, invokes @p onFailure to emit
///          diagnostics or cleanup, and finally resumes execution in the
///          continuation block.  Block labels are derived from @p labelStem to
///          maintain readable names for debugging.
///
/// @param err       Value indicating whether the preceding runtime call failed.
/// @param loc       Source location associated with the guarded operation.
/// @param labelStem Stem used to derive fail/continue block labels.
/// @param onFailure Callback invoked with @p err in the failure block.
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
